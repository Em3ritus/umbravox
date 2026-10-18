# CVLT OF BAPH0MAU5 — a second theme, selectable on the device

Design, 2026-10-18.

## Intent

AETHERSCAN has one look. This adds a second — built around a Baphomet sigil
the user supplied — and makes the choice a setting on the CYD rather than a
build-time decision. Both themes ship. Both stay reachable.

The dial's search animation becomes a setting too, with all five variants
kept, because the radar sweep is the most instrument-shaped thing left on the
screen and which one reads best is a matter of taste rather than correctness.

Success means: a user can change theme and sweep on the glass, the choice
survives a power cycle, and AETHERSCAN renders exactly as it does today when
it is the one selected.

### What this is not

Not a redesign of any screen other than AETHERSCAN's dial. CODEX, DETAIL,
DOMAIN, MESH, OMENS, ECHOES and ATTUNE are recoloured by the palette switch
and otherwise untouched. Their layouts do not become theme-dependent.

## Architecture

Four seams, independent of each other. Each can be built and verified alone.

### 1. The palette becomes runtime

`ui/palette.h` changes from `constexpr uint16_t VOID = rgb(...)` to
`extern uint16_t VOID;`. A new `ui/palette.cpp` holds one table row per theme
and a `setTheme(uint8_t)` that copies a row into the globals.

**Every one of the 239 `pal::` references across the nine screen files keeps
its exact spelling and is not edited.** This was verified, not assumed: the
conversion was built against the real tree, compiled clean under
`-Wall -Wextra`, and theme 0 rendered byte-for-byte identical to the current
output (md5 `2886dd6c61e38ada6f4f3c00afd35fcb` both before and after).

The audit behind that: no `pal::` value is used in a context requiring a
compile-time constant. Every `const uint16_t x = pal::…` is a local inside a
function; the `case BAND_24: return pal::MAGENTA;` hits are return values, not
case labels. There is no `static_assert`, no array initializer and no
namespace-scope table built from a palette value.

A table rather than a switch, so that adding a theme is adding a row — a row
cannot silently omit a colour the way a hand-written assignment can.

`rgb()` stays `constexpr`; it builds the table.

#### Colours the second theme needs that the first does not

BAPH0MAU5 draws a bitmap seal and a witchfire accent, so the `Theme` row
carries six fields AETHERSCAN has no use for:

| Field | Purpose | AETHERSCAN fills it with |
|---|---|---|
| `SWEEP`, `SWEEP_HI` | the search beam | `WITCH`, `WITCH_HI` |
| `LIVE`, `LIVE_HI` | parts of the seal that are alight | `WITCH_HI`, `BRIGHT` |
| `RAMP[4]` | the seal bitmap's four anchor levels | `VOID`, `EDGE`, `DIM`, `BRIGHT` |

Giving AETHERSCAN sensible values rather than leaving them undefined means a
future theme that wants a bitmap centrepiece inherits a working default. A
`RAMP` of four copies of `VOID` would satisfy the compiler and render
nothing, which is exactly the kind of placeholder that survives to a release.

#### The two palettes

AETHERSCAN keeps today's values from `ui/palette.h` exactly. BAPH0MAU5:

```
VOID     06030C    DIM      7E56BC    WITCH     9B62F0
PANEL    130921    TEXT     CAB0EA    WITCH_HI  D6BCFF
GRID     1D1133    BRIGHT   FFFAFF    BONE      E4DAF6
EDGE     3C2062
EDGE_LIT 743CB4    MAGENTA  3BFF6E  (2.4 GHz — neon green)
                   ICE      F2EEFF  (5 GHz — white)
SWEEP    3BFF6E    GREEN    9B62F0  (BLE — violet)
SWEEP_HI C4FFD4
LIVE     24C450    AMBER    F59E0B
LIVE_HI  3BFF6E    RED      D42420

RAMP  = { 06030C, 3E3055, B6AECC, FFFCFF }
```

The band names stay `MAGENTA`/`ICE`/`GREEN` even though BAPH0MAU5 paints them
green/white/violet. Renaming them would mean editing all nine screen files,
which is the cost this whole seam exists to avoid. The names mean "2.4", "5"
and "BLE"; a comment in `palette.h` says so.

The organising rule survives in both: chrome is the theme's own hue, ceremony
is `BONE`, and `MAGENTA`/`ICE`/`GREEN` are only ever a reading. A theme that
broke that would stop being a reskin and start being a different instrument.

### 2. The dial's geometry becomes runtime

