# Cosmic Muffin Theme Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a third on-device theme, COSMIC MUFFIN — neon palette, a full-colour muffin bitmap centrepiece, six new cross-theme sweeps, and four per-theme selectable alerts.

**Architecture:** The theme is one row in the `pal` table; adding it is additive. The muffin is a generated full-colour bitmap (`ui/muffin.h` from `tools/gen_muffin.py`) blitted masked to the dial, branched beside the existing heptagram/seal centrepieces. The six sweeps join the `Sweep` enum and draw only from `pal::` colours, so they recolour per theme; the per-theme sweep mask widens from 8 to 16 bits to hold them. The four alerts are drawn by new functions dispatched on a new persisted `alertStyle` setting, read only for this theme.

**Tech Stack:** C++17, the `ui/` shared host+device code, PlatformIO (envs `nm-cyd-c5`, `nm-c5-node`, `nm-c5-coordinator`), the `sim/` host harness (`make test` / `make regress`), Python 3 + Pillow for the asset generator.

**Spec:** `docs/superpowers/specs/2026-10-21-cosmic-muffin-theme-design.md`

## Global Constraints

- RGB565 throughout; colours constructed with `pal::rgb(r,g,b)`.
- UI code lives in `ui/` and must compile for **both** host and device; device-only code stays in `device/`.
- Sweeps draw only from `pal::` colour variables (never literal colours), so they recolour per theme with no per-theme branch.
- Alerts are **per-theme**: UMBRAVOX keeps its ward, BAPH0MAU5 keeps `drawAlertSummoned`; the four new alerts belong to COSMIC MUFFIN only.
- The generated `ui/muffin.h` is machine-made: a header comment names `tools/gen_muffin.py` and the source art; never hand-edited.
- Commit after each task. Run `make -C sim test` before every commit; it must pass.
- Attribution trailer on every commit: `Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>`.

## Review Focus

