# Speed meter (plan 5c) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** The designer shows, for each board, the worst single-frame time of the scan screen and of the worst alert
level for the layout being edited, predicted from pixels drawn and pixels pushed with a fit measured on both real
boards. A layout over its limit cannot be installed, downloaded or shared.

**Architecture:** The engine builds compose only dirty regions (`scanpres::Engine` regions / window / slices) and
push only what changed (`framepresent::Frame::present`). Today that slice loop lives in `device/engine_adapter.cpp`,
so it moves into `ui/scan_frame.h` (shared). Both the board and a new host replay (`ui/meter.h`, exported to wasm as
`uv_meter`) run it, and both count drawn and pushed pixels the same way. On the board a console command `FRAMELOG`
logs one line per finished frame. `tools/meterprobe.py` collects those lines for a set of calibration packs,
`tools/meterfit.py` fits `ms ≈ a·drawn + b·pushed + c` per board into `web/designer/js/meter-fit.json`, and the
designer's `js/meter.js` turns the wasm counts plus that fit into the meter and the gate.

**Tech Stack:** C++ (ui/, sim host tests, ESP32-C5 firmware), emscripten bridge, Python 3 (pyserial via PlatformIO's
python; `unittest` in `tools/tests/`), plain ES modules with `node:test`.

**Spec:** `docs/superpowers/specs/2026-11-18-designer-alerts-design.md` ("5c design"), parent
`docs/superpowers/specs/2026-11-17-ui-overhaul-design.md` ("Speed budget").

## Global Constraints

> **Ruling (owner, 2026-11-18, after calibration):** one fit over every frame does not work (alert pixels cost
> 2-2.5x scan pixels; the fit came out with a negative slope). Now: separate scan and alert fits per board, judged
> on the p95 frame; scan limit = the built-in theme's PREDICTED scan p95 + 15% (`tools/meter-limits.mjs`); alerts
> 400 ms, but the alert row only warns (amber), never blocks; the cross-check compares predicted with measured p95
> times (scan within 15%), not pixel counts. The 250 ms / 14 ms / worst-frame rules below are superseded. Details:
> `docs/design-notes.md` "Speed meter".

- Limits are judged on the worst single frame: alerts, both boards, <= 250 ms. Scan on the CYD <= 14 ms. Scan on
  the Waveshare = the built-in theme's calibrated worst frame + 15%. Amber from 80% of a limit. Over 100% blocks
  Install, Download and Share. These are starting values: the calibration task may change them, and the limits it
  settles on go in `meter-fit.json`, never into JS constants.
- One set of rules: the counts come from the device's own code (`ui/scan_frame.h`) in wasm. The fit and the limits
  come from `meter-fit.json`. Neither is retyped in JS.
- The shared slice loop must not change what the board draws: `make -C sim regress` prints "UMBRAVOX unchanged
  (landscape and portrait)", and the firmware frame time does not rise (Waveshare engine build compose+xfer within
  5% of today's; CYD `nm-cyd-c5` median scan draw ~12 ms, investigate past 14 ms).
- `FRAMELOG` is off by default and costs nothing when off (one branch per finished frame).
- Internal RAM: no new `.bss` on the board. The `ws-c5-35-engine` `.dram0.bss` is at its cap (94720) and must not
  grow. Counters are a few `uint32_t` in an existing object.
- Commits: author `Em3ritus <152051018+Em3ritus@users.noreply.github.com>`. The ONLY trailer is
  `Co-Authored-By: Claude <noreply@anthropic.com>`.
- Tests run as `make -C sim X && ./sim/X`. From this worktree, `make -C sim regress` needs
  `MANIFEST=<main checkout>/private/baseline/MANIFEST.md5`. `npm test` rebuilds the wasm. A new bridge export must
  also be added to EXPORTS in `tools/build-designer-wasm.sh`.
- After ANY firmware change: `tools/bootprobe.py` on the exact build, on every connected board. If a board is absent,
  report NOT RUN, never pass.
- Never claim a calibration number that was not measured on the board in this session.

## Review Focus

1. **The first frame after entry is a full repaint** (both boards). It is not a layout's steady cost, so the meter
   skips the warm-up frames (frames 0-1 after entry) for scan. For alerts it keeps the alert's first frame,
   because the card appearing is a real frame the owner sees. Test in Task 2.
2. **Counts from wasm and from the board must agree for the same pack.** If they drift (different frame period,
   different demo state), the fit is applied to the wrong numbers. The replay takes the measured frame period from
   `meter-fit.json`, and Task 5's cross-check fails if any pack's worst-frame drawn or pushed differs by more than
   15%.