The same trick, with a far smaller blast radius: the dial constants are read
in exactly two files, `ui/screen_scan.cpp` and `ui/nav.cpp`, about thirty
references between them.

`DIAL_Y`, `DIAL_H`, `DIAL_CX`, `DIAL_CY`, `R_FIELD`, `R_RUNES`, `R_BOUND`,
`R_STAR`, `SPEC_Y`, `SPEC_H` move out of `ui/layout.h` and into the theme
table, set by `setTheme`. Everything else in `layout.h` stays `constexpr`,
including `BODY_H` and `ROWS`, which are derived at compile time and have
nothing to do with the dial.

`DIAL_CX` is currently derived (`COL_R_X + COL_R_W / 2`). `COL_R_X` and
`COL_R_W` stay `constexpr`; `DIAL_CX` becomes a plain runtime value computed
in the table.

This is what lets both themes keep what suits them:

| | AETHERSCAN | BAPH0MAU5 |
|---|---|---|
| `DIAL_Y`, `DIAL_H` | 13, 70 | 13, 90 |
| `DIAL_CX`, `DIAL_CY` | 109, 48 | 109, 57 |
| `R_FIELD` | 22 | 33 |
| `R_RUNES` | 26 | 37 |
| `R_BOUND` | 31 | 42 |
| `R_STAR` | 20 | 31 |
| `SPEC_Y`, `SPEC_H` | 85, 16 | 0, 0 |
| spectrum strip | drawn | not drawn |

`R_STAR` is the radius of the inscribed figure, whatever that figure is: the
`{7/3}` heptagram on AETHERSCAN, the diamond and two triangles on BAPH0MAU5.
The name is kept rather than generalised so the two files that read it are
not touched more than they need to be.

`SPEC_H == 0` is the signal that a theme has no spectrum. `drawScan` skips
`drawSpectrum` when it is zero, and `nav.cpp`'s hit test for the spectrum
(which opens MESH) is skipped on the same condition.

The reason `layout.h` exists at all is that the code which draws a tile and
the code which decides whether a finger landed on it must agree. Making these
runtime *preserves* that property — `nav.cpp` reads the same names, so hit
tests follow the active theme without being told.

**BAPH0MAU5 has no spectrum strip, so on that theme there is no way to reach
MESH from the scan screen.** MESH remains reachable from GRIMOIRE, which is
how every other screen is reached. This is accepted rather than worked around.

### 3. The centrepiece

A branch in `drawDial`, selected by the active theme:

- **AETHERSCAN** — the existing heptagram, lattice, range rings and the
  sixteen 5×5 runes walking the band. Unchanged code.
- **BAPH0MAU5** — a procedural figure (diamond and two triangles) drawn
  behind a baked bitmap of the sigil's face, with eight 5×5 alchemical marks
  walking the band.

Their art carries the face; the geometry around it is drawn rather than baked.
Lines stay sharp, cost nothing to store, and — the reason that actually
matters — a drawn figure can answer to the state where a bitmap can only sit
there.

#### The asset pipeline

This is the part that is easy to get wrong and was got wrong twice. The source
is a *textured* drawing: every stroke is speckled inside and ragged at the
edge. Resampling it directly averages the speckle into mid-grey noise and the
face reads as mush at 50px.

The order of operations is the specification:

1. Trim the source to its content bounding box.
2. Threshold at 52/255 to solid line work — this kills the chalk speckle.
3. Mask to a radius fraction of the half-width: `0.45` for the face,
   `1.01` for the full seal.
4. Binary closing, 5×5 — closes the pinholes the texture left.
5. Binary dilation, 1×1 — so a stroke survives a 20× reduction.
6. Crop to the new bounding box.
7. Resize with **`Image.BOX`** (area average). **Never Lanczos** — it rings on
   hard edges and puts the noise back.
8. Scale by gain 1.15, clip, quantise to 16 levels, pack two pixels per byte.

A generator script produces the header; the header is not hand-edited. The
script lives in the repo so the asset can be regenerated when the source or
the target size changes.

Two assets:

| Asset | Size | Depth | Bytes |
|---|---|---|---|
| `FACE` | 50×50 | 4bpp | 1,250 |
| `BIG` (boot splash) | 112×112 | 4bpp | 6,272 |

4bpp rather than 2bpp. At this size the storage is irrelevant either way, and
sixteen levels are what let a one-pixel stroke sit at half intensity instead
of rounding to black or full chalk.

The generator also emits feature offsets from the bitmap's centre, so the
animation code never hardcodes a pixel position:

```
EYE_L  (-6, -4)    FLAME  ( 0, -18)
EYE_R  ( 6, -4)    PENTA  ( 0,  17)
```

