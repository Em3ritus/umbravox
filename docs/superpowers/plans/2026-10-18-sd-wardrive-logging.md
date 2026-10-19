# SD Wardrive Logging Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Write the platform's already-working WiGLE export to an SD card as a driver-controlled start/stop session, crash-safely, with the host-verifiable parts landing now and the card hardware staged for when a board is available.

**Architecture:** The WiGLE document (preamble + header + `cap::exportRows` + `blescan::exportRows`) already exists and runs over Serial via the `WIGLE` console command. This extracts it into a shared `writeWigle(Print&)`, adds a pure `logging` session state machine (start/stop/toggle/timer/auto-resume, host-tested against a fake `sdlog`), a `sdlog` card peripheral (`writeAtomic` = temp-then-rename, device-only), an on-screen control whose idle appearance is unchanged so render baselines hold, and immediate NVS persistence of the active flag.

**Tech Stack:** C++17, no third-party libraries. Host sim/tests build with `make`; device firmware with PlatformIO (`~/.platformio/penv/bin/pio run -e nm-cyd-c5`, `pio` not on PATH). Framebuffer 160×120 RGB565. SD via the Arduino `SD` library over the shared SPI bus.

**Spec:** `docs/superpowers/specs/2026-10-18-sd-wardrive-logging-design.md`

## Global Constraints

- **Work on a branch off `master`.** Commit at the end of every task. Never merge to `master` — that is the user's call.
- Build flags stay `-std=c++17 -O2 -Wall -Wextra` (host) and the existing device flags. No new warnings in either build.
- **`make -C sim regress` must keep printing `regress: AETHERSCAN unchanged`, with no baseline rewrite.** The on-screen control's *idle* appearance must be byte-identical to today's footer, so none of the nine baseline renders change. A task that needs to rebaseline has changed something it should not have.
- Host tests use the existing idiom: file-scope `static int fails`, `check(const char* what, bool ok)`, `printf`, `return fails ? 1 : 0`. No framework. Add each new test to `TESTS` in `sim/Makefile`.
- `logging.{h,cpp}` must be **pure** — no `<Arduino.h>`, no `SD.h`, no device headers — so it compiles and tests on the host. It reaches the card only through the `sdlog.h` interface, which the test fakes.
- Never edit a `pal::` call site in the nine `ui/screen_*.cpp` files.
- The device cannot be flashed or measured in this plan. `pio run` compiling is the device bar; anything needing a card, a bus, or power-cycling is the deferred Hardware Acceptance section, not a task here.

## Review Focus

Failure modes the spec implies that a task's happy path will not exercise. Each gets its test in the owning task.

1. **A `writeAtomic` whose write fails partway must leave the previous good file intact** — the whole point of temp-then-rename. No card can prove this on the host, but the *logic* (write temp → only rename on success → never touch the live file on failure) is reviewable in Task 3, and the hardware pulled-power test pins it on a board. → Task 3 + Hardware Acceptance.
2. **A `stop` must force exactly one final flush, not zero and not a storm** — a driver who stops and powers off must get the last interval. → Task 4, `test_logging`.
3. **`begin(wasActive=true)` must resume into a *new* file, and `begin(false)` must not log at all** — a stale resume flag that logs without a press, or a lost flag that silently ends a drive, are both the bug. → Task 4, `test_logging`.
4. **`NO_CARD` must be enterable and leavable** — start with no card, insert one, logging begins; the control must never show a silent dead state. → Task 4, `test_logging`.
5. **The control's idle rendering must not move any baseline pixel** — a REC indicator that changes the footer when not logging silently rebaselines nine screens and hides future regressions. → Task 6, proven by `regress` green with no rebaseline.

---

## File Structure

**Created:**
- `sim/test_csv.cpp` — host test for `csv.h` formatters.
- `device/wigle.h` — `wigleHeader(Print&)` (pure, host-testable) and `writeWigle(Print&, uint32_t&)` (device glue over the two exporters).
- `sim/test_wigle.cpp` — host test for `wigleHeader`'s exact bytes.
- `device/sdlog.h`, `device/sdlog.cpp` — the card peripheral: `begin`, `present`, `writeAtomic`.
- `device/logging.h`, `device/logging.cpp` — the pure session state machine.
- `sim/test_logging.cpp` — host test for `logging` against a fake `sdlog`.

