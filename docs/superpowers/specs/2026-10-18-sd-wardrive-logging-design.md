# SD wardrive logging — design

Design, 2026-10-18.

## Intent

Make the platform produce the file it exists to produce: a WigleWifi-1.6 CSV
on an SD card. The driver opens a drive with a start control and closes it with
stop; while a session is active the file is rewritten on a timer, so a crash or
brownout costs at most one flush interval and the drive resumes on reboot.

Everything upstream of the file already works. This is the last link.

## What already exists

The export is not missing — it is built, and it runs today over the serial
console.

- `cap::exportRows(Print&, uint32_t& skipped)` — WiFi rows.
- `blescan::exportRows(Print&, uint32_t& skipped)` — Bluetooth rows, emitted
  after the WiFi ones, with the `Misc [LE]` AuthMode convention WiGLE expects.
- Both dedup tables store `lat7`, `lon7`, `firstEpoch` and a `located` flag.
  Both drop any row without a position or without a UTC clock rather than
  faking it (`capture.cpp:739`, `blescan.cpp:460`).
- `csv.h` holds every formatter: `deg7` (fixed-point degrees, no soft-float
  printf), RFC-4180 `quote`, `stampUtc` (the only FirstSeen format WiGLE
  reads), `mac`.
- The `WIGLE` console command (`main.cpp:183-200`) already writes the complete
  file to `Serial`: the `WigleWifi-1.6,...` preamble, the column header, then
  both exporters, then a row/skip summary.
- Node findings fold into the Core's dedup table through `cap::ingest`, so the
  export is **already mesh-wide** — a node's sightings export exactly like the
  Core's own. The multi-node wardriving story is complete on the output side.

So a user can, with today's firmware, drive and capture a valid wardrive over
USB. What is missing is on-device storage: the same bytes, to a card, on a
timer, without losing the file to a crash.

## What is missing

1. Mount the SD card.
2. Write that same WiGLE stream to a file, crash-safely (temp then rename).
3. A start/stop session that gates the flush timer, with an on-screen control
   and indicator, auto-resuming after a reboot that interrupted a session.
4. Wire `s.sdOk` to the real mount state; name the session file; persist the
   active flag so a reboot can resume.

## Architecture

### 1. Share the WiGLE stream, don't duplicate it

The preamble and column header currently live inline in the `WIGLE` console
handler. The SD writer needs the identical bytes, and `capture.cpp:726` already
states the rule: *"main writes the markers and the header, because the file is
one file and the two exporters must not disagree about them."* A second copy of
the header is a second chance to disagree.

Extract one function, in `main` (or a small `device/wigle.h` helper), taking a
`Print&`:

```c
// Write a complete WigleWifi-1.6 document: preamble, column header, WiFi rows,
// then Bluetooth rows. Returns rows written; `skipped` accumulates rows with
// no position or no UTC clock. The sink is a Print, so it serves Serial and an
// SD File from one body.
uint32_t writeWigle(Print& out, uint32_t& skipped);
```

The `WIGLE` console command calls it with `Serial`; the SD flush calls it with
a `File`. The preamble and header string move into this function verbatim from
`main.cpp:188-193` — unchanged bytes, so a serial capture and a card capture
are byte-for-byte the same document.

### 2. `device/sdlog.{h,cpp}` — the card and the writer

A new subsystem owning the SD card. It is the only new peripheral in this work.

```c
namespace sdlog {

// Mount on the shared SPI bus with the SD's CS. False (and stays false) if no
// card is present or the mount fails. Never blocks the loop retrying.
bool begin();

bool present();         // a card is mounted

// Write a complete document to `name`, crash-safely: open "name.tmp", run
// `body` into it, close, rename over "name". The previous good "name" survives
// any failure. `logging` owns the name; `sdlog` owns the card mechanics.
bool writeAtomic(const char* name, uint32_t (*body)(Print&, uint32_t&),
                 uint32_t& rows, uint32_t& skipped);

} // namespace sdlog
```

`body` is `writeWigle` from §1 — `sdlog` knows nothing of WiGLE, only how to
put a document on the card without a half-written file surviving. The session
name and the rows/skipped bookkeeping belong to `logging`.

### 3. Crash-safe write: temp then rename

Continuous logging is only trustworthy if an interrupted flush cannot destroy
the last good file. Each flush writes the full table to `<name>.tmp`, then
renames it over `<name>`. FAT's rename replaces the directory entry in one
operation, so `<name>` is always a complete earlier flush, never a half-written
one. A crash during the write damages only `.tmp`, which the next flush
overwrites.

The table is rewritten in full each flush. This is O(table) and the stall grows
with the table — see Risks. True append is rejected: dedup updates an AP's
recorded position when a louder sighting arrives, so the correct row for a BSSID
changes after it was first written, and an append-only file would carry stale
positions. Full rewrite is the price of WiGLE wanting one correct row per AP.

### 4. `logging` — the session state machine

