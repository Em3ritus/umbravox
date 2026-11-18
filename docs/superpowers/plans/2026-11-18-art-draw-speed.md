# Art Draw Speed-up Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A theme pack at the art limits (`tools/stress-pack.mjs`) draws the Waveshare SCAN screen in at most 50 ms a
frame (2026-11-18: ~101 ms), with every pixel unchanged.

**Architecture:** Two pixel-identical changes in the shared drawing code (`ui/`): pictures are decoded a row run at a
time instead of one `artpack::index` call per pixel, and a layer (the VOID fill, the screen background, a decor) is
not drawn at all when a later opaque decor covers the whole strip window. A host benchmark on the stress pack
measures both; the boot probe confirms on the board.

**Tech Stack:** C++17 header code in `ui/` (device: ESP32-C5, RISC-V, no FPU), host tests and benches in `sim/`
(g++), Node for the pack generator, PlatformIO + esptool for the Waveshare.

**Spec:** this plan (owner decision 2026-11-18, "go with 1"; measurements in `private/agent/HANDOFF.md`). The animated
decor already repaints only its own rect (`ui/scan_present.h` ~line 465): not part of this plan.

## Global Constraints

- Pixels unchanged: `test_engine_pixels` and `make -C sim regress` unchanged after every task; every new draw path is
  checked byte-for-byte against the current per-pixel code.
- No floating point in `ui/` drawing code (the C5 has no FPU).
- No new `.bss`/`.data` in internal RAM: `riscv32-esp-elf-size -A` of `ws-c5-35-engine` `.dram0.bss` stays 94,720
  (branch at `70e7095`) or lower.
- Built-in themes: Waveshare compose stays ~29 ms/frame; CYD scan draw median stays ~6 ms.
- Commit trailer as on the branch: `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.
- New `sim/` binaries go in `.gitignore` before `git add` (they were committed by mistake before).

## Review Focus

1. A picture whose left edge is at an odd source x after clipping (strip window `s.lo > 0`, or `x < 0`): the first
   nibble is the LOW one. Test: Task 2 `run_odd_start`.
2. A transparent picture (flag bit 0) over a full window must never count as a cover, even if it has no index-0
   pixel. Test: Task 3 `transparent_not_cover`.
3. A decor whose picture did not load, or is not its rect's size (`sizedArt` returns null), draws nothing and so is
   not a cover. Test: Task 3 `unloaded_not_cover`.
4. A nine-slice whose borders add up to more than the rect or the picture (`nineSrc`'s clamps). Test: Task 2
   `nine_borders_exceed`.
5. Frame index at or past `frames` falls back to frame 0, as now. Test: Task 2 `frame_out_of_range`.

---

### Task 1: The art benchmark

**Files:**
- Create: `sim/bench_art.cpp`
- Modify: `sim/Makefile` (target `bench_art`, not part of `test`; add it to `clean`), `.gitignore` (`sim/bench_art`)

**Interfaces:**
- Consumes: `packload::load(dir, why, cap) -> int id`, `packload::useMedia(id)`, `pal::setTheme(id)`,
  `scanfix::demo(theme)`, `scanr::composeStrip(S&, W, H, scene)`, `scanr::radarBounds`, `scanr::animatedDecor`.
- Produces: `sim/bench_art PACK_DIR` printing, for 480x320 and the stress pack, one line each:
  `full us=<n> pixels=<hash>`, `radar us=<n> pixels=<hash>`, `decor us=<n> pixels=<hash>`, `tile us=<n> pixels=<hash>`
  (FNV-1a of the buffer, as in `bench_scan.cpp`), averaged over 20 runs.

- [ ] **Step 1:** Write `bench_art.cpp`: load the pack, select it, then time (a) a full frame in 16-row strips and
  (b) each region as the presenter draws it: strips over the region's rows with the column window
  `S(px, W, y, h, r.x, r.x + r.w)`, for `radarBounds`, the animated decor rect and `uieng::scan(W,H).tiles[0]`.
- [ ] **Step 2:** Makefile: `bench_art` builds like `test_style_render` (same sources, `-O2`, no sanitizers).
- [ ] **Step 3:** Run `node tools/stress-pack.mjs /tmp/sp && make -C sim bench_art && sim/bench_art /tmp/sp/stress-p4`.
  Expected: four lines, non-zero times. Record them (3 runs, median) in the commit message as the baseline.
- [ ] **Step 4:** Commit `feat(sim): bench_art times the stress pack's frame and its repaint regions`.