**Modified:**
- `device/main.cpp` — `WIGLE` command calls `writeWigle`; `logging::begin/tick` wired into `setup`/`loop`; `s.sdOk`/`s.logName` from the real state; the `body` pointer injected.
- `ui/model.h` — `State` gains a logging-indicator field (not in `Settings`).
- `ui/screen_scan.cpp` — `drawFooter` renders the indicator; idle appearance unchanged.
- `ui/layout.h` — the control's hit region.
- `ui/nav.cpp` — tap on the control calls `logging::toggle` (via a seam, since `nav` is pure UI and `logging` is device).
- `device/prefs.cpp`, `device/prefs.h` — `setLogging(bool)` immediate write, `loggingWas()` read.
- `sim/Makefile` — `test_csv`, `test_wigle`, `test_logging` rules and `TESTS`.
- `sim/test_nav.cpp` — tap routing for the control.

---

### Task 1: `test_csv` — pin the WiGLE formatters

`csv.h` is pure and currently untested, and a silent format bug there costs a whole drive's upload. This task adds a test and changes no production code.

**Files:**
- Create: `sim/test_csv.cpp`
- Modify: `sim/Makefile`

**Interfaces:**
- Consumes: `csv::deg7`, `csv::quote`, `csv::stampUtc`, `csv::mac` from `device/csv.h`.
- Produces: nothing; a test only.

- [ ] **Step 1: Write the test**

Create `sim/test_csv.cpp`:

```cpp
// Host test for the WiGLE CSV formatters. These are pure and format-critical:
// a silent error here is a drive that will not upload, so they are pinned even
// though the code is already written.
#include "../device/csv.h"
#include "../ui/gnss.h"     // toEpoch, so the timestamp test is a round-trip
#include <stdio.h>
#include <string.h>

static int fails = 0;
static void check(const char* what, bool ok) {
    printf(ok ? "  ok   %s\n" : "  FAIL %s\n", what);
    if (!ok) fails++;
}
static bool eq(const char* a, const char* b) { return strcmp(a, b) == 0; }

int main() {
    char out[64];

    printf("deg7: fixed-point degrees, 7 places:\n");
    csv::deg7(out, 377749000);   check("37.7749 positive", eq(out, "37.7749000"));
    csv::deg7(out, -1224194000); check("-122.4194 negative", eq(out, "-122.4194000"));
    csv::deg7(out, 0);           check("zero", eq(out, "0.0000000"));
    csv::deg7(out, 5);           check("tiny fraction keeps leading zeros", eq(out, "0.0000005"));

    printf("quote: RFC 4180:\n");
    csv::quote("plain", out, sizeof out);        check("plain still quoted", eq(out, "\"plain\""));
    csv::quote("a,b", out, sizeof out);          check("comma", eq(out, "\"a,b\""));
    csv::quote("say \"hi\"", out, sizeof out);   check("inner quotes doubled", eq(out, "\"say \"\"hi\"\"\""));
    csv::quote("", out, sizeof out);             check("empty", eq(out, "\"\""));

    printf("stampUtc: round-trips with the project's own toEpoch:\n");
    // No magic epoch literal: toEpoch builds it, stampUtc must read it back.
    // This also pins that the two converters agree, which the filename (Task 4)
    // and firstEpoch both rely on.
    csv::stampUtc(out, ui::Gnss::toEpoch(2026, 10, 18, 14, 23, 45));
    check("2026-10-18 14:23:45 round-trips", eq(out, "2026-10-18 14:23:45"));
    csv::stampUtc(out, ui::Gnss::toEpoch(2000, 1, 1, 0, 0, 0));
    check("millennium midnight", eq(out, "2000-01-01 00:00:00"));
    csv::stampUtc(out, 0u);
    check("epoch 0 is 1970", eq(out, "1970-01-01 00:00:00"));

    printf("mac:\n");
    const uint8_t m[6] = {0x00,0x11,0x22,0xAB,0xCD,0xEF};
    csv::mac(out, m);  check("uppercase colon-separated", eq(out, "00:11:22:AB:CD:EF"));

    printf(fails ? "\n%d FAILED\n" : "\nall passed\n", fails);
    return fails ? 1 : 0;
}
```

Testing `stampUtc(toEpoch(...))` as a round-trip means the test carries no
hand-computed epoch to get wrong, and it pins that the two converters agree.

- [ ] **Step 2: Add the rule and run it to verify it passes**

In `sim/Makefile`, add beside the other test rules (it links `gnss.cpp` for
`toEpoch`):

```make
test_csv: test_csv.cpp ../device/csv.h ../ui/gnss.cpp ../ui/gnss.h
	$(CXX) $(CXXFLAGS) -o $@ test_csv.cpp ../ui/gnss.cpp -lm
```

and append ` test_csv` to `TESTS`.

Run: `cd sim && make -s test`
Expected: seven `== test_*` blocks, each ending `all passed`, exit 0. If `test_csv` fails, you have found a real pre-existing format bug — report it rather than editing the expectation to match the bug.

- [ ] **Step 3: Verify regression gate unaffected**