- **Out-of-range `theme` / `sweep` / `alertStyle` from NVS** (downgrade or corruption): `clampSettings` must snap each into range without indexing off the table — Task 1 (theme), Task 3 (sweep vs the theme's mask), Task 4 (alertStyle).
- **Sweep-mask bit overflow**: `1 << v` for `v ≥ 8` and a `uint8_t` mask silently drops the new sweeps; mask and shifts must be 16-bit — Task 1.
- **`gen_muffin.py` run from the wrong directory / missing source art**: it must exit non-zero with a clear message, not write a broken header — Task 2.
- **Muffin blit staying inside the dial**: the bitmap must be clipped to the circle and never write outside the canvas at the theme's `rField` — Task 2.
- **Settings size/padding drift**: `Settings` is 10 bytes with no padding today, and `dwellMs` (uint16) makes it 2-aligned — so `alertStyle` alone (→11) adds a phantom padding byte. It must be added with a named `reserved` slack byte (→ padding-free `sizeof == 12`), the way `labels` filled slack before; the canary catches any miss — Task 4.

---

### Task 1: Theme row + 16-bit sweep mask

**Files:**
- Modify: `ui/palette.h` (ThemeId enum, `sweepMask` return type)
- Modify: `ui/palette.cpp` (`Row.sweeps` type, add the COSMIC MUFFIN row, `themeName`)
- Modify: `ui/nav.cpp:86` (`stepSweep`: cast the shift to the mask width)
- Test: `sim/test_palette.cpp` (new)
- Modify: `sim/Makefile` (add `test_palette` to `TESTS` + a build rule)

**Interfaces:**
- Produces: `pal::TH_COSMICMUFFIN = 2`, `pal::TH_COUNT = 3`; `pal::sweepMask(uint8_t) -> uint16_t`; `pal::themeName(2) == "COSMIC MUFFIN"`.

- [ ] **Step 1: Write the failing test** `sim/test_palette.cpp`

```cpp
#include "../ui/palette.h"
#include <cstring>
// check(): same helper shape as the other sim tests
check("three themes", pal::TH_COUNT == 3);
check("cosmic name", !strcmp(pal::themeName(pal::TH_COSMICMUFFIN), "COSMIC MUFFIN"));
pal::setTheme(pal::TH_COSMICMUFFIN);
check("neon void applied", pal::VOID == pal::rgb(0x09,0x07,0x1C));
check("cosmic offers all sweeps", pal::sweepMask(pal::TH_COSMICMUFFIN) == 0x07FF); // 11 bits
pal::setTheme(99);  // out of range
check("bad theme id falls back to UMBRAVOX", pal::VOID == pal::rgb(0x07,0x04,0x0E));
```

- [ ] **Step 2: Run it, verify it fails** — `make -C sim test_palette` → FAIL (TH_COSMICMUFFIN undefined).
- [ ] **Step 3: Widen the mask** — in `ui/palette.h` change `uint8_t sweepMask(...)` to `uint16_t sweepMask(...)`; in `ui/palette.cpp` change `Row.sweeps` to `uint16_t` and `sweepMask` return to `uint16_t`. In `ui/nav.cpp` `stepSweep`, make the test `(mask & (uint16_t)(1u << v))`.
- [ ] **Step 4: Add the enum + row** — add `TH_COSMICMUFFIN` before `TH_COUNT` in `ui/palette.h`; add a third `Row` to `THEMES[]` with the palette values from the spec's table and a `sweeps` literal of `0x07FF` (all eleven sweeps once Task 3 lands; harmless now — unknown bits are just unoffered). Dial geometry copied from the BAPH0MAU5 row to start. Add its `themeName` arm.
- [ ] **Step 5: Run tests** — `make -C sim test` → PASS.
- [ ] **Step 6: Commit** (`feat: COSMIC MUFFIN theme row + 16-bit sweep mask`).

---

### Task 2: Muffin centrepiece (generator, header, renderer, branch)

**Files:**
- Create: `tools/gen_muffin.py`
- Create: `ui/muffin.h` (generated, committed)
- Create: `ui/muffinseal.cpp`, `ui/muffinseal.h`
- Modify: `ui/screen_scan.cpp` (`drawDialAt`: centrepiece branch ~line 596)
- Modify: `sim/Makefile` (`SRC` += `../ui/muffinseal.cpp`)
- Test: `sim/test_muffin.cpp` (new) + add to `TESTS`

**Interfaces:**
- Produces: `ui/muffin.h` with `constexpr int MUFFIN_W, MUFFIN_H;` `constexpr uint8_t MUFFIN_PX[MUFFIN_W*MUFFIN_H];` (palette indices) `constexpr uint16_t MUFFIN_PAL[N];` (RGB565). `ui::drawMuffin(Canvas& c, int cx, int cy, int r, uint32_t frame, int liftOverride)` in `muffinseal.h`.

- [ ] **Step 1: Write `tools/gen_muffin.py`** — mirror `tools/gen_sigil.py`: resolve the source art relative to repo root (exit non-zero with a message if absent, run-from-repo-root like gen_sigil), crop to the muffin, downsample to `MUFFIN_W=MUFFIN_H=72`, quantise to ≤16 colours, write `ui/muffin.h` (generated banner comment + the three arrays). Source art: `private/mockup/Muffin/2026-10-21_09-21.pixel-8bit (1).png` (copy into `tools/` or reference an agreed in-repo path — the private/ art is gitignored, so the generator must read a committed copy; add `ui/art/cosmic-muffin-src.png` as that committed source).
- [ ] **Step 2: Generate** — run `python3 -I tools/gen_muffin.py` from repo root; confirm `ui/muffin.h` written with the expected `MUFFIN_W/H`.
- [ ] **Step 3: Write the failing test** `sim/test_muffin.cpp`

```cpp
// drawMuffin writes lit pixels inside the circle and NONE outside it.
Canvas c; c.fill(0,0,W,H, 0x0000);
pal::setTheme(pal::TH_COSMICMUFFIN);
ui::drawMuffin(c, 80, 60, 30, 0, 128);
check("draws something", anyNonZeroWithin(c, 80,60, 30));
check("nothing outside the disc", allZeroOutside(c, 80,60, 31));
```

- [ ] **Step 4: Run it, verify it fails** — FAIL (drawMuffin undefined).
- [ ] **Step 5: Implement `drawMuffin`** in `ui/muffinseal.cpp` — blit `MUFFIN_PX`→`MUFFIN_PAL` scaled to diameter `2*r`, clipped to the circle of radius `r`; apply `liftOverride` (128 = unmodified, below sinks each pixel toward `pal::VOID`, above toward white) exactly as `seal.cpp` does. No pixel written where `dx*dx+dy*dy > r*r` or outside the canvas.
- [ ] **Step 6: Branch the centrepiece** — in `drawDialAt`, add `else if (s.set.theme == pal::TH_COSMICMUFFIN) { drawMuffin(c, cx, cy, rField, s.frame, searchLift(s.set.sweep, s.frame)); }` before the heptagram `else`.
- [ ] **Step 7: Run tests** — `make -C sim test` → PASS.
- [ ] **Step 8: Commit** (`feat: muffin centrepiece -- gen_muffin.py, ui/muffin.h, drawMuffin`).

---

### Task 3: Six cosmic sweeps

**Files:**
- Modify: `ui/model.h:49` (`Sweep` enum)
- Modify: `ui/sweep.cpp` (`drawSearch`, `searchTouches`, `searchMarkLum`, `searchLift`, `pulseRing` switches)
- Modify: `ui/palette.cpp` (set the new bits in all three rows' `sweeps`)
- Test: `sim/test_sweep.cpp` (new) + add to `TESTS`

**Interfaces:**
- Produces: `SW_GALAXY, SW_ORBIT, SW_STARFALL, SW_PULSAR, SW_COMET, SW_AURORA` added before `SW_COUNT` (now 11).

- [ ] **Step 1: Write the failing test** `sim/test_sweep.cpp`

```cpp
check("eleven sweeps", ui::SW_COUNT == 11);
// every mode draws without writing outside the dial, for each theme
for each theme t, for each mode m in 0..SW_COUNT-1 (that the mask offers):
    pal::setTheme(t); Canvas c cleared; drawSearch(c, m, 80,60, 30, 0,0,0, f);
    check("in bounds", allZeroOutside(c, 80,60, 32));
check("galaxy offered by UMBRAVOX", pal::sweepMask(pal::TH_UMBRAVOX) & (1<<ui::SW_GALAXY));
```

- [ ] **Step 2: Run it, verify it fails** — FAIL (SW_GALAXY undefined / count wrong).
- [ ] **Step 3: Add the enum values** in `ui/model.h`.
- [ ] **Step 4: Implement each mode** in `ui/sweep.cpp` — add a `case` arm per new mode in each switch, using only `pal::` colours. Motion per the mockups (`private/cosmic-muffin/cosmic-muffin-sweeps*.gif`): Galaxy = rotating spiral arms; Orbit = planet+comet-tail on the rim; Starfall = crossing streaks + twinkle; Pulsar = expanding rings (reuse `pulseRing` detection); Comet = long-tailed comet circling; Aurora = undulating neon ring-bands. `searchTouches`: Pulsar finds by distance (like `SW_PULSE`); Galaxy/Orbit/Comet find by bearing (like the radar); Starfall/Aurora don't find (return false, like a wash). `searchLift` returns -1 for all six (they don't own the centrepiece's light).
- [ ] **Step 5: Offer them** — OR the six bits into each row's `sweeps` in `ui/palette.cpp` (UMBRAVOX, BAPH0MAU5, COSMIC MUFFIN).
- [ ] **Step 6: Run tests** — `make -C sim test` → PASS.
- [ ] **Step 7: Commit** (`feat: six cosmic sweeps, offered across all themes`).

---

### Task 4: Four alerts + the `alertStyle` setting

**Files:**
- Modify: `ui/model.h` (`AlertStyle` enum; `Settings.alertStyle`)
- Modify: `ui/screen_scan.cpp` (`drawAlert` dispatch; four `drawAlert*` fns)
- Modify: `ui/nav.cpp` (`clampSettings`, `settingsEqual`, grimoire `alertStyle` cell)
- Modify: `device/prefs.cpp` (save/load `alertStyle`)
- Modify: `sim/test_settings.cpp` (canary → 11; exercise `alertStyle`)
- Test: `sim/test_nav.cpp` (clamp + cycle, extend existing)

**Interfaces:**
- Produces: `enum AlertStyle { AL_SUPERNOVA=0, AL_VORTEX, AL_COMET, AL_LOCKON, ALERT_STYLE_COUNT };` `Settings.alertStyle = 0`, plus `Settings.reserved = 0` (the padding-free slack byte, next-setting slot).

- [ ] **Step 1: Write the failing tests** — in `test_settings.cpp` bump `static_assert(sizeof(Settings) == 12, ...)` and add both `alertStyle` and `reserved` to the exercised/compared fields; in `test_nav.cpp` add:

```cpp
Settings s; s.theme = pal::TH_COSMICMUFFIN; s.alertStyle = 99;
clampSettings(s); check("alertStyle clamped", s.alertStyle < ui::ALERT_STYLE_COUNT);
```

- [ ] **Step 2: Run them, verify they fail** — FAIL (size 10≠12; `alertStyle`/`AlertStyle` undefined).
- [ ] **Step 3: Add the fields + enum** in `ui/model.h`: `uint8_t alertStyle = 0;` and `uint8_t reserved = 0;` (mirror the `labels` slack comment so the struct stays padding-free at 12 bytes), and the `AlertStyle` enum. Clamp `alertStyle < ALERT_STYLE_COUNT` in `clampSettings`; include `alertStyle` in `settingsEqual` (compare `reserved` too, harmlessly).
- [ ] **Step 4: Dispatch + draw** — in `drawAlert`, keep the UMBRAVOX ward and the `TH_BAPH0MAU5 -> drawAlertSummoned` arm; add `TH_COSMICMUFFIN -> switch (s.set.alertStyle)` over the four new `drawAlertSupernova/Vortex/CometStrike/LockOn(Canvas&, const State&, ...)`, animated per `cosmic-muffin-alert*.gif` / `-strip*.png`. Threat name + `alertConf` colour-coding reused.
- [ ] **Step 5: Persist** — `nvs.putUChar("alertsty", s.alertStyle)` in `save`, `getUChar("alertsty", ...)` in `load` (`device/prefs.cpp`).
- [ ] **Step 6: Menu cell** — add an `alertStyle` cell to the GRIMOIRE (after the LABELS cell) that cycles `alertStyle`, shown/active only when `st.theme == pal::TH_COSMICMUFFIN`.
- [ ] **Step 7: Run tests** — `make -C sim test` → PASS.
- [ ] **Step 8: Commit** (`feat: four Cosmic Muffin alerts + selectable alertStyle`).

---

### Task 5: Sim previews, demo, regression baseline, hardware

**Files:**
- Modify: `sim/main_sim.cpp` (`--alert <n>` flag; `--theme 2` already works)
- Modify: `ui/demo.cpp` (a COSMIC MUFFIN demo state so all screens render)
- Modify: `private/baseline/MANIFEST.md5` (rebaseline, gitignored)
- Modify: `README.md` / `docs/design-notes.md` (note the third theme)

- [ ] **Step 1: Sim flag** — add `--alert <n>` to `main_sim.cpp` setting `s.set.alertStyle` and forcing `s.alertActive` so each alert renders to PNG; `--theme 2`/`--sweep 5..10` already route through.
- [ ] **Step 2: Visual check** — render the nine screens at `--theme 2`, each sweep, each alert; eyeball against the mockups in `private/cosmic-muffin/`.
- [ ] **Step 3: Rebaseline** — regenerate `private/baseline/MANIFEST.md5` per the `regress` target's documented procedure, after the visual check; `make -C sim regress` → PASS.
- [ ] **Step 4: Build firmware** — `pio run -e nm-cyd-c5 -e nm-c5-node -e nm-c5-coordinator` all succeed.
- [ ] **Step 5: Hardware pass** — flash the CYD; confirm the theme, each of the eleven sweeps, the muffin centrepiece, and each alert (the `TEST` console command walks the alert card). Note results in `docs/`.
- [ ] **Step 6: Commit** (`feat: Cosmic Muffin -- sim previews, demo, rebaseline, docs`).

---

## Notes for the executor

- The private-folder art is gitignored; Task 2 commits a copy as `ui/art/cosmic-muffin-src.png` so the generator has a committed source (confirm this location is acceptable, else ask).
- Node/coordinator envs don't compile the scan screen, but they do compile `ui/`; keep `muffinseal.cpp` free of device-only headers so all envs build.
- Keep each sweep's `searchTouches` honest — a mode that finds by bearing and one that doesn't are not interchangeable (see `sweep.h`).