3. **Meter cost in the browser:** 2 boards x (scan + 3 alert levels) x N frames must not freeze the Layout step. The
   meter runs debounced after edits, and `measure` for both boards finishes in under 2 s in node. Test in Task 6.
4. **No fit for a board** (missing or older `meter-fit.json` without that board): the row says "not calibrated" and
   does not block. A broken fit (NaN, negative slope) is refused when loading, never shown as 0 ms. Test in Task 6.
5. **A built-in theme or the Standard layout must never be over the limit.** Otherwise every author is blocked. Task
   7 calibration asserts the built-in theme is under 100% on both boards, and a designer test pins it with the
   shipped fit.

---

### Task 1: Shared scan slice loop with drawn/pushed counters

**Files:**
- Create: `ui/scan_frame.h`
- Modify: `ui/frame_present.h` (count pushed area in `emit` and in the staged-region loop of `present`)
- Modify: `device/engine_adapter.cpp:151-205,369-373` (call the shared loop; delete `renderSliceT`'s scan use and
  `renderScanRegions`)
- Test: `sim/test_scan_frame.cpp` (new, added to `TESTS` and given a rule like `test_alert_takeover`)

**Interfaces:**
- Produces: `struct uimeter::Count { uint32_t drawn = 0, pushed = 0; void clear(); };`
- Produces: `framepresent::Frame::pushed() -> uint32_t`: pixels sent by the last `present()`, reset at its start.
- Produces: `template<class Stage> void scanframe::slice(const scanpres::Engine& e, const scanr::ScanScene& sc, int W,
  int H, int y0, int y1, uint16_t* buf, bool mirrorValid, Stage& stage, uimeter::Count& n)`. Stage provides
  `full(sy, buf, sh)`, `window(sy, buf, sh, lo, w)`, `region(sy, buf, sh, lo, w, id)` (the device maps these to
  `frame_.stage / stageWindow / stageRegion` or `sink_.strip`). It holds the regions -> window -> full-strip choice
  that `service()` and `renderScanRegions` make today, unchanged. `n.drawn` += composed width x strip height for
  each `composeStrip` call.

- [ ] **Step 1: Write the failing test.** `test_scan_frame.cpp` drives a `scanpres::Engine` at 480x320 and at 320x240
  with the built-in theme and `ui::demoState`, through `scanframe::slice` into a `framepresent::Frame`, for 10
  frames. Assertions: `frame0_drawn == W*H` and `frame0_pushed == W*H`. Re-presenting an unchanged scene gives
  `pushed == 0`. For a sweep-only frame, `drawn < W*H/4`. The composed back buffer equals a full `composeStrip` of
  the same scene pixel for pixel, on every frame.
- [ ] **Step 2: Run** `make -C sim test_scan_frame && ./sim/test_scan_frame`. Expected: build fails (no
  `ui/scan_frame.h`).
- [ ] **Step 3: Implement** `ui/scan_frame.h` and the `Frame::pushed()` counter, moving the logic out of
  `engine_adapter.cpp` verbatim. Then make `engine_adapter.cpp` call `scanframe::slice` with a Stage that wraps
  `frame_` / `sink_` and keeps the `sliceCompareUs_` timing around each stage call.
- [ ] **Step 4: Run** the new test (PASS), then `make -C sim test_scan_engine test_scan_present test_alert_takeover &&`
  each binary (PASS), then `make -C sim regress MANIFEST=...` ("unchanged").
- [ ] **Step 5: Build** all five envs (`nm-cyd-c5 nm-cyd-c5-engine nm-c5-node nm-c5-coordinator ws-c5-35-engine`):
  SUCCESS, with `ws-c5-35-engine` `.dram0.bss` <= 94720. Run `tools/bootprobe.py` on each connected board with the
  exact build. Note the `[eng]` compose/xfer medians as the Task 3 baseline.
- [ ] **Step 6: Commit** `refactor(ui): the scan slice loop is shared code and counts pixels drawn and pushed`.

### Task 2: Host replay `meter::run` and the `uv_meter` bridge export

**Files:**
- Create: `ui/meter.h`
- Modify: `web/designer/wasm/bridge.cpp`, `tools/build-designer-wasm.sh` (EXPORTS += `_uv_meter`)
- Test: `sim/test_meter.cpp` (new, in `TESTS`)

**Interfaces:**
- Consumes: `scanframe::slice`, `uimeter::Count`, `Frame::pushed()` (Task 1); `uiproj::scan`, `prevws`'s demo state
  setup (`ui/preview_ws.h` render(): reuse its state building, factored into `prevws::demo(theme, frame, alertStyle,
  alertConf, frameMs) -> {ui::State, uiproj::Chrome}` so the preview and the meter cannot diverge).
