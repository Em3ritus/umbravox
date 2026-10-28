# Portrait mode, first version -- design

Date: 2026-10-28. Branch `features`. Status: approved by the owner (scope and
the ORIENTATION setting); architecture below is the implementer's.

## Goal

A new ORIENTATION setting (LANDSCAPE, the default, or PORTRAIT). In portrait
the NM-CYD-C5's 240x320 ST7789 is driven in its native portrait rotation and
the UI draws a tall 120x160 canvas at 2x, which fills the glass exactly. The
first version lays out the CORE screens only; every other screen shows a
placeholder that always leads back.

## Scope

Laid out for portrait:

- SCAN: header (menu chip, wordmark, band badge), the three count tiles in a
  row, the dial panel with the theme's centrepiece (heptagram, seal or
  emblem) at the theme's own radii, the spectrum strip when the theme has
  one, and a two-row footer: GPS label and node count, then a full-width
  START/STOP (or NO CARD) button.
- The alert card over the dial panel, every rite: the UMBRAVOX ward,
  BAPH0MAU5's summoning, COSMIC MUFFIN's four (SUPERNOVA, VORTEX, COMET,
  LOCK-ON) and a pack's named rite. The rites already take the card
  rectangle as arguments; the portrait card is a different rectangle.
- GRIMOIRE: two columns of four plus a full-width LABELS row.
- ATTUNE page 1 and page 2, with the new ORIENT row on page 2.
- FILES: list, selection, delete confirmation, active (REC) row, no card.

Placeholder (portrait only): CODEX, SIGHTING, DOMAIN, MESH, OMENS, ECHOES,
ASPECT. A card with the screen's name, "PORTRAIT LAYOUT COMING", a hint
naming ATTUNE page 2, and a large BACK button (the header back chip works
too) that returns to the screen the user came from (SCAN or GRIMOIRE).

The way back to landscape from anywhere: BACK (placeholder) -> menu chip ->
GRIMOIRE -> ATTUNE -> PAGE 1/2 -> ORIENT. ATTUNE is a core screen, so this
path always exists.

## Architecture

- `Settings.orient` takes the reserved `spare` byte (Settings stays 14 bytes
  with no padding). `ORIENT_LANDSCAPE = 0` (default), `ORIENT_PORTRAIT = 1`.
  `clampSettings` maps anything else -- and PORTRAIT on a board that cannot
  show it (`ui::setPortraitAllowed(false)`) -- to LANDSCAPE. NVS key
  `orient`, saved and loaded individually like every other field.
- `Canvas` keeps its 19,200-pixel buffer but gains runtime `w,h` (stride `w`).
  `W,H` stay the constexpr landscape defaults, so every landscape screen is
  untouched; the helpers (pixel, text, vignette, scanlines, emblem blitter)
  clip to and stride by `w,h`.
- `ui::drawScreen(c, s, d)` is the one dispatcher (device and simulator): it
  sizes the canvas from `s.set.orient` and calls the landscape screen or its
  portrait counterpart (`ui/portrait_*.cpp`, layout in `ui/portrait_layout.h`,
  namespace `ui::pl`).
- Shared drawing is reused, never forked: the dial (`drawDialAt`), the alert
  card (`drawAlertIn`, which the landscape `drawAlert` now calls with its old
  rectangle) and the spectrum (`drawSpectrumAt`).
- Touch: `navTouch` routes taps through pure portrait hit-zone functions when
  `set.orient` is PORTRAIT (separate from the landscape ones, same helpers).
- Dirty rectangles in portrait come from a pixel diff against a shadow copy of
  the last frame (`ui/framediff`), kept in PSRAM on the board. It is correct
  by construction for every portrait screen, so no portrait screen has
  hand-written dirty logic to get wrong. `ui::orientationChanged()` resets the
  shadow and every screen's prev State; the device also forces a full blit.
- Device: `gfx->setRotation()` switches the panel live (no reboot) when the
  setting changes; blit and touch mapping follow the runtime geometry. The
  XPT2046 mapping is derived from the measured landscape calibration through
  the panel's native frame (`device/touchmap.h`, host-tested), so portrait
  needs no new calibration. `board::ROTATION_PORTRAIT` (0) is one line to
  change; `board::FLIP_180` turns both orientations over.
- Waveshare 3.5" (ws-c5-35): LANDSCAPE ONLY. Its panel is 320x480 native and
  its geometry/status strip are landscape-shaped; the setting is gated off
  there (`board::HAS_PORTRAIT = false`) and the ORIENT row reads
  "LANDSCAPE ONLY". This is the safe choice.

## Emblem rule

Theme-pack emblems (<= 96x96) are never clipped by a rectangle: they are
scaled (nearest) to the dial's field diameter (2 x R_FIELD) and masked to that
circle, exactly as in landscape. Portrait uses the theme's own radii, which
fit the 116-px-wide portrait dial panel, so the emblem appears at the same
size as in landscape. In the alert card the emblem's radius is capped at
min(card w, card h)/2 - 2. The pack format and limits are unchanged.

## Deferred

Portrait layouts for CODEX, SIGHTING, DOMAIN, MESH, OMENS, ECHOES, ASPECT;
portrait on the Waveshare board; a portrait preview in the wasm designer
(the bridge still builds and renders landscape identically); scrolling on
placeholder screens.
