# Cosmic Muffin — Theme Design

**Date:** 2026-10-21
**Status:** approved 2026-10-21 (alerts per-theme, sweeps cross-theme — confirmed)

## Intent

Add a third on-device theme, **COSMIC MUFFIN**: a neon, deep-space look built
from the user's galaxy-cupcake reference art, alongside UMBRAVOX and
CVLT OF BAPH0MAU5. Its centrepiece is the muffin itself (a full-colour bitmap,
not a drawn figure), and it ships a family of new cosmic **sweeps** and
**alerts**. The new sweeps are also offered by the two existing themes,
recoloured to each theme's palette.

The look was settled through mockups in `private/cosmic-muffin/` (palette,
emblem, sweeps GIFs, alert filmstrips). This document is the build spec.

## Goals

- A new theme `TH_COSMICMUFFIN` with a palette pulled from the reference.
- The muffin as the dial centrepiece: a **full-colour** circular bitmap,
  generated from the pixel-art reference, masked to the dial.
- **Six new sweeps** — Galaxy, Orbit, Starfall, Pulsar, Comet, Aurora — drawn
  from `pal::` colours so they recolour per theme, and offered by **all three**
  themes (subject to the centrepiece caveat below).
- **Four alerts** — Supernova, Vortex, Comet Strike, Lock-On — selectable on
  the device, as the Cosmic Muffin theme's alert set.
- Everything host-testable and sim-previewable, as the rest of the UI is.

## Non-goals / anti-goals

- Not touching capture, mesh, logging, or signatures — this is UI only.
- Not replacing the existing sweeps (radar/gaze/pulse/star/candle) — the six
  cosmic sweeps are additive.
- Not changing UMBRAVOX's or BAPH0MAU5's own centrepiece or alert — they keep
  the heptagram/ward and the seal/Summoned rite respectively. They only gain
  the new sweeps.

## Palette (RGB565, maps to the `Row` in `ui/palette.cpp`)

Pulled from the reference; the role names are the existing palette seam
(`MAGENTA` = 2.4 GHz band, `ICE` = 5 GHz, `GREEN` = BLE — hues, not names).

| role | value | note |
|------|-------|------|
| void_ | `#09071C` | deep space ground |
| panel | `#16103A` | card fill |
| grid | `#1E1646` | lattice in the circle |
| edge / edgeLit | `#3A2A6E` / `#A84DFF` | ring work, neon violet |
| dim / text / bright | `#8A7BD0` / `#E8DEFF` / `#FFFFFF` | type |
| witch / witchHi / bone | `#A84DFF` / `#FF1FCC` / `#FFD04A` | chrome: violet, magenta, gold |
| sweep / sweepHi | `#1EF5EE` / `#FF1FCC` | the beam: cyan body, magenta edge |
| live / liveHi | `#FF58B0` / `#FFF0D0` | centrepiece alight (muffin glow) |
| ramp[4] | void → `#3A2A6E` → `#A84DFF` → `#FFF0E8` | dark→light anchor levels |
| magenta / ice / green | `#FF1FCC` / `#1EF5EE` / `#3BFF9E` | 2.4 / 5 / BLE data bands |
| amber / red | `#FFC24A` / `#FF4D6A` | status |

Dial geometry (`dialY,dialH,dialCx,dialCy,rField,rRunes,rBound,rStar,specY,specH`)
starts from BAPH0MAU5's values (it also has a large bitmap centrepiece) and is
tuned so the muffin fills the circle.

## The muffin centrepiece (the one genuinely new mechanism)

The existing centrepieces are *drawn* (UMBRAVOX heptagram) or a *ramp bitmap*
(BAPH0MAU5 face via `ui/sigil.h`, four grey levels tinted by the theme). The
muffin is full-colour, so it needs a full-colour bitmap path:

- **Generator** `tools/gen_muffin.py` (sibling of `gen_sigil.py`): crop the
  reference to the muffin, downsample to the centrepiece diameter (~`2*rField`,
  ~60–72 px), quantise to a small indexed palette, and emit `ui/muffin.h` — an
  indexed bitmap plus its `uint16_t` RGB565 colour table (and a 1-bit circle
  mask, or mask at draw time). Source art path recorded in the header comment;
  regenerate with the script, never hand-edit. ~72×72 at 1 byte/px + a ~16-entry
  table ≈ 5 KB in flash.
- **Renderer** `ui/muffin.h` consumer: `drawMuffin(c, cx, cy, r, frame, lift)`
  in a new `ui/muffinseal.cpp` (or folded into `seal.cpp`). Blits the indexed
  bitmap through its colour table, clipped to the circle, applying the
  heartbeat/`searchLift` brightness the same way the seal does (scale each
  pixel toward `VOID` for a dim, toward white for a lift) so candle/pulse
  searches can light it.
- **Scan screen** (`ui/screen_scan.cpp`) branches the centrepiece by theme:
  UMBRAVOX → heptagram, BAPH0MAU5 → `drawSeal`, COSMIC MUFFIN → `drawMuffin`.