Logging is a session the user opens and closes, not a thing that happens
whenever a card is present. This state machine is the one piece that is pure
and host-testable; it owns *when* to write and calls `sdlog` to do the writing.

```c
namespace logging {

void begin(bool wasActive);  // at boot: resume if we died logging (see below)
void toggle();               // the control calls this; a GPIO button could too
void start();
void stop();                 // forces a final flush, then closes the session
void tick(uint32_t nowMs, uint32_t nowEpoch);  // runs the flush timer

enum State : uint8_t { IDLE, ACTIVE, NO_CARD };
State    state();
uint32_t sessionSecs(uint32_t nowMs);  // for the REC indicator's clock

} // namespace logging
```

- **Start** opens a new session file and begins the flush timer. If no card is
  mounted, the state is `NO_CARD` and the intent is remembered, so inserting a
  card then begins logging.
- **Stop** forces one final flush — capturing the last interval — then closes
  the session. A clean stop therefore leaves a file current to the second.
- While `ACTIVE`, `tick` calls `sdlog::writeAtomic` with the session name and `writeWigle` every `FLUSH_MS` (start at 30000,
  a compile-time constant). The flush runs synchronously in `loop()`; the C5 is
  single-core and capture advances only when `loop()` calls `cap::tick`, so
  capture pauses for the flush's runtime and resumes after. No explicit hop
  pause is needed.

`FLUSH_MS` is a constant, not a setting, for v1: ATTUNE is at its documented
seven-row ceiling.

**Input-agnostic by construction.** `toggle()` is the single entry point. The
on-screen control calls it; a physical GPIO button, if one is added later,
calls the same function from its debounced handler. No logging logic lives in
the touch code or would live in a button ISR — they only call `toggle()`.

### 5. The control and the indicator

An on-screen touch control on the SCAN screen, because logging state must be
visible exactly while you are scanning. It is both the indicator and the
button:

- `IDLE` — `○ LOG`, tappable to start.
- `ACTIVE` — `● REC mm:ss`, the session clock running, tappable to stop. The
  filled dot is the one unambiguous "you are recording" signal.
- `NO_CARD` — `○ LOG` struck through or dimmed, so a tap that cannot start
  logging does not look like one that failed silently.