Level 0 is transparent, so the drawn figure shows through behind the face
exactly as it does in the original artwork. The blit is clipped to
`R_FIELD - 1` so nothing escapes the scrying field.

#### Setting parts of the seal alight

A tint region recolours the seal's **own strokes** through a witchfire ramp
instead of the chalk ramp. Nothing is drawn on top of the artwork — an
overlaid green mark reads as a sticker; letting its own line work catch is
what makes it look like the thing is doing it.

```c
struct Tint { int x, y, r; bool dark; };
```

`dark` suppresses those pixels instead of lighting them, which is how the
blink works: the eyes are not covered, they simply stop being there.

### 4. The sweep

Five modes behind `s.set.sweep`, as a branch in `drawDial`, plus a helper that
decides which contacts the search is currently touching. Each mode needs its
own answer and they are not interchangeable.

| id | Mode | Origin | Finds by |
|---|---|---|---|
| 0 | Radar | centre | bearing, within 26° behind the beam |
| 1 | Gaze | the two eyes | bearing |
| 2 | Pulse | centre, expanding ring | **distance** — near things answer first |
| 3 | Star | centre, five limbs at 72° | bearing, any limb within 20° |
| 4 | Candle | none | nothing; contact age alone drives the fade |

The gaze is the one that fixes the original complaint. The light leaves the
face rather than the middle of the circle, so the seal is doing the looking
instead of sitting behind an instrument that looks on its behalf. Its beams
are walked and clipped to the field, because they start off-centre and cannot
simply be drawn to a radius.

**The gaze requires eyes, which AETHERSCAN does not have, and is not offered
there.** Decided 2026-10-18. The alternative considered and rejected was
emitting its beams from the heptagram's two upper vertices: it works, but
there is nothing at those points that reads as an eye, so the animation would
mean nothing on that theme.

Sweep availability is therefore per-theme. The ids stay global — a sweep is
the same sweep whichever theme draws it — and each theme row carries a
bitmask of the ones it offers:

| Theme | Offers | Mask |
|---|---|---|
| AETHERSCAN | radar, pulse, star, candle | `0b11101` |
| BAPH0MAU5 | all five | `0b11111` |

Two consequences for the implementation:

- The ASPECT `-`/`+` control steps to the next **available** id, skipping any
  the mask excludes. It must not step onto gaze on AETHERSCAN and bounce
  back, which reads as a broken control rather than an unavailable option.
- `clampSettings` falls back to radar when the stored sweep is not available
  on the selected theme. This fires whenever the theme changes with gaze
  selected, which is the same job it already does for every other stored
  value.

**The candle removes the notion of a fresh detection.** Nothing sweeps, so no
contact is ever "just found" and the hot/crossed highlight never fires. That
is the trade the mode exists to make, not a defect.

### The BAPH0MAU5 animation set

Carried over from the earlier review. These are the seal's behaviours and are
independent of which sweep is selected.

- **Heartbeat** — a double-thump envelope on the seal's lift, replacing a sine
  breath. A sine is what every interface does when it wants to look alive and
  it reads as a pulsing bulb; two quick beats and a longer rest is the shape of
  something with a heart in it.
- **Flame lick** — the tint patch above the horns grows and shrinks. At this
  size a flame is a handful of strokes disagreeing with each other.
- **The eyes answer a finding** — they catch when the search turns a contact
  up, and lean one pixel toward its bearing. Tying the flare to the sweep's
  *position* instead made the face strobe on a timer.
- **Pentacle beat** — one flare per channel dwell, which makes the seal the
  clock as well as the field.
- **Blink** — both eyes go dark for two frames roughly every 200, rare enough
  that it is caught out of the corner of the eye.
- **Possession** — the fire spreads with `alertConf`: flame, then eyes, then
  horns, then pentacle, then the whole face. The seal becomes the threat
  gauge, so a glance says how bad it is without reading a number.

Rejected: a ripple expanding from the centre on each finding. Two things
moving radially in one small circle, and the circle loses.

Possession may remove the need for a separate alert colour on this theme. That
is noted, not decided; `RED` stays in the table either way.

### 5. The ASPECT screen

A new screen, reached as the eighth GRIMOIRE tile. The grid is three columns
and currently holds seven entries across three rows, so slots eight and nine
are already free — this costs no new geometry. Rune 11 (crown) is unused by
the existing seven and is the tile's mark.

Body runs 13..102, ninety rows:

```
y 15..75    live dial preview, R_BOUND 30, via the real drawDial
y 78..89    THEME   -  AETHERSCAN  +
y 90..101   SWEEP   -  GAZE        +
```

