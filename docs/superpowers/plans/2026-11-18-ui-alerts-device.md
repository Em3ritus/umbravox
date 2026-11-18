# UI Alerts (device side, plan 5a) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

> **Approved by the owner 2026-11-18** (all four decisions; execution inline, one final review).

**Goal:** A layout theme places and styles the alert card for each level, may play a picture in place of the rite,
and may make other pieces flash, pulse or shake during an alert; the CYD engine build gets the alert takeover.

**Architecture:** The look (`uieng::BoardLook`, plan 4) gains three alert cards and a per-piece effect; the parser
fills them and `checkLook` enforces the readability and cost rules, so the device never trusts the designer. Drawing
keeps today's takeover (`scan_alert.h`) and gives it the card's rect, font and picture; effects are a post-process
on a piece's rect after the scene is composed and before the card is drawn, so no piece's drawing code changes.
The presenter repaints the card's moving parts and the effect rects. Designer controls, alert preview and the speed
meter are plan 5b.

**Tech Stack:** C++17 in `ui/` (no FPU on the C5), host tests in `sim/`, PlatformIO.

**Spec:** `docs/superpowers/specs/2026-11-17-ui-overhaul-design.md` (Goals: alerts; Pieces: `alert.card.*`,
`alert.clip`, effects; Device behaviour: "Alerts stay readable"; Speed budget: CYD takeover, device caps).

## Decisions made here (flag any that are wrong)

1. **Alert pictures are `.art` files** (`a0`-`a15`, animated or still, 4-bit, never scaled), not a new video format
   and not `radar.vid`. Reuses plan 4's loader, limits and designer GIF conversion; costs: 16 colours, and a card
   picture shares the board's 393,216-byte art budget.
2. **Cards are not layout pieces**: they live in the look (`BoardLook.card[3]`), so the overlap, hide and 44 px rules
   of `resolve` do not apply to them (a card is an overlay; tap anywhere dismisses, as today).
3. **Effects are post-processes** on the piece's rect: flash and pulse tint the rect toward the alert's edge
   colour; shake shifts the rect's rows sideways by up to 2 px, filling the gap with the pixel at the edge. Speed:
   slow 1000 ms, medium 500 ms, fast 250 ms per cycle. Effects show at LOW and MED (and at HIGH only outside a HIGH
   card smaller than the screen).