### Task 2: Row-run picture decoding

**Files:**
- Modify: `ui/artpack.h` (add `blitRun`), `ui/scan_render.h:474-510` (`artInto`, `nineInto` use it)
- Create: `sim/test_art_draw.cpp`; Modify: `sim/Makefile` (`TESTS` list, sanitizer flags as `test_artpack`), `.gitignore`

**Interfaces:**
- Produces: `inline void artpack::blitRun(const Art& a, uint16_t frame, int sx, int sy, int n, uint16_t* dst)` —
  writes `n` pixels of source row `sy` from column `sx` to `dst[0..n)`; skips index 0 when `a.transparent`; the
  caller keeps `0 <= sx`, `sx + n <= a.w`, `0 <= sy < a.h`, `frame < a.frames`. Walks one byte pointer
  (`px + frame*frameBytes + (sy*w + sx)/2`), starting on the low nibble when `(sy*w + sx)` is odd.
- `artInto`: one `blitRun` per row over the clipped span. `nineInto`: per row, up to three kinds of run — the left
  border (source x = d), the repeated middle (source x = lo + (d - lo) % mid, split where it wraps), the right border
  — each a `blitRun`; where `nineSrc` clamps (borders larger than rect or picture), fall back to the per-pixel path
  for that row so the result is unchanged.

- [ ] **Step 1: Write the failing tests** in `test_art_draw.cpp`. Keep the current `artInto`/`nineInto` bodies in the
  test as `refArtInto`/`refNineInto`. Each test draws the same case with both into two buffers and checks them equal:
  `blitRun_basic` (calls `artpack::blitRun` directly: 7x3 picture, sx 0..6, every n), `run_odd_start` (w 7, x = -3,
  and a window `lo = 5`), `widths` (w 1, 2, 3, 480; h 1 and 320), `transparent_and_opaque`, `frame_out_of_range`
  (frame = frames and 65535), `offscreen` (x, y negative and past W/H; nothing written outside the window),
  `nine_sizes` (rects 1x1 up to 480x320 with slices {0,0,0,0}, {4,4,4,4}, {1,2,3,4}), `nine_borders_exceed`
  (slices 30 on a 20x20 picture in a 10x10 rect). Pictures from `sim/artfix.h make()` and a seeded random 16-colour
  generator.
- [ ] **Step 2:** `make -C sim test_art_draw && ./sim/test_art_draw` → FAIL to compile: `blitRun` is not a member of
  `artpack`.
- [ ] **Step 3:** Implement `blitRun`, then switch `artInto` and `nineInto` to it as described above.
- [ ] **Step 4:** `make -C sim test_art_draw && ./sim/test_art_draw` → all PASS. `make -C sim test` exit 0;
  `make -C sim regress` unchanged; `sim/bench_art` hashes identical to Task 1, times lower (record them).
- [ ] **Step 5:** Commit `perf(ui): pictures decode a row run at a time`.

### Task 3: Skip layers an opaque decor covers

**Files:**
- Modify: `ui/scan_render.h` (`drawChrome` ~1084, `applyLook` ~990, `drawDecor` ~977; new `firstLayer`, `artBlits`)
- Test: `sim/test_art_draw.cpp` (packs built in a temp folder as in `sim/test_style_render.cpp`)