Run: `make -s regress`
Expected: `regress: AETHERSCAN unchanged`.

- [ ] **Step 4: Commit**

```bash
git add -A
git commit -m "test: pin the WiGLE CSV formatters"
```

---

### Task 2: Extract the shared WiGLE stream

The preamble and column header live inline in the `WIGLE` console command. The SD writer needs identical bytes. Extract them so there is one source, and host-test the pure framing.

**Files:**
- Create: `device/wigle.h`, `sim/test_wigle.cpp`
- Modify: `device/main.cpp`, `sim/Makefile`

**Interfaces:**
- Consumes: `cap::exportRows`, `blescan::exportRows` (device-only); `Print`.
- Produces: `void ui_wigle::header(Print&)` (pure) and `uint32_t ui_wigle::writeDoc(Print&, uint32_t& skipped)` (device glue).

- [ ] **Step 1: Write the failing header test**

Create `sim/test_wigle.cpp`. It tests only the pure `header()`, with a tiny host `Print` shim (the Arduino `Print` base is not on the host, so the pure header function takes a minimal sink):

```cpp
// Host test for the WiGLE document framing. Only the preamble and column
// header are pure and testable here; the rows come from device-only exporters
// and are exercised by the WIGLE console command on hardware.
#include "../device/wigle.h"
#include <stdio.h>
#include <string.h>

static int fails = 0;
static void check(const char* what, bool ok) {
    printf(ok ? "  ok   %s\n" : "  FAIL %s\n", what);
    if (!ok) fails++;
}

// The sink wigle::header writes into. On device this is backed by Print; here
// by a growing buffer. See the Sink definition in wigle.h.
struct Buf : ui_wigle::Sink {
    char s[512]; int n = 0;
    void put(const char* t) override { for (int i = 0; t[i] && n < 511; i++) s[n++] = t[i]; s[n] = 0; }
};

int main() {
    Buf b;
    ui_wigle::header(b);
    const char* want =
        "WigleWifi-1.6,appRelease=aetherscan,model=NM-CYD-C5,release=1,"
        "device=esp32c5,display=,board=,brand=AETHERSCAN\n"
        "MAC,SSID,AuthMode,FirstSeen,Channel,RSSI,CurrentLatitude,"
        "CurrentLongitude,AltitudeMeters,AccuracyMeters,Type\n";
    check("preamble and column header are exact WigleWifi-1.6", strcmp(b.s, want) == 0);

    printf(fails ? "\n%d FAILED\n" : "\nall passed\n", fails);
    return fails ? 1 : 0;
}
```

- [ ] **Step 2: Run it to verify it fails**

Run: `cd sim && make test_wigle` (after adding the rule in Step 4, or compile directly)
Expected: FAIL to compile — `device/wigle.h` does not exist yet.

- [ ] **Step 3: Write `device/wigle.h`**

```cpp
// The WigleWifi-1.6 document, in one place, so Serial and the SD file emit the
// same bytes. capture.cpp:726 states the rule: main owns the markers and the
// header so the two exporters cannot disagree about them.
#pragma once
#include <stdint.h>

namespace ui_wigle {

// A line sink. On the host a test buffer; on the device a thin wrapper over
// Print (see writeDoc). Keeping header() free of Print makes it host-testable.
struct Sink { virtual void put(const char* s) = 0; };

// The preamble line and the column header. Pure; no exporters.
inline void header(Sink& out) {
    out.put("WigleWifi-1.6,appRelease=aetherscan,model=NM-CYD-C5,release=1,"
            "device=esp32c5,display=,board=,brand=AETHERSCAN\n");
    out.put("MAC,SSID,AuthMode,FirstSeen,Channel,RSSI,CurrentLatitude,"
            "CurrentLongitude,AltitudeMeters,AccuracyMeters,Type\n");
}

} // namespace ui_wigle
```

Then the device-only glue, in `device/wigle.h` guarded so the host test does not pull in `Print`/exporters. Put `writeDoc` in a `#if` that is true only on the device build (e.g. `#ifdef ARDUINO`), or in a separate `device/wigle_doc.h` included only by `main.cpp`. Choose the separate-header route to keep `wigle.h` host-clean:

Create the glue inline in `main.cpp` instead (it is the only caller), as `writeWigle`:

```cpp
// Bridges the pure header sink to a Print, then appends both exporters.
struct PrintSink : ui_wigle::Sink {
    Print& p;
    PrintSink(Print& pp) : p(pp) {}
    void put(const char* s) override { p.print(s); }
};

static uint32_t writeWigle(Print& out, uint32_t& skipped) {
    PrintSink sink(out);
    ui_wigle::header(sink);
    const uint32_t w = cap::exportRows(out, skipped);
    const uint32_t b = blescan::exportRows(out, skipped);
    return w + b;
}
```