Exact placement is settled in the plan against the 160 px layout (the SCAN
header's spare middle, or the footer). The hit region goes in `ui/layout.h`
beside the others so draw and hit-test cannot drift, per that file's reason for
existing. It renders and is tappable in the simulator.

### 6. Boot and reboot: auto-resume

The active flag is persisted the instant it changes (see §8), so a brownout or
crash is survivable. At boot, `logging::begin(wasActive)` is told whether the
device was logging when it last stopped running:

- Was logging → resume into a **new** session file, indicator `ACTIVE`. A crash
  never silently ends a drive; you keep recording until you press stop.
- Was idle → start `IDLE`.

Resuming into a new file rather than reopening the old one is deliberate: the
pre-crash file is already a complete, uploadable document (temp-then-rename),
and a fresh file after a reboot keeps each continuous span self-contained.

### 7. Filename

`utcEpoch` returns 0 until an RMC sentence has supplied a date (GGA carries
time but not date; `gnss.h` is explicit that a GGA-only stamp is "right about
the hour and wrong about the year"). A session is named once, when it opens:

- Date known → `WD-YYYYMMDD-HHMMSS.CSV` (seconds included, because start/stop/
  start and reboots produce several sessions that must not collide).
- No date yet → `WD-NNNN.CSV`, incrementing past any file already on the card.

Later flushes within a session rewrite that same file.

### 8. Status and persistence

`s.sdOk = sdlog::present()` so the footer stops asserting a card that was never
checked (`main.cpp:276` currently hardcodes it false). The last flush's
`rows`/`skipped` surface where it is cheap — the gap between them is exactly
"what will not upload to WiGLE", the one number a driver wants mid-session.

The active flag is persisted in NVS **immediately on every start and stop**,
not through the debounced `Settings` path. Logging state and preferences have
different persistence needs: a preference can settle for two seconds before it
is written, but if the flag that drives auto-resume is not on flash the instant
you press start, a brownout one second later loses the fact that you were
logging. So this is its own tiny NVS key (`prefs::setLogging(bool)`, an
immediate write), kept out of the `Settings` struct and its round-trip. It is
session state, not a knob.

## The primary risk: three devices on one SPI bus

`main.cpp:27` — *"LCD, touch and the SD card all share one SPI bus."* The panel
drives it through an `Arduino_ESP32SPI` bus object at 40 MHz; touch uses the
Arduino `SPI` object at 2 MHz; the `SD` library wants it too. The C5 has few
SPI peripherals, so these must share one controller without corrupting each
other's transactions.

This is the real engineering of the task and it **cannot be settled on the
host** — the simulator has no SPI. It is settled only on hardware, by
confirming that a flush mid-session does not garble the panel or wedge touch,
and that SD init at its lower clock coexists with the panel at 40 MHz. The
likely shape is explicit bus handoff: quiesce the panel, run the SD transaction
at SD clock, restore. The plan must treat this as the item most likely to need
hardware iteration, and must not report the feature verified until it has run on
a board.

## Hardware unknowns to confirm on-board

- **The SD chip-select pin.** Not in the code (`PIN_CS = 23` is the LCD's) and
  not in any public source for the NM-CYD-C5. Confirm from the board or its
  schematic. SCK/MOSI/MISO (6/7/2) are the shared bus and already defined.
- **That the slot is on the shared SPI bus** rather than a separate SDMMC
  route. The `main.cpp` comment says SPI; confirm against the board.

## What ships host-verified vs hardware-pending

**Host-verified now, no board:**
- The `csv.h` formatters, which are the WiGLE-acceptance-critical logic and are
  currently untested. A new `test_csv` pins: `deg7` precision and sign across
  known coordinates; `quote` against names with commas, quotes, and embedded
  control bytes (RFC 4180); `stampUtc` against known epochs including the
  date-rollover the gnss comment warns about; `mac` formatting. This is where
  a silent format error would cost a whole drive's upload, so it is worth a
  test even though the code is written.
- The `logging` state machine, with `sdlog` stubbed by a fake that records
  calls instead of touching a card. A `test_logging` pins: start opens a
  session and arms the timer; the timer flushes at `FLUSH_MS` and not before;
  stop forces exactly one final flush and closes; `toggle` alternates;
  `begin(true)` resumes into a new session while `begin(false)` stays idle;
  `NO_CARD` is entered and left as a card appears. The state machine is where
  the start/stop intent lives, so it is tested without hardware.
- The on-screen control and its three indicator states render and are tappable
  in the simulator.

**Hardware-pending, cannot be host-tested:**
- The SD mount, the physical write, the temp/rename, and the bus coexistence.
- The `writeWigle`/`exportRows` row emission is device-only to compile
  (`capture.cpp`/`blescan.cpp` include Arduino and BLE headers), but it is
  already exercised today by the `WIGLE` console command, so redirecting its
  output to a `File` carries no new row-formatting risk — only file-I/O risk.

## Verification

| Claim | How |
|---|---|
| `csv.h` formats WiGLE-valid fields | `test_csv`, host, in the project idiom |
| start/stop/toggle/resume/timer behave | `test_logging` against a fake `sdlog`, host |
| `writeWigle` output is unchanged for Serial | `WIGLE` console output byte-compared before/after the header extraction |
| The control shows the right state | sim render at `IDLE` / `ACTIVE` / `NO_CARD` |
| Host suite and render regression still green | `make -s test`, `make -s regress` |
| Firmware builds | `pio run -e nm-cyd-c5` |
| **Card mounts, file is valid WiGLE** | **hardware: upload to WiGLE, or diff a card capture against a serial capture of the same table** |
| **A flush does not corrupt panel or touch** | **hardware: observe a flush mid-session** |
| **Stop leaves a complete file; pulled power mid-flush leaves a valid file** | **hardware: stop and parse; then cut power during a write and confirm `<name>.csv` still parses** |
| **Auto-resume after a reboot while logging** | **hardware: reboot mid-session, confirm it resumes into a new file** |

The hardware rows are the real acceptance tests and all need a board. Nothing
in this feature may be called verified on hardware until they pass.

## Out of scope

- A **physical GPIO start/stop button**. Decided for later, not now: the touch
  control ships first and the logic is input-agnostic (`logging::toggle()`), so
  a debounced button handler drops in with no change to the state machine once
  a free GPIO is confirmed on the board. Eyes-free toggling while driving is
  the reason to add it.
- A log-interval setting (ATTUNE is full; a later follow-up).
- Cellular/Bluetooth-beacon columns beyond what `exportRows` already emits.
- Reading the card back, browsing logs on the device, or deleting them.
- Node-local logging on the satellites. That is the next subsystem: a node
  currently buffers 192 records in RAM and loses them on link drop, so its own
  capture should become an authoritative local log with the mesh as telemetry
  on top. Named here so it is not forgotten; specified separately.

## Serial: card-only timer, console command kept

Decided. The timer writes **only to the card**, so a battery session spends no
bus time on a serial sink nobody is reading. The `WIGLE` console command stays
as-is for on-demand serial dumps when tethered. Two export paths, one shared
`writeWigle` body.

## End of a drive: press stop, pull the card, upload

Stop forces a final flush, so pressing it leaves the session file current to
the second — a complete, valid WigleWifi-1.6 document ready to upload. Pull the
card, upload the `.csv`. Nothing else to finalize.

And because every flush is temp-then-rename, the file is a complete document at
*every* instant, not only after stop. So the one remaining edge — power cut
while `ACTIVE`, between flushes, without pressing stop — costs at most
`FLUSH_MS` (≤30 s) of the most recent captures from that file, and auto-resume
(§6) then continues the drive into a new file on reboot. A deliberate stop has
no such gap.

Two ways to get the data off, both already covered: pull the card (primary), or
`WIGLE` over serial while tethered (the existing command, unchanged).