**Interfaces:**
- Consumes: `uieng::activeLook(W)`, `sizedArt(idx, w, h)`, `uieng::MAX_DECOR`.
- Produces: `inline int scanr::firstLayer(int W, int H, const S& s, bool scan)` — the lowest layer this strip must
  draw: 0 = the VOID fill, 1 = the screen background, 2 + i = decor i. It is the highest layer whose picture is
  loaded at its rect's size, is not transparent and whose rect contains the whole window
  `[s.lo, s.hi) x [s.y0, s.y0 + s.h)`; the background counts as a layer with rect `{0,0,W,H}`; decor counts only
  when `scan`. No such layer: 0. `inline uint32_t& scanr::artBlits()` counts `artInto` calls (as
  `radargeometry::fallbackCalls`). `inline bool& scanr::coverEnabled()` (default true; tests set false to compare
  against drawing every layer).
- `drawChrome` skips the VOID `box` when `firstLayer > 0`; `applyLook` still sets fonts and clip but skips the
  background when `firstLayer > 1`; `drawDecor` starts at decor `firstLayer - 2` when that is positive. Computed per
  strip from the live look: nothing cached between frames.

- [ ] **Step 1: Write the failing tests:** `cover_skips` (opaque 240x240 decor.1 over a full-screen bg; a strip window
  inside decor.1 → `firstLayer == 3`, and composing it calls `artInto` once fewer per skipped layer, via
  `artBlits()`), `transparent_not_cover`, `unloaded_not_cover` (decor naming a missing `a5.art`), `partial_not_cover`
  (window one pixel wider than the decor), `not_scan` (decor ignored on other screens), and `frame_identical`: for each
  of these packs and the stress pack, a full frame and the radar region composed with skipping equal the same
  composed with `firstLayer` forced to 0 (a test-only `scanr::coverEnabled()` flag, default true).
- [ ] **Step 2:** `make -C sim test_art_draw && ./sim/test_art_draw` → FAIL to compile (`firstLayer` not declared).
- [ ] **Step 3:** Implement `firstLayer`, `artBlits`, `coverEnabled` and the three skips.
- [ ] **Step 4:** `./sim/test_art_draw` PASS; `make -C sim test` exit 0; `make -C sim regress` unchanged;
  `sim/bench_art` hashes identical to Task 1. Record times.
- [ ] **Step 5:** Commit `perf(ui): a layer under an opaque decor that covers the strip is not drawn`.

### Task 4: Hardware gate and docs

**Files:**
- Modify: `docs/theme-packs.md` (Styles section: one sentence that opaque decor over the background costs nothing
  under it, transparent decor costs its full area), `docs/lessons.md` only if the board surprises us.

- [ ] **Step 1:** `pio run -e ws-c5-35-engine -e nm-cyd-c5`; `riscv32-esp-elf-size -A` → `.dram0.bss` <= 94,720.
- [ ] **Step 2:** Flash the Waveshare (`esptool --no-stub`, `docs/lessons.md` #15). Owner: idle NEVER, SCAN, hands
  off. Built-in theme: `tools/bootprobe.py --port /dev/ttyACM0 --frames 40` → 12/12 clean, compose ~29 ms/frame.
- [ ] **Step 3:** `node tools/stress-pack.mjs OUT`, append a newline to `layout.json` (re-arms the guard), `sdput.py
  put`, owner selects STRESS P4; `bootprobe.py --resets 0 --frames 60` → compose (ms per 10 s / frames) <= 50 ms.
- [ ] **Step 4:** CYD: `pio run -e nm-cyd-c5 -t upload --upload-port /dev/ttyUSB0`, `bootprobe.py` → 12/12, draw ~6 ms.
- [ ] **Step 5:** Commit the docs line with the measured numbers. If compose is still over 50 ms, stop and write up the
  next lever (a PSRAM copy of the static background and decor) for the owner; do not start it in this plan.