- [ ] **Step 4: Point the `WIGLE` command at it**

In `main.cpp`'s `pollConsole`, replace the inline preamble/header/exporter block (`main.cpp:187-200`) with:

```cpp
                Serial.println("=== WIGLE BEGIN ===");
                uint32_t skipped = 0;
                const uint32_t n = writeWigle(Serial, skipped);
                logf("=== WIGLE END === %lu rows, %lu skipped "
                     "(no position or no UTC)\n",
                     (unsigned long)n, (unsigned long)skipped);
```

The emitted bytes between BEGIN and END must be identical to before — same header, same exporters in the same order. This is a pure extraction.

- [ ] **Step 5: Add the test rule, run host suite**

In `sim/Makefile`:

```make
test_wigle: test_wigle.cpp ../device/wigle.h
	$(CXX) $(CXXFLAGS) -o $@ test_wigle.cpp -lm
```

append ` test_wigle` to `TESTS`.

Run: `cd sim && make -s test`
Expected: eight tests, all `all passed`.

- [ ] **Step 6: Device build**

Run: `~/.platformio/penv/bin/pio run -e nm-cyd-c5`
Expected: compiles, no new warnings.

- [ ] **Step 7: Verify regress, commit**

Run: `make -s regress` → `regress: AETHERSCAN unchanged`.

```bash
git add -A
git commit -m "refactor: share the WiGLE document between Serial and the SD writer"
```

---

### Task 3: `sdlog` — the card peripheral (device-build-only)

The crash-safe write. It compiles against `SD.h`; its real behaviour is a Hardware Acceptance item. The logic (temp → rename only on success → never touch the live file on failure) is reviewable now.

**Files:**
- Create: `device/sdlog.h`, `device/sdlog.cpp`

**Interfaces:**
- Consumes: `SD.h`, the shared SPI bus, the SD CS pin (a `constexpr` flagged for board confirmation).
- Produces: `bool sdlog::begin()`, `bool sdlog::present()`, `bool sdlog::writeAtomic(const char* name, uint32_t (*body)(Print&, uint32_t&), uint32_t& rows, uint32_t& skipped)`.

- [ ] **Step 1: Write `device/sdlog.h`**

```cpp
// The SD card: mount, and write a document crash-safely. Knows nothing of
// WiGLE -- it only puts a document on the card without a half-written file
// surviving. logging owns the session name and the bookkeeping.
#pragma once
#include <stdint.h>
class Print;

namespace sdlog {

bool begin();     // mount on the shared SPI bus; false and stays false if none
bool present();   // a card is mounted

// Write `body` to `name`, atomically: open "name.tmp", run body into it, flush,
// close, rename over "name". Returns true only if the rename succeeded, so a
// failure anywhere leaves the previous good "name" untouched.
bool writeAtomic(const char* name,
                 uint32_t (*body)(Print&, uint32_t&),
                 uint32_t& rows, uint32_t& skipped);

} // namespace sdlog
```

- [ ] **Step 2: Write `device/sdlog.cpp`**

```cpp
#include "sdlog.h"
#include <Arduino.h>
#include <SPI.h>
#include <SD.h>

namespace sdlog {
namespace {
bool mounted = false;

// CONFIRM AGAINST THE BOARD. The NM-CYD-C5's SD chip-select is not documented
// publicly; PIN_CS (23) is the LCD's, not this. SCK/MOSI/MISO (6/7/2) are the
// shared bus. This value is a placeholder to be set from the schematic before
// the card is expected to mount -- see the Hardware Acceptance section.
constexpr int SD_CS = -1;
}

bool begin() {
    // The panel and touch already own the SPI bus; SD shares it. The exact
    // handoff (SPI.begin pins, SD.begin(cs, SPI)) is settled on hardware.
    mounted = (SD_CS >= 0) && SD.begin(SD_CS, SPI);
    return mounted;
}

bool present() { return mounted; }

bool writeAtomic(const char* name,
                 uint32_t (*body)(Print&, uint32_t&),
                 uint32_t& rows, uint32_t& skipped) {
    if (!mounted) return false;
    char tmp[32];
    snprintf(tmp, sizeof tmp, "%s.tmp", name);

    SD.remove(tmp);
    File f = SD.open(tmp, FILE_WRITE);
    if (!f) return false;

    rows = body(f, skipped);
    f.flush();
    f.close();

    // Rename only after a clean close. SD.rename replaces the directory entry,
    // so "name" is either the old complete file or the new one, never a
    // fragment. Remove the old target first; FAT rename will not overwrite.
    SD.remove(name);
    if (!SD.rename(tmp, name)) { SD.remove(tmp); return false; }
    return true;
}

} // namespace sdlog
```