- Compiles on host too (it's in `ui/`), so the sim renders it and `regress`
  covers it.

## Sweeps

Add six values to `enum Sweep` (`ui/model.h`): `SW_GALAXY, SW_ORBIT,
SW_STARFALL, SW_PULSAR, SW_COMET, SW_AURORA`, before `SW_COUNT` (now 11).

- **Mask widening (required):** `Row.sweeps` and `pal::sweepMask()` are
  `uint8_t` (8 bits); 11 sweeps need more. Widen both to `uint16_t`. Every
  theme row's mask literal extends accordingly.
- **Implementation:** each new mode gets its arm in `drawSearch`,
  `searchTouches`, `searchMarkLum`, `searchLift`, `pulseRing` (`ui/sweep.cpp`),
  drawn only from `pal::SWEEP/SWEEP_HI/WITCH/ICE/MAGENTA/GREEN/BONE` so it
  recolours per theme with no per-theme code. (Pulsar reuses the pulse-ring
  detection maths; the others find by bearing like the radar, or don't find and
  just animate, matching their motion.)
- **Availability:** all three themes offer all six new sweeps (mask bits set in
  every row). The existing gating still holds for the old modes: **gaze** needs
  eyes, so it stays off UMBRAVOX; **candle** lights a bitmap centrepiece, so it
  is offered by BAPH0MAU5 and now Cosmic Muffin, not UMBRAVOX.

## Alerts

Add `drawAlertSupernova/Vortex/CometStrike/LockOn` to `ui/screen_scan.cpp`
(peers of `drawAlertSummoned`), each self-contained (no seal figure needed).

- New `Settings.alertStyle` (uint8_t): which alert animation plays. Only read
  for the Cosmic Muffin theme; UMBRAVOX/BAPH0MAU5 ignore it and keep their
  existing alert. `drawAlert` dispatches: UMBRAVOX → ward, BAPH0MAU5 →
  `drawAlertSummoned`, COSMIC MUFFIN → one of the four by `alertStyle`.
- Selectable on-device: a cell on the GRIMOIRE/settings menu cycles
  `alertStyle` (shown only when the active theme is Cosmic Muffin), mirroring
  how the theme and sweep settings are chosen today.
- The threat name + confidence colour-coding (`alertConf` → red/amber/witch)
  carries over unchanged.

## Settings / persistence / clamps

- `ui/model.h`: `Settings.alertStyle = 0` added. `theme` now ranges 0..2,
  `sweep` 0..`SW_COUNT-1`.
- `ui/nav.cpp`: `clampSettings` clamps `theme < TH_COUNT`, `sweep < SW_COUNT`,
  `alertStyle < ALERT_STYLE_COUNT`; `settingsEqual` includes `alertStyle`; the
  theme/sweep cyclers reach the new values; the GRIMOIRE gains the alertStyle
  cell.
- `device/prefs.cpp`: persist `alertStyle` field-by-field in the "aether" NVS
  namespace, like the other settings; load at boot.
- `sim/test_settings.cpp`: the no-padding canary (`sizeof(Settings)`) updates by
  exactly the one new byte; the test documents the new size.

## Simulator & tests

- Sim: `--theme 2` selects Cosmic Muffin; `--sweep 5..10` the new sweeps;
  a `--alert <n>` flag (or reuse `--screen`) previews each alert style. `demo.cpp`
  gains a Cosmic Muffin demo state so all screens render in the new theme.
- `make test`: settings canary + nav clamp cover the new fields.
- `make regress`: rebaseline — the new theme adds rendered screens; regenerate
  the baseline MANIFEST after visually confirming the nine screens in the new
  theme.
- Hardware: flash the CYD, eyeball the theme, each sweep, and each alert
  (`TEST` console cmd already walks the alert card).

## Build order (for the plan)

1. Palette: widen the mask type, add the `TH_COSMICMUFFIN` row + enum +
   `themeName`. (Theme selectable, no new art yet — old centrepiece/sweeps.)
2. `gen_muffin.py` → `ui/muffin.h`; `drawMuffin`; scan-screen centrepiece branch.
3. Sweeps: enum + `sweep.cpp` arms + masks; sim `--sweep`.
4. Alerts: four draw fns + `alertStyle` setting + dispatch + menu cell + NVS.
5. Sim/demo/tests/regress; hardware pass.

## Decisions made here (builder's call, flag if wrong)

- **Alerts are Cosmic-Muffin-only and selectable** (not added to the other two
  themes). The request said *sweeps* go cross-theme; alerts read as the new
  theme's signature set, so they live there. Reversible later.
- **Six cosmic sweeps are additive** (11 total), not replacements for the
  existing five.
- **`alertStyle` is a new persisted byte**, chosen on the settings menu only
  while Cosmic Muffin is active.
- **Full-colour bitmap centrepiece** via a generated `ui/muffin.h`, rather than
  forcing the muffin through the existing 4-level ramp (which would flatten its
  colour).