The two rows reuse ATTUNE's `SET_MINUS_X` / `SET_PLUS_X` / `SET_BTN_W` /
`SET_BTN_H` geometry and its `button()` treatment, so the controls are the
same object in both places.

**The preview calls the real `drawDial`.** It cannot be a drawing of a dial;
it has to be the dial, for the same reason `layout.h` exists. The preview
renders into the canvas at the preview's centre and radius, with the *pending*
theme and sweep rather than the committed ones, so the user watches the sweep
run while choosing it.

The preview needs `drawDial` to take its centre and radii as arguments rather
than reading the globals directly. That refactor is part of this work.

Changing the theme repaints everything, so ASPECT must force a full-frame blit
on any change — the dirty regions of the old theme say nothing about the new
one. This is the same rule `navTouch` already applies on a screen change.

### 6. Settings and persistence

Two fields on `ui::Settings`:

```c
uint8_t theme = 0;   // pal::ThemeId
uint8_t sweep = 0;   // 0 radar, 1 gaze, 2 pulse, 3 star, 4 candle
```

`clampSettings` gains: `theme` ≥ `TH_COUNT` → 0; `sweep` not available on the
selected theme → 0.

`device/prefs.cpp` needs **four** changes, and the third is the one that bites:

1. `write()` — store both.
2. `load()` — read both, then `clampSettings` as it already does.
3. **`same()`** — compare both.
4. `VER` 3 → 4.

A field missing from `same()` means the debounce timer never fires and the
setting silently never persists. That file already carries a comment about a
bug of exactly this shape — the timer keying off the wrong comparison — so it
gets an explicit test rather than a careful read.

`VER` 4 discards any stored v3 blob and writes defaults, which is the existing
behaviour for an unknown version and is correct here: a v3 store has no theme
field to migrate.

## Verification

| Claim | How it is checked |
|---|---|
| AETHERSCAN is unchanged | `aethersim` output byte-identical to a pre-change baseline, every screen |
| Both themes compile | `-Wall -Wextra` clean, both sim and `pio run` |
| The switch works | sim renders each theme and sweep; no stale pixels |
| Settings persist | power cycle on hardware, then read back |
| `same()` is complete | host test: mutate each field, assert a save is scheduled |
| Sweep clamping | host test: select gaze, switch to AETHERSCAN, assert sweep falls back |
| Draw cost | **measured on the board, not estimated** |

That last row is the open risk. The C5 is single core, so packet capture and
drawing share one CPU and every redraw costs captures. The seal blit is
roughly 2,500 pixel tests against the rune ring's 400, and the star sweep
draws five tails where the radar draws one. Nothing here has been timed on
hardware. If the frame cost is unacceptable the fallbacks, in order, are:
cache the seal's static pixels, drop the star, or reduce the face to 2bpp.

## Out of scope for this work

- **The boot splash.** Designed and rendered, and the `BIG` asset is specified
  above, but it is a separate screen with its own sequence. Land it after the
  theme switch works.
- **Moving the spectrum to its own screen.** Not needed — AETHERSCAN keeps its
  strip, and BAPH0MAU5 simply has none.
- **A third theme.** The table makes one cheap; nobody has asked.
- **Per-theme fonts.** `font5x7` is shared and stays shared.

## Working without version control

This project is not a git repository and is staying that way until the work is
ready. That is a decision about where the code lives, but it changes how the
central claim of this spec gets checked.

"AETHERSCAN renders exactly as it does today" is verified by diffing against a
baseline. With no VCS there is nothing to diff against after the fact, so the
baseline has to be captured **before the first edit** and kept outside the
tree:

1. Build the simulator from the current tree.
2. Render every screen — `scan`, `codex`, `detail`, `domain`, `mesh`, `omens`,
   `echoes`, `grimoire`, `attune` — at a fixed frame.
3. Record their checksums alongside the PNGs.

Those checksums are the contract. Any later claim that AETHERSCAN is untouched
cites them.

**This has been done.** `private/baseline/` holds all nine screens at
`--frame 0 --scale 4`, their checksums in `MANIFEST.md5`, a byte-for-byte copy
of the pre-change `ui/` tree in `ui-pristine/`, and a `README.txt` with the
command to re-render and compare. `scan.png` is
`2886dd6c61e38ada6f4f3c00afd35fcb`, matching the palette proof build.

The rollback copy is in `private/` rather than a temp directory on purpose: a
scratch directory is session-scoped and can be cleared, and without that tree
there is no way back from a bad edit across nine files. This work touches all
nine.