- [ ] **Step 3: Device build**

Run: `~/.platformio/penv/bin/pio run -e nm-cyd-c5`
Expected: compiles, no new warnings. (It will not mount anything — `SD_CS` is -1 until the board is known. That is intended; `begin()` returns false and `present()` stays false, so nothing downstream writes.)

- [ ] **Step 4: Commit**

```bash
git add -A
git commit -m "feat: sdlog card peripheral with crash-safe atomic write (untested until hardware)"
```

---

### Task 4: `logging` — the session state machine (host-tested)

The heart of the feature, and fully host-verifiable. Pure: it reaches the card only through `sdlog.h`, which the test fakes.

**Files:**
- Create: `device/logging.h`, `device/logging.cpp`, `sim/test_logging.cpp`
- Modify: `sim/Makefile`

**Interfaces:**
- Consumes: `sdlog::present`, `sdlog::writeAtomic` (faked in the test); a session-name builder.
- Produces: `logging::begin(bool wasActive, Body body)`, `toggle`, `start`, `stop`, `tick(nowMs, nowEpoch)`, `State state()`, `sessionSecs(nowMs)`, and `bool wantsActive()` (for persistence).

- [ ] **Step 1: Write `device/logging.h`**

```cpp
// The wardrive logging session: start/stop/toggle, the flush timer, and
// auto-resume after a reboot. Pure -- no Arduino, no SD -- so it tests on the
// host against a fake sdlog. It reaches the card only through sdlog.h.
#pragma once
#include <stdint.h>

namespace logging {

enum State : uint8_t { IDLE, ACTIVE, NO_CARD };

// The document writer (main injects writeWigle). Kept as a pointer so logging
// carries no dependency on the exporters or Print.
typedef uint32_t (*Body)(class Print&, uint32_t&);

constexpr uint32_t FLUSH_MS = 30000;

// At boot: resume if we died logging. `body` is the writer; `nowEpoch` seeds
// the session filename when a date is already known.
void begin(bool wasActive, Body body, uint32_t nowMs, uint32_t nowEpoch);

void toggle(uint32_t nowMs, uint32_t nowEpoch);
void start(uint32_t nowMs, uint32_t nowEpoch);
void stop(uint32_t nowMs);

// Every loop. Flushes at FLUSH_MS while ACTIVE; promotes NO_CARD to ACTIVE
// when a card appears.
void tick(uint32_t nowMs, uint32_t nowEpoch);

State       state();
bool        wantsActive();   // the flag to persist: true from start to stop
uint32_t    sessionSecs(uint32_t nowMs);
const char* fileName();      // current session file, "" when idle
uint32_t    lastRows();
uint32_t    lastSkipped();

} // namespace logging
```

- [ ] **Step 2: Write the failing test with a fake `sdlog`**

Create `sim/test_logging.cpp`:

```cpp
// Host test for the logging state machine. sdlog is faked: it records calls
// instead of touching a card, so start/stop/timer/resume are pinned without
// hardware.
#include "../device/logging.h"
#include "../device/sdlog.h"
#include <stdio.h>
#include <string.h>

// --- fake sdlog -----------------------------------------------------------
namespace { bool cardPresent = true; int writes = 0; char lastName[32]; }
namespace sdlog {
bool begin() { return cardPresent; }
bool present() { return cardPresent; }
bool writeAtomic(const char* name, uint32_t (*)(class Print&, uint32_t&),
                 uint32_t& rows, uint32_t& skipped) {
    if (!cardPresent) return false;
    writes++; strncpy(lastName, name, sizeof lastName - 1);
    rows = 7; skipped = 1; return true;
}
}

static uint32_t fakeBody(class Print&, uint32_t&) { return 0; }

static int fails = 0;
static void check(const char* what, bool ok) {
    printf(ok ? "  ok   %s\n" : "  FAIL %s\n", what);
    if (!ok) fails++;
}

int main() {
    using namespace logging;

    printf("start opens a session and arms the timer:\n");
    cardPresent = true; writes = 0;
    begin(false, fakeBody, 0, 1792972800u);
    check("begins idle", state() == IDLE);
    start(1000, 1792972800u);
    check("now active", state() == ACTIVE);
    check("start wrote once (opened the file)", writes == 1);

    printf("the timer flushes at FLUSH_MS and not before:\n");
    writes = 0;
    tick(1000 + FLUSH_MS - 1, 1792972800u);
    check("no flush before the interval", writes == 0);
    tick(1000 + FLUSH_MS, 1792972800u);
    check("one flush at the interval", writes == 1);

    printf("stop forces exactly one final flush and closes:\n");
    writes = 0;
    stop(1000 + FLUSH_MS + 5);
    check("stop flushed once", writes == 1);
    check("now idle", state() == IDLE);
    check("file name cleared", fileName()[0] == 0);

    printf("toggle alternates:\n");
    begin(false, fakeBody, 0, 1792972800u);
    toggle(10, 1792972800u); check("toggle -> active", state() == ACTIVE);
    toggle(20, 0);           check("toggle -> idle", state() == IDLE);

    printf("auto-resume:\n");
    begin(true, fakeBody, 0, 1792972800u);
    check("begin(wasActive) resumes active", state() == ACTIVE);
    begin(false, fakeBody, 0, 1792972800u);
    check("begin(idle) stays idle", state() == IDLE);

    printf("NO_CARD is entered and left:\n");
    cardPresent = false;
    begin(false, fakeBody, 0, 1792972800u);
    start(0, 1792972800u);
    check("start with no card -> NO_CARD", state() == NO_CARD);
    check("wantsActive is true despite no card", wantsActive());
    cardPresent = true;
    tick(100, 1792972800u);
    check("card appears -> ACTIVE", state() == ACTIVE);

    printf("resume uses a new file each time:\n");
    begin(true, fakeBody, 0, 1792972800u);
    char first[32]; strncpy(first, fileName(), sizeof first);
    stop(50);
    begin(true, fakeBody, 60, 1792972800u + 60);
    check("a later session names a different file", strcmp(first, fileName()) != 0);

    printf(fails ? "\n%d FAILED\n" : "\nall passed\n", fails);
    return fails ? 1 : 0;
}
```

