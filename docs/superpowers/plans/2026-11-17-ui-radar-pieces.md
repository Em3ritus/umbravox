# UI Radar Pieces Implementation Plan (plan 2 of 6)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** On Scan, the radar, spectrum, channel readout and emblem badge become separate `layout.json` pieces; the radar
can be any square size from a minimum up to today's, with rings generated on the device for radii that have no table.

**Architecture:** The built-in hero sub-geometry (today's `heroSub`/`radarBounds`/`chRect`/`specRect` and the badge
offsets in `ui/scan_render.h`) moves into `ui/layout_engine.h` as `heroParts()`, and `ScanLayout` gains `radar`,
`spectrum`, `channel`, `badge` rects that `scan()` fills from the built-in geometry or the override. Drawing and dirty
regions read those rects. An integer ring generator (`ui/ring_gen.h`) reproduces the baked tables exactly and serves
any other radius from a small cache.

**Tech Stack:** C++17 in `ui/`, host tests in `sim/`, PlatformIO firmware.

**Spec:** `docs/superpowers/specs/2026-11-17-ui-overhaul-design.md` (Pieces; Radar size). Builds on plan 1
(`docs/superpowers/plans/2026-11-17-ui-layout-foundation.md`), same branch `ui-overhaul`.

**Research:** `private/agent/notes/radar-geometry-map.md` (every formula and line number below comes from it).

## Global Constraints

- Built-in themes stay byte-identical: `sim/test_engine_pixels` (220 fingerprints) and `make -C sim regress` pass unchanged.
- Radar sizes: square; radius `r = (w - 5) / 2` from the piece's box (the box is `2r+5`, today's margin for markers).
  Minimum `r = 24` (box 53). Maximum today's: Waveshare `r = 102` (box 209), CYD `r = 55` (box 115).
- Every ring the radar draws comes from a table or the generator; the `double` trig ring fallback
  (`scan_render.h` ~1147-1150) is never reached for any radius 8..102.
- The generator output for every radius that has a baked table equals that table exactly (same points, same order,
  same coverage).
- Ring cache memory: device in PSRAM (`heap_caps_malloc(MALLOC_CAP_SPIRAM)`), never `.bss`; host static.
- `radar.vid` accepts any square size from 24x24 to 102x102 (still drawn at 2x, so a clip of side `r` fills a radar of
  radius `r`). Released firmware rejects non-102 clips and skips only the clip.
- New pieces: `radar`, `spectrum`, `channel`, `badge`, all hideable. `channel` and `badge` and `spectrum` are not
  tappable (no 44 px rule). Floors: `channel` at least its built-in size; `badge` at least 24x24;
  `spectrum` at least 42 wide (pitch 2 x 21 channels) and `band + gap + 4` tall (Waveshare 14+4+4 = 22, CYD 11+7+4 = 22).
