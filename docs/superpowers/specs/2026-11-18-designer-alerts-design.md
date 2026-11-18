# Designer alerts (plan 5b) and the speed meter (plan 5c): design

Date: 2026-11-18. Parent spec: `2026-11-17-ui-overhaul-design.md` (sections "Designer" and "Speed budget").
Plan 5a put alert cards, card pictures and effects on the device; this is the designer side, then the meter.

## Intent

A theme author designs the alert in the designer: where each level's card sits, its font and picture, and which
pieces flash, pulse or shake. They see it play on the live preview, are warned where a choice behaves in ways they
might not expect (a card over STOP, a font too small), and, after 5c, cannot install or share a layout the board
cannot draw fast enough.

## Split (owner, 2026-11-18)

- **5b**: alert editing, alert preview and Play, warnings, plus the 5a deferred fixes. No hardware needed.
- **5c**: the speed meter. Calibrated on both real boards **before** it ships (owner: "real boards first").

## Approaches (5b)

1. **Preview through the device code (chosen).** The designer edits `layout.json` only; the canvas, the alert
   and Play are drawn by the wasm build of the device drawing code (`uv_render_engine`, `ui/preview_ws.h`, whose
   `frame * 20 ms` is already the alert's age).
2. JS overlay: draw cards and effects in the browser on top of the canvas. Faster to write, but drifts from the
   board, and the parent spec makes the preview the device code. Rejected.

Editing UI, three options shown as mockups; owner picked A:
- **A (chosen): a mode switch above the screen**: `Scan | Alert LOW | Alert MED | Alert HIGH | ▶ Play`.
- B: all three cards as outlines on the scan canvas (the HIGH card hides every piece under it). Rejected.
- C: a separate Alert step (effects set away from their pieces, no scan behind the card). Rejected.

## 5b design

- **Mode switch** on the Layout step, per board (Waveshare 480x320 and CYD engine 320x240). In **Scan**, editing
  works as today. In an **Alert** level, the preview shows that alert over the scan; the scan pieces are dimmed and
  not selectable; only that level's card is (move, resize, nudge, exact numbers).
- **Card panel**: rect, font (built-in or uploaded, as for pieces), scale, picture (`clip`, the existing art
  picker; counts against the art budget). Writes `alert.card.<low|med|high>` as documented in
  `docs/theme-packs.md` ("Alerts: cards and effects"). No card for a level = the built-in look; the panel offers
  "Use the built-in card".
- **Effects**: each piece's side panel gains "During an alert": effect `none | flash | pulse | shake`, speed
  `slow | medium | fast`, written as `effect`/`speed` on the piece. The designer enforces the device caps (4 effect
  pieces on the Waveshare, 2 on the CYD): the control is disabled at the cap with the reason, rather than the
  device dropping extras.
- **Play**: plays the current level from alert age 0 by stepping the preview frame, looping, until stopped or the
  mode changes. In Scan mode, Play is hidden.
- **Warnings** (checks list and inline under the card panel):
  - a LOW/MED card covering STOP, MENU or any nav button: "While the alert shows, a tap there dismisses the alert
    first" (owner, 2026-11-18; a warning, not an error);
  - card font under 7 px (the font's own height, as the device rule): says the device falls back to the built-in
    card font;
  - a picture larger than its card: says the device falls back to the rite;
  - the existing errors stay errors (card off screen, below 240x120 / 160x96, unknown effect or speed).
- **5a deferred fixes**:
  - `docs/theme-packs.md`: card font rule is the font's height alone (not height x scale); rewrite the dismiss
    bullet so it does not contradict itself;
  - device: skip effect repaints for a piece wholly under a non-whole-screen card (test first; render baselines
    must stay unchanged);
  - a repo test for the presenter's pending path (`sim/test_alert_takeover.cpp`).

### Constraints

- Built-in themes and packs without alert keys render byte-identically (`make -C sim regress` unchanged).
- Designer and device rules stay one set: limits come from `limits.json` / the wasm `uv_layout_check`, not
  duplicated constants in JS.
- `npm test` covers the mode switch, card/effect model edits, caps and each warning; the wasm parity test covers
  an alert frame with a card, a picture and an effect.

### Left to the builder

Exact DOM and styling, how Play's frame rate is paced, warning wording beyond the owner's STOP sentence, and
whether the CYD cap lives in `limits.json` or comes from the wasm bridge (one source either way).

## 5c design (decided now, built after 5b)

- **Counters** in the shared drawing code: pixels drawn and pixels pushed, per frame. The same code runs in wasm and
  on the board, so the counts agree.
- **Calibration first** (owner): a set of test packs (small, large, every effect, full-screen card) and a probe
  script in `tools/` (like `bootprobe.py`) that loads each on both boards, runs `TEST`, and logs counters and
  measured ms per frame. Fit per board: `ms ≈ a·drawn + b·pushed + c`. The fit ships in the designer; the probe stays
  so the fit can be redone after firmware changes.
- **Limits** (owner approved my proposal, 2026-11-18), judged on the **worst single frame**:
  - alerts, both boards: <= 250 ms (>= 4 frames/s; ALERT FX measured 190 avg / 221 worst and looked right);
  - scan, CYD: <= 14 ms (today's firmware regression line);
  - scan, Waveshare: the built-in theme's calibrated time + ~15%, set from the calibration session;
  - amber warning from 80% of a limit; over 100% blocks Install and Share.
  - These are starting values; the calibration session may adjust them.
- **Meter**: always visible on the Layout step, one row per board, scan and the worst alert level.