- Produces: `int meter::run(int theme, int w, int h, int mode, int frames, int frameMs, uimeter::Count* out)`, where
  mode is -1 = scan or 0/1/2 = alert LOW/MED/HIGH. It returns the number of frames written. Each frame is
  snapshot -> `nextSlice` until done -> `present`, as `service()` does. The alert age is `frame * frameMs`.
- Produces: `extern "C" int uv_meter(int theme, int w, int h, int mode, int frames, int frameMs, uint32_t* out)`,
  where `out` holds `2*frames` values (drawn, pushed pairs).

- [ ] **Step 1: Write the failing test** `test_meter.cpp`, with the built-in theme at 480x320 and 320x240, 30 frames,
  frameMs 40. Assertions: two runs give identical counts (deterministic). Scan frame 0 is a full repaint, and
  frames >= 2 have `pushed < W*H/2`. Alert HIGH's worst frame has `drawn >=` the card area
  (`uv_card_default`'s rect). With the `alert-fx` test layout loaded through `sim/packload.h`, the alert with an
  effect has a higher worst-frame `pushed` than the same layout without the effect. After `meter::run`,
  `prevws::render` of frame 5 is still byte-identical to its value before the run (no shared static state leaks).
- [ ] **Step 2: Run** `make -C sim test_meter && ./sim/test_meter`. Expected: build fails (no `ui/meter.h`).
- [ ] **Step 3: Implement** `prevws::demo`, `meter::run` and `uv_meter`. The engine, Frame and buffers are local or
  static to `meter.h` (heap on the host and in wasm; `meter.h` is never compiled into firmware).
- [ ] **Step 4: Run** `./sim/test_meter` (PASS), then `make -C sim regress MANIFEST=...` ("unchanged"), then `npm test`
  (all pass, including `wasm-parity`).
- [ ] **Step 5: Commit** `feat(ui): replay a layout's frames on the host and count pixels drawn and pushed`.

### Task 3: Per-frame log on the board (`FRAMELOG`)

**Files:**
- Modify: `device/engine_adapter.cpp` (per-frame accumulators), `device/engine_adapter.h`, `device/main.cpp`
  (console command next to `TEST` at ~:1137, plus the log line)
- Test: `sim/test_scan_frame.cpp` (the line formatter), `tools/tests/test_meterprobe.py` (Task 4 parses it)

**Interfaces:**
- Produces: `engineadapter::FrameRec { uint32_t drawn, pushed, us, gapMs; int8_t alert; }` (alert = -1 for scan, or
  the shown alert's conf) and `bool engineadapter::takeFrame(FrameRec&)`, true once per finished frame.
- Produces: console `FRAMELOG 1` / `FRAMELOG 0`. While on: the screen does not dim or blank, and each finished frame
  logs `[frm] d <drawn> p <pushed> us <compose+diff+xfer us> gap <ms since previous frame> a <alert>`. us is
  summed over the frame's slices. gap gives the board's real frame period, which the replay needs.
- Produces: the pure formatter `int scanframe::formatFrm(char* out, size_t n, uint32_t d, uint32_t p, uint32_t us,
  uint32_t gap, int a)` in `ui/scan_frame.h`, so the host test pins the exact text the probe parses.

- [ ] **Step 1: Write the failing test** in `test_scan_frame.cpp`: `formatFrm(buf, 64, 153600, 4800, 31250, 41, -1)`
  equals `"[frm] d 153600 p 4800 us 31250 gap 41 a -1"`. Run it and see it FAIL.
- [ ] **Step 2: Implement** the formatter, the accumulators and `FRAMELOG`. The idle-dim path checks the flag.
- [ ] **Step 3: Run** the host test (PASS). Build all five envs (SUCCESS, bss as Task 1). Bootprobe each connected
  board. By hand on the Waveshare: `FRAMELOG 1`, see `[frm]` lines and the screen stays lit for 60 s. Then
  `FRAMELOG 0` and the lines stop. `[eng]` compose/xfer stay within 5% of the Task 1 baseline. Record what was
  actually run in the ledger.
- [ ] **Step 4: Commit** `feat(device): FRAMELOG logs pixels drawn, pushed and time for every frame`.

### Task 4: Calibration packs and the probe

**Files:**
- Create: `tools/meter-packs.mjs`, `tools/meterprobe.py`, `tools/tests/test_meterprobe.py`

**Interfaces:**
- Consumes: `tools/stress-pack.mjs` (large) and `tools/alert-pack.mjs` (every effect), called as child processes.
  The `[frm]` line format comes from Task 3. Loading uses `tools/sdput.py` functions imported as a module (no
  re-implemented protocol).
- Produces: `node tools/meter-packs.mjs OUT` writes `OUT/{meter-small,stress-p4,alert-fx,meter-fullcard}/`.
  meter-small is the Standard layout with one count tile and no decor. meter-fullcard is a HIGH card covering the
  whole screen on both boards, with a flash effect at the fastest speed. The built-in theme is measured with no pack
  (`THEME 0`).
- Produces: `meterprobe.py --port P --board cyd|waveshare --packs OUT --out CSV [--seconds 20]`. For each pack it
  runs put, reboot, waits for the boot banner, sends `THEME <n>` (found by name in the boot log) and `FRAMELOG 1`,
  records scan frames for N s, then sends `TEST` and records alert frames for the HIGH, MED and LOW steps. It writes
  CSV `board,pack,mode,drawn,pushed,us,gap` (mode is `scan`, `low`, `med` or `high`). It deletes the pack folder
  after its run and ends with `FRAMELOG 0`.
- Produces: the pure `parse_frm(text) -> list[dict]` and `mode_of(alert:int) -> str`, tested offline.

- [ ] **Step 1: Write the failing tests** in `test_meterprobe.py`. `parse_frm` on a captured transcript (including
  interleaved `[eng]` lines and a truncated last line) returns exactly the complete `[frm]` rows. `mode_of(-1) ==
  'scan'` and `mode_of(2) == 'high'`. Running `meter-packs.mjs` into a temp dir creates the four folders, each with a
  `theme.json` that the wasm `uv_layout_check` accepts. Run `python3 -m unittest tools/tests/test_meterprobe.py` and
  see it FAIL.
- [ ] **Step 2: Implement** both tools. The probe opens the port the way `bootprobe.py` does (DTR/RTS low) and waits
  ~10 s after a reset.
- [ ] **Step 3: Run** the unittest (PASS) and `node tools/meter-packs.mjs <scratch>` (four folders).
- [ ] **Step 4: Commit** `feat(tools): calibration packs and a probe that logs frame cost per pack on a board`.

### Task 5: Fit and cross-check

**Files:**
- Create: `tools/meterfit.py`, `tools/meter-crosscheck.mjs`, `tools/tests/test_meterfit.py`

**Interfaces:**
- Consumes: the probe CSV (Task 4) and `uv_meter` via `web/designer/js/preview.js` (Task 6 adds `measure`. Until
  then the cross-check calls the module directly).
- Produces: `meterfit.py CSV... --out web/designer/js/meter-fit.json`. Per board it does least squares on all frames
  of all packs (3x3 normal equations in plain Python, no numpy). Output:
  `{"version":1,"measured":"YYYY-MM-DD","firmware":"<git sha>","boards":{"waveshare":{"a":..,"b":..,"c":..,
  "frameMs":<median gap>,"limits":{"scanMs":..,"alertMs":250},"rmsMs":..},"cyd-engine":{...}}}`. The Waveshare
  `scanMs` = 1.15 x the built-in theme's measured worst scan frame. CYD `scanMs` = 14 unless Task 7 changes it.
- Produces: `node tools/meter-crosscheck.mjs CSV PACKS_DIR` runs the wasm meter on each pack with the fit's
  `frameMs` and prints per pack and mode the device vs wasm worst-frame drawn and pushed. It exits 1 if any differs
  by more than 15%.

- [ ] **Step 1: Write the failing tests** in `test_meterfit.py`. Synthetic rows from `ms = 0.0002·d + 0.0001·p + 3`
  with noise of ±0.1 recover a, b and c within 5%. A board whose rows give a negative slope or fewer than 3 distinct
  packs exits 2 with a message, writing no file. `scanMs` = 1.15 x the worst built-in scan frame. Run and see FAIL.
- [ ] **Step 2: Implement** both tools.
- [ ] **Step 3: Run** the unittest (PASS).
- [ ] **Step 4: Commit** `feat(tools): fit frame time from pixels drawn and pushed, and cross-check wasm counts`.

### Task 6: The meter in the designer and the gate

**Files:**
- Create: `web/designer/js/meter.js`, `web/designer/test/meter.test.mjs`, `web/designer/test/fixtures/meter-fit.json`
- Modify: `web/designer/js/preview.js` (`measure`), `web/designer/js/gate.js`, `web/designer/js/app.js` (meter panel on
  the Layout step, debounced re-measure, buttons at ~:337), `web/designer/index.html`, `tools/assemble-designer.sh`
  (copy `meter-fit.json` if not already globbed)

**Interfaces:**
- Consumes: `uv_meter` (Task 2), the `meter-fit.json` format (Task 5).
- Produces: `preview.measure(board, mode, frames = 30, frameMs) -> Array<{drawn, pushed}>` for the current draft.
- Produces: `loadFit(json) -> {ok:true, fit} | {ok:false, why}`, which refuses version != 1, non-finite numbers and
  negative a/b.
- Produces: `meterRows(fit, measured) -> {rows: Array<{board, kind:'scan'|'alert', level:null|'low'|'med'|'high',
  ms, limit, pct, state:'ok'|'amber'|'over'|'uncalibrated'}>, over: boolean, reason: string|null}`. measured is
  `{board: {scan:[...], low:[...], med:[...], high:[...]}}`. Each board has one scan row plus one alert row (the
  worst level). Scan skips frames 0-1. `state` is amber when pct >= 80 and over when pct > 100.
- Produces: `installGate(..., meter = null)`, which adds `{ok:false, reason:'Fix this first: Speed: <board> <kind> is
  <ms> ms, over its <limit> ms limit.'}` when `meter.over`.

- [ ] **Step 1: Write the failing tests** (`meter.test.mjs`, `gate.test.mjs`). `meterRows` with the fixture fit:
  pct 79 -> ok, 80 -> amber, 100 -> amber, 101 -> over. A board absent from the fit -> `uncalibrated`, not over.
  Scan ignores frames 0-1 but alerts include frame 0. `loadFit` refuses a NaN, a negative `a` and version 2. The
  gate blocks with the Speed reason when over and does not block for amber or uncalibrated. Using the real wasm: the
  built-in theme with the Standard layout and the SHIPPED `js/meter-fit.json` is under 100% on both boards
  (skipped with a note until Task 7 commits the file). `measure` for both boards x 4 modes x 30 frames finishes in
  under 2000 ms. Run `npm test` and see these FAIL.
- [ ] **Step 2: Implement** `meter.js`, `measure`, the gate argument, and the panel: always visible on the Layout
  step, one row per board showing scan and worst alert, "~N ms of L ms" with an ok/amber/over mark. A changed layout
  re-measures 400 ms after the last edit. Download, Install and Share are disabled with the gate's reason while over.
- [ ] **Step 3: Run** `npm test` (all pass).
- [ ] **Step 4: Commit** `feat(designer): speed meter on the Layout step; over the limit blocks install and share`.

### Task 7: Calibration session on both boards (owner present)

**Needs:** both boards connected, and the owner's go-ahead for the CYD engine migration below.

- [ ] **Step 1: CYD engine install (owner approves first).** Layouts only run on `nm-cyd-c5-engine`, which needs the
  16 MB partition table (`docs/NM-NATIVE-ENGINE.md`): take a full-flash backup into `private/`, flash the bootloader,
  partitions, boot_app0 and app. After calibration, restore the release `nm-cyd-c5` from the backup unless the owner
  says otherwise.
- [ ] **Step 2: Bootprobe** both boards on the exact calibration builds (12/12 clean).
- [ ] **Step 3: Probe** `node tools/meter-packs.mjs <scratch>/packs`, then `meterprobe.py` on each board into
  `private/logs/meter-<board>-2026-MM-DD.csv`.
- [ ] **Step 4: Fit** `meterfit.py ... --out web/designer/js/meter-fit.json`, then `meter-crosscheck.mjs` (exit 0).
  Report each board's a, b, c, rms error, frameMs and limits to the owner. If built-in CYD scan is over 14 ms on the
  engine build, stop and ask the owner for the CYD scan limit (the spec allows the session to adjust limits).
- [ ] **Step 5: Run** `npm test` (the Task 6 built-in-under-100% test now runs and passes).
- [ ] **Step 6: Commit** `feat(designer): speed meter calibrated on the CYD and the Waveshare` (fit json, CSVs stay
  in `private/`).

### Task 8: Docs

**Files:**
- Modify: `docs/theme-packs.md` (a "Speed" section: what is measured, the limits, why a layout is refused),
  `docs/design-notes.md` (the shared slice loop, the counters, the fit and how to redo it after firmware changes:
  the Task 7 commands), `docs/lessons.md` only if the session taught something.

- [ ] **Step 1: Write** the sections. They name `tools/meterprobe.py`, `tools/meterfit.py` and
  `tools/meter-crosscheck.mjs` with the exact commands, and the numbers come from the committed `meter-fit.json`.
- [ ] **Step 2: Check** `git grep -nE "/home/[a-z]+"` is empty.
- [ ] **Step 3: Commit** `docs: the speed meter, its limits and how to recalibrate it`.