4. **Device caps** (checkLook, layout skipped if broken): effects on at most 4 pieces on the Waveshare, 2 on the
   CYD; a card picture at most `animAreaMax(W)` pixels (25,600 / 10,000) and no larger than its card; card font
   at least 7 px tall (`h * scale`, the built-in 5x7's height); card at least 240x120 (Waveshare) / 160x96 (CYD).

## Global Constraints

- Built-in themes on the Waveshare and on the legacy CYD UI: pixels byte-identical (`test_engine_pixels`,
  `make -C sim regress`). The CYD engine build's alert frames change deliberately (Task 5 lists each changed hash).
- No new internal RAM: `.dram0.bss` of `ws-c5-35-engine` stays <= 94,720; new look fields live in `PackLayout`
  (PSRAM, x12 packs: state the growth in bytes in the Task 1 commit).
- `layout.json` keys this firmware does not know stay ignored; older firmware ignores the new keys (no format bump).
- Commit trailers: `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>` and the session's `Claude-Session:`.
- New `sim/` binaries go in `.gitignore` before `git add`.

## Review Focus

1. A card font that is tall enough at scale 1 but `fitScale` picks a smaller scale for a long label: the minimum
   must hold after fitting. Test: Task 2 `card_font_min_after_fit`.
2. A LOW card rect outside the hero (e.g. over the count tiles): the presenter must repaint the card rect, and the
   scene under a LOW card must still draw outside it. Test: Task 2 `low_card_outside_hero`.
3. An effect piece that a HIGH full-screen card covers: nothing drawn, no repaint cost. Test: Task 4 `effect_under_high`.
4. A card picture whose file did not load (selection-time load failed): the rite plays, as with no picture.
   Test: Task 3 `clip_unloaded_falls_back`.
5. Alert ends mid-effect: the piece repaints to its normal look on the next frame. Test: Task 4 `effect_clears`.

---

### Task 1: Parse and check alert cards and effects

**Files:**
- Modify: `ui/layout_engine.h` (`PieceStyle` + `effect`, `speed`; new `struct AlertCard { Rect r; int8_t font = -1;
  uint8_t scale = 0; int8_t clip = -1; }`; `BoardLook` + `AlertCard card[3]` (index 0 LOW, 1 MED, 2 HIGH; `r.w == 0`
  = the built-in rect); `enum Effect : uint8_t { FX_NONE, FX_FLASH, FX_PULSE, FX_SHAKE }`, `enum FxSpeed : uint8_t
  { SP_SLOW, SP_MEDIUM, SP_FAST }` (default medium))
- Modify: `ui/themelayout.cpp` (piece object keys `effect`, `speed`; board keys `alert.card.low|med|high` as objects
  with `rect` (required), `font`, `scale`, `clip`; `checkLook` rules of Decision 4 plus "card on screen"),
  `docs/theme-packs.md` (layout.json section: the keys and the refusal reasons)
- Test: `sim/test_themelayout.cpp`

**Interfaces:**
- Produces: `uieng::AlertCard`, `BoardLook::card[3]`, `PieceStyle::effect/speed`, `Effect`, `FxSpeed`;
  `inline uieng::Rect scanr::alertCardRect(int W, int H, int level)` is Task 2's.
- Fonts and pictures named by a card count in `fontsUsed`/`artsUsed` (so they load on selection).

- [ ] **Step 1: Failing tests:** `card_parsed` (all four keys read into `look[0].card[1]`), `card_default` (no key →
  `r.w == 0`), `effect_parsed` (`"effect":"shake","speed":"fast"` on `count.ble`), `effect_bad_value_refused`,
  `card_off_screen_refused`, `card_too_small_refused` (239x120 Waveshare, 159x96 CYD), `card_font_too_small_refused`
  (5 px font, scale 1), `card_clip_too_big_refused` (art larger than the card; area over `animAreaMax`),
  `too_many_effects_refused` (5 on Waveshare, 3 on CYD), `card_refs_marked_used`.
- [ ] **Step 2:** `make -C sim test_themelayout && ./sim/test_themelayout` → FAIL (fields not declared).
- [ ] **Step 3:** Implement; keep the 16,384-byte and depth-4 limits.
- [ ] **Step 4:** Test passes; `make -C sim test` exit 0; `npm test` passes (the designer reads `limits.json`; no
  change expected). Commit `feat(layout): alert cards and piece effects in layout.json`.

### Task 2: Cards on the Waveshare: rect, font, scene underneath

**Files:**
- Modify: `ui/scan_render.h` (`composeStrip` ~1595), `ui/scan_alert.h` (`alertText` font scope), `ui/scan_present.h`
  (alert regions ~438-470 use the card rect)
- Test: `sim/test_alert_takeover.cpp`, `sim/test_scan_present.cpp`

**Interfaces:**
- Produces: `inline uieng::Rect scanr::alertCardRect(int W, int H, int level)` — level 1 (LOW/MED by conf) or 2;
  the look's card rect when set, else today's: level 1 the hero, level 2 `{0,0,W,H}`.
  `inline const fontpack::Font* scanr::alertCardFont(int W, int level)` — the card's font when loaded and tall
  enough, else null (built-in 5x7).
- `composeStrip`: level 2 with a full-screen card keeps today's "skip the scene"; any card smaller than the
  screen composes the scene first (as level 1 does today), then the takeover in the card rect. The card's words use
  `alertCardFont` via a `FontScope` clipped to the card; `fitScale` never goes below the 7 px minimum.

- [ ] **Step 1: Failing tests:** `builtin_card_unchanged` (no look: frames equal today's at age 0, 400, 3000 for
  LOW/MED/HIGH — hashes recorded before the change), `card_rect_used` (LOW card 300x160 at (90,80): nothing outside
  changes vs no alert except under the card), `low_card_outside_hero`, `high_card_partial` (HIGH 400x240: scene
  visible outside), `card_font_used`, `card_font_min_after_fit` (a 7 px font, a 30-letter label: rendered line
  height >= 7), presenter `card_regions` (settled LOW alert repaints only inside the card rect).
- [ ] **Step 2:** Run `make -C sim test_alert_takeover test_scan_present` and both binaries → FAIL.
- [ ] **Step 3:** Implement.
- [ ] **Step 4:** Tests pass; `test_engine_pixels` and `make -C sim regress` unchanged; `make -C sim test` exit 0.
  Commit `feat(ui): the alert card takes the theme's rect and font`.

### Task 3: A picture in place of the rite

**Files:** Modify `ui/scan_alert.h` (`drawAlertTakeover`, `alertRegions`); Test `sim/test_alert_takeover.cpp`

**Interfaces:**
- Consumes: `AlertCard::clip`, `sizedArt`-style lookup via `assets::art`, `artpack::frameAt(a, age)`, `artInto`.
- When the card's picture is loaded: it is drawn centred in the rite's square (`alertGeom`), frame by alert age,
  instead of `riteFor(...)`'s drawing; the fill, strobe, HIGH border and words stay. `alertRegions` then returns the
  picture's rect in place of the rite square.

- [ ] **Step 1: Failing tests:** `clip_replaces_rite` (rite pixels absent, picture pixels present at its rect),
  `clip_frames_by_age`, `clip_words_on_top` (the label's pixels are the same as with no picture once settled),
  `clip_unloaded_falls_back`, `clip_regions`.
- [ ] **Step 2:** Run → FAIL. **Step 3:** Implement. **Step 4:** Pass; suite exit 0; regress unchanged. Commit
  `feat(ui): an alert card may play a picture instead of the rite`.

### Task 4: Effects during an alert

**Files:** Modify `ui/scan_render.h` (after `composeStripScene`, before the takeover), `ui/scan_present.h`
(effect rects while an alert is active); Test: `sim/test_alert_takeover.cpp`, `sim/test_scan_present.cpp`

**Interfaces:**
- Produces: `inline void scanr::tintRect(S& s, uieng::Rect r, uint16_t col, int num, int den)` (blend every pixel
  of `r` inside the strip toward `col` by num/den, integer `ui::Canvas::mix`), `inline void scanr::shakeRect(S& s,
  uieng::Rect r, int dx)` (rows of `r` shifted by dx, |dx| <= 2, gap filled with the edge pixel),
  `inline int scanr::fxPhase(uint32_t ageMs, uint8_t speed)` (0..1023 over one cycle).
- Flash: tint 1/2 during the first half of each cycle. Pulse: tint `(512 + isin(phase)/2)/1024 * 1/2`. Shake: dx
  = -2, -1, 0, 1, 2, 1, 0, -1 stepped through the cycle. Colour: the alert's `edge` (`safeEdge`, as the card uses).

- [ ] **Step 1: Failing tests:** `flash_tints`, `pulse_tints_by_phase`, `shake_shifts`, `effect_only_in_rect`,
  `effect_under_high` (full-screen HIGH: identical to no effect), `effect_clears` (frame after the alert ends equals
  a never-alerted frame), `no_alert_no_effect`, presenter `effect_regions` (each frame of an active alert repaints
  the effect pieces' rects).
- [ ] **Step 2:** Run → FAIL. **Step 3:** Implement. **Step 4:** Pass; suite exit 0; regress unchanged. Commit
  `feat(ui): pieces flash, pulse or shake during an alert`.

### Task 5: The CYD engine build gets the takeover

**Files:** Modify `ui/scan_render.h` (`composeStrip`: drop `W >= 400`), `ui/scan_present.h` (W<400 branch: alert
level changes and settled-alert rows; `sceneParts` `p.alert` no longer `W >= 400` only), test baselines that pin
CYD engine alert frames; Test: `sim/test_alert_takeover.cpp`, `sim/test_scan_present.cpp`

- [ ] **Step 1: Failing tests:** `cyd_takeover_low_med_high` (320x240: the card draws; HIGH full screen),
  `cyd_settled_rows` (a settled alert repaints only the rows of `alertRegions`), `cyd_tap_dismiss_unchanged`.
- [ ] **Step 2:** Run → FAIL. **Step 3:** Implement; then run `make -C sim test` and list every test whose pinned
  hash changes: each must be a 320-wide engine alert frame. Update those pins in this commit and list them by name
  in the message. Anything else changing is a bug.
- [ ] **Step 4:** Suite exit 0; `make -C sim regress` unchanged (legacy CYD and Waveshare). Commit
  `feat(ui): the CYD engine build shows the alert takeover`.

### Task 6: Hardware gate and docs

- [ ] **Step 1:** `pio run` all four envs; `.dram0.bss` check; boot probe on both boards (12/12, heaps as now,
  Waveshare UMBRAVOX ~19 ms, CYD draw ~6 ms).
- [ ] **Step 2:** `docs/theme-packs.md`: alert cards, pictures, effects and their limits; `docs/design-notes.md` one
  paragraph on effects as post-processes.
- [ ] **Step 3:** the alert look on hardware: the console `TEST` command (and the OMENS test button) already walks
  HIGH/MED/LOW test alerts, 5 s each (`device/main.cpp` `TEST_SEQ`). With a test pack that sets cards and effects,
  send `TEST` on both boards, check that the boards stay up (no reset, heap as before) and log frame times during the
  takeover; the look itself is the owner's to judge by eye.