- Commits: author `Em3ritus <152051018+Em3ritus@users.noreply.github.com>`, trailer
  `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.

## Decisions made here

- **Hero with pieces:** `hero` becomes the bounding box of the visible radar-group pieces (radar, spectrum, channel,
  badge). The LOW/MED alert takeover covers that box, so `covered` in `scan_present.h` stays valid; the threat hit is
  the top 44 rows of that box, as today. If all four are hidden, `hero` is the built-in hero shifted (plan 1).
  The tile-over-radar check becomes tile-over-any-radar-group-piece.
- **Spectrum drawing** uses the piece rect with per-board constants so the built-in reproduces exactly:
  `band` (label band under the bars) Waveshare 14 / CYD 11, `gap` (above the tallest bar) Waveshare 4 / CYD 7;
  `barBase = y + h - band`, `barMax = h - band - gap`, `pitch = max(2, w / 21)`. The wide/compact label rule stays.
- **Channel** is right-aligned at `x + w - 4`, top `y + 4`, scale 2 on the Waveshare, 1 on the CYD.
  Built-in rect Waveshare `chRect` = `{hero.x+hero.w-100, hero.y, 100, 24}`; CYD `{hero.x+hero.w-60, hero.y, 60, 16}`.
- **Badge** is drawn centred in its rect with radius `min(w,h)/2 - 4`. Built-in rect `{hero.x, hero.y, 2R+8, 2R+8}`,
  `R` = 27 Waveshare / 22 CYD (so the centre is today's `hero.x+4+R`).
- **Rune ring** (Waveshare) is drawn only when `r >= 42`; below that the 11 px glyphs would collide.
- **crewArt's `radius == 55` native set** stays as is (it is the correct art for that size on either board).

## Review Focus

1. A radius just above or below a tabled one (e.g. 101, 54, 25): rings drawn from the generator, ink inside the radar box, no trig fallback (Task 1, Task 4).
2. Radar at the minimum (box 53) with every sweep style and the cosmic/cvlt/mascot art: no ink outside the radar box (Task 4).
3. A layout that hides the radar but keeps contacts arriving: no crash, nothing drawn, dirty regions skip it (Task 4).
4. A clip whose side differs from the placed radius (designer not yet re-converting): masked to the disc, never drawn outside the box (Task 5).
5. Spectrum at its minimum size: bars and labels inside the piece on both boards (Task 4).

---

### Task 1: Integer ring generator and ring cache

**Files:**
- Create: `ui/ring_gen.h`
- Modify: `ui/scan_render.h` (ring lookup at ~1127 uses `ringFor`)
- Test: `sim/test_ring_gen.cpp` (new; Makefile target + `TESTS`)

**Interfaces:**
- Produces:
```cpp
namespace radargeometry {
int  generateRing(int r, Point* out, int cap);      // row-major (y, then x), as the baked tables; returns count, or -1 if cap too small
int  ringCapacity(int r);                            // an upper bound on generateRing's count for r
void setRingStorage(Point* buf, int cap);            // device: a PSRAM block; host default: static storage
const Point* ringFor(int r, int& count);             // baked table if one exists, else the cache (generate on miss), else nullptr
}
```
- Algorithm (must match `tools/gen_radar_geometry.py` exactly): for each pixel `(x, y)` with `y` from `-r-2` to `r+2`
  and `x` likewise, skip unless `|hypot(x,y) - r| <= 1.25` — evaluate in integers as
  `(4*(x*x+y*y)) ` against `(2r-2.5)^2` and `(2r+2.5)^2` scaled by 16 (all dyadic); count 8x8 sub-samples at
  `(16x + 2sx + 1 - 8, 16y + 2sy + 1 - 8)` (units of 1/16 px) with `(16r-8)^2 <= X^2+Y^2 <= (16r+8)^2`;
  `alpha = (hits*16 + 32) / 64`; emit `{x, y, alpha}` only when `alpha > 0` if the script does (check the script's
  rule and copy it). The cache keeps the 3 most recent radii (LRU).

- [ ] **Step 1: Write the failing tests:** `generator equals every baked table` (all 23 radii, point by point);
  `untabled radius 77 returns points from the cache`; `cache evicts least recent of 3`; `every radius 8..102 has a ring
  via ringFor`; `capacity is an upper bound for 8..102`.
- [ ] **Step 2: Run, expect FAIL** — `make -C sim test_ring_gen && sim/test_ring_gen` (no header).
- [ ] **Step 3: Implement**; read `tools/gen_radar_geometry.py` first and mirror its band test and alpha rule. Switch the
  draw at `scan_render.h` ~1127 to `ringFor`.
- [ ] **Step 4: Run, expect PASS**; `make -C sim test_engine_pixels test_clean_radar` and run both → unchanged.
- [ ] **Step 5: Commit** — `feat(ui): integer ring generator, bit-identical to the baked tables`.

### Task 2: Built-in hero parts in the layout engine

**Files:**
- Modify: `ui/layout_engine.h` (`heroParts`, `ScanLayout` + `ChromeOverride` gain the four rects)
- Modify: `ui/scan_render.h` (`heroSub`, `radarBounds`, `chRect`, `specRect` read them)
- Test: `sim/test_layout_override.cpp`, `sim/test_clean_radar.cpp` (unchanged expectations)

**Interfaces:**
- Produces:
```cpp
namespace uieng {
struct HeroParts { Rect radar, spectrum, channel, badge; };
inline HeroParts heroParts(int W, int H, Rect hero);   // today's formulas for this hero (map §1)
// ScanLayout and ChromeOverride gain: Rect radar, spectrum, channel, badge;
}
```
- [ ] **Step 1: Test** `built-in parts reproduce today's`: Waveshare radar `{188,56,209,209}`, spectrum side column
  `{398,194,66,74}` (specRect) and bars rect `{399,194,63,74}`, channel `{364,52,100,24}`, badge `{124,52,62,62}`;
  CYD radar `{151,52,115,115}`, spectrum `{138,162,140,26}`, channel `{252,52,60,16}`, badge `{104,52,52,52}`.
  (Recompute each from the map's formulas before asserting; fix the test numbers, not the formulas, if a hand value is off.)
- [ ] **Step 2: Run, expect FAIL.**
- [ ] **Step 3: Implement**; `scan_render.h`'s helpers return `uieng::scan(W,H).radar` etc. Keep `heroSub` for the
  compact `field` only.
- [ ] **Step 4: Run**: the new test, `test_engine_pixels` (unchanged), `test_clean_radar`, `test_sweep_tail`,
  `test_scan_fx`, `test_radar_video`, `test_scan_present` → all pass.
- [ ] **Step 5: Commit** — `refactor(ui): hero parts are scan() rects`.

### Task 3: Radar-group pieces in layout.json

**Files:**
- Modify: `ui/themelayout.h`, `ui/themelayout.cpp`
- Test: `sim/test_themelayout.cpp`

**Interfaces:**
- Produces: `Piece` gains `P_RADAR, P_SPECTRUM, P_CHANNEL, P_BADGE` appended after `P_COUNT_BLE`; `PIECE_NAME` gains
  `"radar" "spectrum" "channel" "badge"`; `isTile`/`hideable`/`tappable` stop using `p >= P_COUNT_WIFI` ranges and
  name their members. `resolve` fills `ChromeOverride::radar/spectrum/channel/badge` (DEFAULT = built-in shifted by the
  body delta) and sets `hero` per "Decisions".

- [ ] **Step 1: Tests** (both boards): `defaults equal heroParts shifted`; `radar must be square`; `radar under 53
  refused`; `radar over today's box refused`; `radar 101 wide accepted (r = 48)`; `spectrum under 42 wide refused`;
  `channel smaller than built-in refused`; `badge under 24 refused`; `all four hideable`; `tile over the moved radar
  refused`; `hero is the bounding box of the visible group`; `hero with all four hidden is the shifted built-in hero`;
  `radar over spectrum refused`.
- [ ] **Step 2: Run, expect FAIL.**
- [ ] **Step 3: Implement.** Content area is unchanged (the group, like tiles, does not shape the body).
- [ ] **Step 4: Run, expect PASS**; also `test_layout_override`, `test_layout_pack`, `test_engine_pixels`.
- [ ] **Step 5: Commit** — `feat(ui): radar, spectrum, channel and badge are layout pieces`.

### Task 4: Draw and repaint each piece in its rect

**Files:**
- Modify: `ui/scan_render.h` (radar centre/radius from `L.radar`; channel, badge, spectrum from their rects with the
  "Decisions" constants; hidden piece = skip; rune ring only `r >= 42`), `ui/scan_present.h` (dirty: radar, channel,
  spectrum rects; badge rect added when the theme changes; a hidden piece adds nothing)
- Test: `sim/test_engine_pixels.cpp` (new layout cases)

- [ ] **Step 1: Tests:** with layouts that (a) move the radar to the left of the hero at r = 77 and the spectrum
  below the tiles, (b) put the radar at the minimum box 53, (c) hide the radar, (d) put the spectrum at its minimum —
  render Scan for every built-in theme on both boards and every sweep style (`scanfix::demo` + `sc.sweep` loop): ink only
  in visible pieces and the body (reuse `strayInk`, adding the four new rects to its piece list); and `no trig ring
  fallback`: make the fallback path count calls in a test hook (`radargeometry::fallbackCalls()`), assert 0.
- [ ] **Step 2: Run, expect FAIL.**
- [ ] **Step 3: Implement.** Hero-derived code that remains (`alertGeom(L.hero)`, `composeStrip`'s `inside` test,
  threat hit) keeps reading `hero`, which is now the group's box.
- [ ] **Step 4: Run**: the new tests; `test_engine_pixels` built-ins unchanged; `test_scan_present`, `test_scan_fx`,
  `test_alert_takeover`, `test_clean_radar`, `test_sweep_tail` → pass.
- [ ] **Step 5: Commit** — `feat(ui): radar pieces draw and repaint in their own rects`.

### Task 5: Radar clips of any square size

**Files:**
- Modify: `ui/themepack.h`, `ui/themepack.cpp` (`parseClip`: square `w == h`, 24..102; frame bytes from `w`;
  `MAX_CLIP` unchanged as the cap), `sim/test_radarclip.cpp`, `test_cosmic_clip.cpp`, `test_cvlt_clip.cpp` where they
  assert 102-only behaviour, `web/designer/js/limits.json` if it states 102 (check `limits.test.mjs`)
- Test: `sim/test_radarclip.cpp`, `sim/test_radar_video.cpp`

- [ ] **Step 1: Tests:** `64x64 clip accepted`; `64x63 refused (not square)`; `23x23 refused`; `103x103 refused`;
  `file length must match w*w/2 per frame`; render: a 64x64 clip in a r = 64 radar fills the disc; a 102 clip in a
  r = 48 radar is masked to the disc (no ink outside the radar box).
- [ ] **Step 2: Run, expect FAIL.**
- [ ] **Step 3: Implement.** `videoInto` already uses `v.w`; keep the 2x draw.
- [ ] **Step 4: Run, expect PASS**; designer `npm test` (the wasm bridge parses clips too).
- [ ] **Step 5: Commit** — `feat: radar.vid may be any square size from 24 to 102`.

### Task 6: Device ring storage, speed and docs

**Files:**
- Modify: `device/main.cpp` or `device/themeload.cpp` (allocate the ring cache in PSRAM at boot and call
  `radargeometry::setRingStorage`), `docs/theme-packs.md` (layout.json section: the four pieces, sizes, floors;
  radar.vid sizes), `CHANGELOG.md`
- Test: `make -C sim bench`

- [ ] **Step 1:** Add a bench case: scan draw at r = 77 (generated) vs r = 102 (table), and the one-off generation time
  for r = 101; record numbers in the commit body. Expected: draw within 5% of the table case; generation under 5 ms host.
- [ ] **Step 2:** Device storage; all firmware envs build (`pio run -e nm-cyd-c5 -e nm-cyd-c5-engine -e ws-c5-35-engine
  -e ws-c5-28-engine -e nm-c5-node -e nm-c5-coordinator`).
- [ ] **Step 3:** Docs; `npm test`; full `make -C sim test` (background; ~45 min) → all pass.
- [ ] **Step 4: Commit** — `feat: ring cache in PSRAM; docs for radar pieces`.
- [ ] **Step 5 (hardware, owner present):** boot probe with a layout whose radar is r = 77; median scan draw must stay
  ≤ ~14 ms. Not connected → ledger "not run".
