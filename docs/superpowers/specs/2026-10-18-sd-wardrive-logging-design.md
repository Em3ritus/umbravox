# SD wardrive logging — design

Design, 2026-10-18.

## Intent

Make the platform produce the file it exists to produce: a WigleWifi-1.6 CSV
on an SD card, written continuously while driving so a crash or brownout costs
at most one flush interval.

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
2. Write that same WiGLE stream to a file instead of (or as well as) `Serial`.
3. Do it on a timer, crash-safely.
4. Wire `s.sdOk` to the real mount state; name the file.

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

// The whole flush: pick/keep the filename, open "<name>.tmp", call
// writeWigle() into it, close, rename over "<name>". Returns success and
// stashes last rows/skipped. A failure leaves the previous good file intact.
bool flush(uint32_t nowEpoch);

bool     ok();          // card mounted and last flush (if any) succeeded
uint32_t lastRows();
uint32_t lastSkipped();

} // namespace sdlog
```

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

### 4. Trigger: a timer in `loop()`

Every `FLUSH_MS` (start at 30000, a compile-time constant), the loop calls
`sdlog::flush(utcEpoch)`. The flush runs synchronously in `loop()`; because the
C5 is single-core and capture advances only when `loop()` calls `cap::tick`,
capture naturally pauses for the duration of the flush and resumes after. No
explicit hop pause is needed — the stall is the flush's own runtime.

`FLUSH_MS` is a constant, not a setting, for v1. ATTUNE is at its documented
seven-row ceiling; a "log interval" knob is a separate small follow-up if
wanted, and would belong on the appearance/settings work only after that
screen gains room.

### 5. Filename

`utcEpoch` returns 0 until an RMC sentence has supplied a date (GGA carries
time but not date; `gnss.h` is explicit that a GGA-only timestamp is "right
about the hour and wrong about the year"). So:

- Once `utcEpoch` is nonzero at the first flush, name the file from it:
  `WD-YYYYMMDD-HHMM.CSV` (8.3-friendly uppercase, as it lands on FAT).
- If no date yet at the first flush, use `WD-0001.CSV`, incrementing past any
  file that already exists, and keep that name for the session.

One file per power-up session; later flushes rewrite the same name.

### 6. Status

`s.sdOk = sdlog::ok()` so the footer stops asserting a card that was never
checked (`main.cpp:276` currently hardcodes it false). Surface the last flush's
`rows`/`skipped` where it is cheap — the gap between them is exactly "what will
not upload to WiGLE", which is the one number a driver wants mid-session. The
console `WIGLE` summary line already states it; a line in the MESH or a status
screen is enough and no new screen is required.

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
| `writeWigle` output is unchanged for Serial | `WIGLE` console output byte-compared before/after the header extraction |
| Host suite and render regression still green | `make -s test`, `make -s regress` |
| Firmware builds | `pio run -e nm-cyd-c5` |
| **Card mounts, file is valid WiGLE** | **hardware: upload to WiGLE, or diff a card capture against a serial capture of the same table** |
| **A flush does not corrupt panel or touch** | **hardware: observe a flush mid-session** |
| **A pulled-power mid-flush leaves a valid file** | **hardware: cut power during a write, confirm `<name>.csv` still parses** |

The last three are the real acceptance tests and all need a board. Nothing in
this feature may be called verified on hardware until they pass.

## Out of scope

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

## End of a drive: the card file is the finished export

There is no finalize step, and that is the point of the temp-then-rename write.
At every instant the card holds `<name>.csv` as a **complete, valid,
uploadable** document — the last full flush — never a partial one. When the
drive ends: power off, pull the card, upload the `.csv` to WiGLE. Nothing has
to be "closed" or "exported" first.

The one edge is the last interval: powering off between flushes loses up to
`FLUSH_MS` (≤30 s) of the most recent captures from the card, which is the
accepted cost of the continuous model. For a long drive that is negligible; the
file is otherwise current to within 30 seconds the whole way.

If a zero-loss clean stop is wanted later, the cheapest form is a `FLUSH`
console command mirroring `WIGLE` (one forced flush, no UI), or a flush on a
deliberate shutdown. Both are small follow-ups and neither is required for the
card file to be a valid export at any power-off.