- [ ] **Step 3: Run it to verify it fails**

Run: `cd sim && make test_logging` (after the Step 5 rule)
Expected: FAIL to compile — `logging.cpp` not implemented.

- [ ] **Step 4: Implement `device/logging.cpp`**

Pure C++. State: `State st`, `Body body`, `uint32_t startedMs`, `uint32_t nextFlushMs`, `char name[32]`, `uint32_t rows, skipped`, `bool want`. Key behaviours, matching the test:
- `start`: set `want=true`; build the filename from `nowEpoch` (date → `WD-YYYYMMDD-HHMMSS.CSV`, else a `WD-NNNN.CSV` counter — reuse `csv::stampUtc` arithmetic or a small local formatter; no `located` needed); if `sdlog::present()` flush immediately (opens the file) and go `ACTIVE`, else `NO_CARD`; set `nextFlushMs = nowMs + FLUSH_MS`.
- `stop`: if `ACTIVE`, one `flush` now; `want=false`; `st=IDLE`; clear `name`.
- `toggle`: `IDLE`/`NO_CARD` → `start`; `ACTIVE` → `stop`.
- `tick`: if `NO_CARD` and `sdlog::present()` → open the file, `ACTIVE`; if `ACTIVE` and `nowMs >= nextFlushMs` → `flush`, `nextFlushMs += FLUSH_MS`.
- `begin(wasActive, ...)`: store `body`; if `wasActive` → `start` (new file); else `IDLE`.
- `flush` helper: `sdlog::writeAtomic(name, body, rows, skipped)`; if it returns false and the card is gone, drop to `NO_CARD`.
- `wantsActive` returns `want`; `sessionSecs` is `(nowMs - startedMs)/1000`.

Do not include any Arduino or SD header. `Print` is only named as an opaque type in the `Body` signature.

- [ ] **Step 5: Add the rule, run, verify it passes**

```make
test_logging: test_logging.cpp ../device/logging.cpp ../device/logging.h ../device/sdlog.h
	$(CXX) $(CXXFLAGS) -o $@ test_logging.cpp ../device/logging.cpp -lm
```

append ` test_logging` to `TESTS`.

Run: `cd sim && make -s test`
Expected: nine tests, all `all passed`.

- [ ] **Step 6: Device build, regress, commit**

Run: `~/.platformio/penv/bin/pio run -e nm-cyd-c5` → compiles.
Run: `make -s regress` → `AETHERSCAN unchanged`.

```bash
git add -A
git commit -m "feat: wardrive logging session state machine, host-tested"
```

---

### Task 5: Persist the active flag immediately

Auto-resume needs the flag on flash the instant it changes, not on the debounced settings timer.

**Files:**
- Modify: `device/prefs.h`, `device/prefs.cpp`

**Interfaces:**
- Produces: `void prefs::setLogging(bool)` (immediate write), `bool prefs::loggingWas()`.

- [ ] **Step 1: Declare and implement**

In `prefs.h`, add `void setLogging(bool on);` and `bool loggingWas();`.

In `prefs.cpp`, using the already-open NVS handle:

```cpp
void setLogging(bool on) {
    if (!ready) return;
    nvs.putUChar("logging", on ? 1 : 0);   // immediate: a brownout one second
                                           // after start must still resume
}
bool loggingWas() {
    return ready && nvs.getUChar("logging", 0) != 0;
}
```

This is a separate key, outside the `Settings` struct and its debounced round-trip and its `settingsEqual`/`static_assert` — logging is session state, not a knob, and `VER` does not change.

- [ ] **Step 2: Device build, commit**

Run: `~/.platformio/penv/bin/pio run -e nm-cyd-c5` → compiles.
Run: `cd sim && make -s test && make -s regress` → all green (`prefs.cpp` is not in the host build; this just confirms nothing else broke).

```bash
git add -A
git commit -m "feat: persist the logging active flag immediately in NVS"
```

---

### Task 6: The on-screen control and indicator

The control. Its **idle appearance must equal today's footer** so no baseline moves.

**Files:**
- Modify: `ui/model.h`, `ui/screen_scan.cpp`, `ui/layout.h`, `ui/nav.cpp`, `sim/test_nav.cpp`

**Interfaces:**
- Consumes: a logging-state field on `State`.
- Produces: `ui::LOG_X/LOG_Y/LOG_W/LOG_H` hit region; a `State::logState` field; a nav seam that reports a tap on the control.

- [ ] **Step 1: Add the state field**

In `ui/model.h`, on `State` (not `Settings`): `uint8_t logState = 0;  // 0 idle, 1 active, 2 no-card` and `uint32_t logSecs = 0;`. `demoState` leaves `logState = 0` (idle), so the sim renders the idle appearance and baselines do not move.

- [ ] **Step 2: Render it in `drawFooter`, idle unchanged**

In `ui/screen_scan.cpp`'s `drawFooter`, the SD status at x≈64-158 currently draws `s.sdOk ? s.logName : "NO CARD"` with a status rune. Extend it so:
- `logState == 0` (idle): **exactly today's drawing** — do not change a pixel. The `● REC`/`○ LOG` affordance may share the rune already there, but its idle pixels must be identical. Verify by `regress`.
- `logState == 1` (active): a filled indicator and `REC` with `s.logSecs` as `mm:ss`, in `pal::RED` or `pal::GREEN` per the project's status convention.
- `logState == 2` (no-card): the "NO CARD" text struck or dimmed.

Keep the idle path byte-identical; branch only for 1 and 2.

- [ ] **Step 3: Hit region and nav seam**

In `ui/layout.h`, add the control's rectangle (over the footer SD status area) as `LOG_X/LOG_Y/LOG_W/LOG_H`.

`ui/nav.cpp` is pure UI and must not call device `logging`. Add to `Nav` a `bool logToggleRequested` flag (like `resetRequested`), set when the control is tapped on the scan screen; `main` acts on it and clears it. In `onTap`, on `SCR_SCAN`, a tap in the `LOG_*` region sets `n.logToggleRequested = true` and returns false (same screen).

- [ ] **Step 4: Pin the tap routing**

In `sim/test_nav.cpp`, add: a tap in the `LOG_*` region on `SCR_SCAN` sets `logToggleRequested`; a tap elsewhere does not. Follow the file's idiom and `tapAt` helper.

- [ ] **Step 5: Verify idle baselines did NOT move**

Run: `cd sim && make -s regress`
Expected: `regress: AETHERSCAN unchanged`, **no rebaseline**. If any screen differs, the idle footer drawing changed — fix it, do not rebaseline.

- [ ] **Step 6: See the active/no-card states**

Render with `logState` forced (a temporary line in `main_sim.cpp`, or a `--log` flag): confirm `● REC 00:42` and the struck `NO CARD` read correctly. Revert any temporary render hook.

Run: `make -s test` → ten tests pass.

- [ ] **Step 7: Commit**

```bash
git add -A
git commit -m "feat: on-screen start/stop control and REC indicator"
```

---

### Task 7: Wire it together in `main`

**Files:**
- Modify: `device/main.cpp`

**Interfaces:**
- Consumes: everything above.
- Produces: a working device build where the control drives logging and the footer reflects it.

- [ ] **Step 1: Boot**

In `setup`: `sdlog::begin();` then `logging::begin(prefs::loggingWas(), writeWigle, millis(), ui::Gnss::utcEpoch(millis()));`.

- [ ] **Step 2: Loop**

In `loop`, each frame: `logging::tick(millis(), ui::Gnss::utcEpoch(millis()));`. After `navTouch`, if `nav.logToggleRequested` → `logging::toggle(millis(), ui::Gnss::utcEpoch(millis())); prefs::setLogging(logging::wantsActive()); nav.logToggleRequested = false;`.

- [ ] **Step 3: Publish state for the footer**

Before drawing: `s.sdOk = sdlog::present();` (replaces `main.cpp:276`'s hardcoded false); `s.logState = (uint8_t)logging::state();`; `s.logSecs = logging::sessionSecs(millis());`; `strncpy(s.logName, logging::fileName(), sizeof s.logName - 1);`.

- [ ] **Step 4: Full-repaint on state change**

A `logState` change must mark the footer dirty (the footer is shared chrome; `dirtyChrome` already compares fields — add `logState`/`logSecs` to its comparison, or mark the footer rect when they change), so the indicator updates without a full redraw.

- [ ] **Step 5: Device build, host suite, regress**

Run: `~/.platformio/penv/bin/pio run -e nm-cyd-c5` → compiles, report RAM/flash.
Run: `cd sim && make -s test && make -s regress` → all green.

- [ ] **Step 6: Commit**

```bash
git add -A
git commit -m "feat: wire start/stop logging into the device loop and footer"
```

---

## Hardware Acceptance

Checklist to run on the NM-CYD-C5 with a card. **Verification run 2026-10-19 on the real board** (via serial reset + SDLS/SDCAT/GPSFIX bench commands) — status marked per item.

1. ✅ **Set the SD CS pin.** `SD_CS = 10` (RockBase schematic, README). Confirmed: card mounts (`sdlog: card mounted on CS 10, 29818 MB`), and the panel (40 MHz) and touch init and run normally alongside it on the shared bus.
2. ✅ **Valid WiGLE file.** Confirmed via a canned fix (`GPSFIX`) and a real-fix session: geolocated `WIFI`/`BLE` rows with coordinates, auth strings and `FirstSeen` land on the card in valid WigleWifi-1.6 format (`SDCAT`). A real-sky upload still wants one stable outdoor fix during a session (not code-gated).
3. ✅ **A flush does not corrupt the panel or touch.** The panel rendered continuously (normal 6–8 ms draw / 13–18 ms blit) through SD mounts, writes and flushes; touch kept working.
4. ✅ **Stop leaves a complete file; pulled power mid-flush leaves a valid file.** Confirmed: power pulled mid-session, and after replug every `.CSV` on the card parses as a complete WigleWifi document with no `.tmp`/`.bak` debris. (A random pull lands between flushes — the realistic brownout case; the mid-rename corner is covered by backup-rename.) Review Focus 1.
5. ✅ **Auto-resume.** Confirmed: with the logging flag set, a reboot comes up `ACTIVE` and writes a fresh session file with no tap (observed `WD-20261019-134319.CSV` created at boot). Review Focus 3.
6. ✅ **Dateless resume must not overwrite.** IMPLEMENTED (no longer deferred): `logging::begin` seeds the counter from `sdlog::maxIndex("/WD-", ".CSV")` and advances past existing files. Host-tested (`names past WD-0007 -> WD-0008`). Spec §7 satisfied.
7. ⏳ **Record the measurements.** RAM 34.7% / Flash 51.0% recorded. Flush duration (the capture stall) still to be measured on a real drive.

---

## Self-review

**Spec coverage.** Shared `writeWigle` → Task 2. `sdlog` + crash-safe write → Task 3 (logic) + Hardware Acceptance (card). `logging` state machine, start/stop/toggle/timer/resume/NO_CARD/filename → Task 4. Control + indicator, idle unchanged → Task 6. `s.sdOk`/status → Tasks 6–7. Immediate NVS persistence → Task 5. `csv.h` test → Task 1. Card-only timer, `WIGLE` kept → Task 2 (command retained) + Task 7 (timer writes via sdlog only). Serial/end-of-drive → covered by the above. Physical button out of scope → the input-agnostic `toggle` in Task 4 leaves it a drop-in.

**Placeholder scan.** `SD_CS = -1` is a deliberate, documented hardware placeholder, not a plan gap — Task 3 and Hardware Acceptance both call it out. No TBD/TODO elsewhere.

**Type consistency.** `logging::Body` is `uint32_t(*)(Print&, uint32_t&)` — matches `writeWigle`'s signature in Task 2 and the `sdlog::writeAtomic` body parameter in Task 3. `State::logState` is `uint8_t` mirroring `logging::State`'s three values. `sdlog::writeAtomic` has one signature across Tasks 3, 4 (fake), and 7.

**Review Focus coverage.** (1) atomic write integrity → Task 3 logic + Hardware Acceptance 2/4. (2) stop forces one flush → Task 4 `test_logging`. (3) resume into a new file / idle stays idle → Task 4 `test_logging`. (4) NO_CARD enter/leave → Task 4 `test_logging`. (5) idle rendering moves no baseline → Task 6, `regress` green with no rebaseline.
