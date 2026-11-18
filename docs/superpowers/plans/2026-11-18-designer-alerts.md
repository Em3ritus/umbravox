# Designer alerts (plan 5b) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** The designer's Layout step edits alert cards (rect, font, scale, picture per level) and per-piece alert
effects, plays the alert on the live preview, and warns about choices the device handles quietly; plus the plan-5a
deferred device and doc fixes.

**Architecture:** The preview stays the device drawing code in wasm (`uv_render_engine` / `uv_render_ws`, alert age =
frame x 20 ms); the designer only edits `layout.json`. The model (`web/designer/js/layout.js`) gains `cards` and
piece `effect`/`speed`; DOM-free logic for the alert mode lives in a new `web/designer/js/layout-alert.js`; `app.js`
wires it. Device rules the designer needs (effect cap, built-in card rect) come from the wasm bridge, never retyped.

**Tech Stack:** C++ (ui/, sim host tests), emscripten wasm bridge, plain ES modules, `node:test`.

**Spec:** `docs/superpowers/specs/2026-11-18-designer-alerts-design.md` (5b part), with the parent
`docs/superpowers/specs/2026-11-17-ui-overhaul-design.md`. Key format: `docs/theme-packs.md` "Alerts: cards and effects".

## Global Constraints

- Built-in themes and packs without alert keys render byte-identically: `make -C sim regress` prints
  "UMBRAVOX unchanged (landscape and portrait)".
- Designer and device rules are one set: caps, defaults and verdicts come from the wasm bridge / `limits.json`.
- Effect caps: 4 pieces on the Waveshare, 2 on the CYD. Card minimum 240x120 (Waveshare) / 160x96 (CYD).
- Card font minimum: the font's own height >= 7 px (`scanr::CARD_FONT_MIN_PX`), scale not counted.
- Card picture plays only if no larger than the card and `w*h <= ANIM_AREA_MAX[board]` (25600 / 10000).
- Owner's STOP warning text (verbatim meaning): "While the alert shows, a tap there dismisses the alert first."
  It is a warning, never an error.
- Commits: author `Em3ritus <152051018+Em3ritus@users.noreply.github.com>`; the ONLY trailer is
  `Co-Authored-By: Claude <noreply@anthropic.com>` (no model name, no Claude-Session line).
- Tests run as `make -C sim X && ./sim/X` (never a grep of build output); full `make -C sim test` may take >10 min:
  run it in the background. `npm test` needs the wasm rebuilt first: `tools/build-designer-wasm.sh`.
- After ANY firmware change: `~/.platformio/penv/bin/python tools/bootprobe.py` on the exact build (never claim it
  if not run; boards may be absent: then report NOT RUN).

## Review Focus

1. **An opened pack with alert keys** (`fromJson`) must round-trip unchanged until edited, and must not report
   `alert.card.*` as "not a piece". Test in Task 3.
2. **Effect cap reached, then a piece with an effect is hidden**: hidden pieces do not count (device counts
   `effect != NONE` on resolved pieces; a hidden piece keeps no style in layout.json). Test in Task 3 (`fxCount`).
3. **Switching board or leaving Scan screen while Play runs** stops Play and selection returns to Scan mode.
   Test in Task 5 (`nextMode`).
4. **A card picture uploaded larger than the card**: the packed art is fitted to the card and the area cap, so the
   device never falls back silently. Test in Task 3 (layoutmedia `cardFor`).
5. **The CYD (320x240) board**: every card rule uses that board's own numbers (min 160x96, cap 2, area 10000).
   Tests in Tasks 2-4 include a CYD case each.

---

### Task 1: Device fixes from 5a (effect under a card, presenter pending path, docs)

**Files:**
- Modify: `ui/scan_alert.h` (`effectRects`, `drawEffects`, ~lines 408-440)
- Modify: `ui/themelayout.h`, `ui/themelayout.cpp:716-717` (effect cap constant)
- Modify: `docs/theme-packs.md` (section "Alerts: cards and effects", ~lines 248-290)
- Test: `sim/test_alert_takeover.cpp`

**Interfaces:**
- Produces: `int themelayout::fxCap(Board b)` (constexpr inline in `themelayout.h`: 4 for `B_WAVESHARE`, else 2);
  `checkLook` uses it for the check and its message.
- Produces: `scanr::effectRects(W, H, out, shake)` skips a piece whose rect lies wholly inside the active alert's
  card when that card is not whole-screen; `drawEffects` skips the same pieces. New signature:
  `int effectRects(int W, int H, uieng::Rect* out, bool* shake = nullptr, const uieng::Rect* card = nullptr)`
  (callers in `scan_present.h` pass the current card rect while an alert is up).

- [ ] **Step 1: Write the failing tests** in `sim/test_alert_takeover.cpp`:
  - `"an effect wholly under a MED card is not repainted"`: Waveshare look with `alert.card.med` at
    `{90,80,300,160}` and `count.ble` placed at `{100,90,100,60}` with `FX_PULSE`; after a MED alert settles,
    the presenter's dirty regions contain no rect equal to the `count.ble` rect, and `effectRects(..., &card)` returns 0.
  - `"an effect partly outside the card still repaints"`: same with `count.ble` at `{60,90,100,60}`: returned 1.
  - `"a commit while a frame is pending dirties the card rows"` (the 5a M4 path): Waveshare presenter, commit a
    scan frame, service fewer strips than it needs (`pending()` true), commit a MED alert frame; `regions()` returns
    0 and the scheduled rows cover every row of the MED card rect.
- [ ] **Step 2: Run** `make -C sim test_alert_takeover && ./sim/test_alert_takeover | tail -n 5`. Expected: the two
  effect tests FAIL; the pending-path test may already pass (it pins existing behaviour; record which).
- [ ] **Step 3: Implement** `fxCap`, the `card` parameter in `effectRects`/`drawEffects`, and pass the card rect from
  `scan_present.h` where effect regions are added.
- [ ] **Step 4: Run** the test again: PASSED. Then `make -C sim test_themelayout && ./sim/test_themelayout | tail -n 1`
  (cap message unchanged: "too many pieces with an effect (at most 4)").
- [ ] **Step 5: Fix `docs/theme-packs.md`**: card font rule = "the font's own height is at least 7 px (scale is not
  counted)"; rewrite the dismiss bullet as: a HIGH card is dismissed by a tap anywhere; a LOW or MED card is
  dismissed by a tap on the card, and a tap outside it reaches the piece under it, except where the card covers a
  piece (then that tap dismisses the alert first). Check the wording against `HitCtx.alertRect` in the code before
  writing it.
- [ ] **Step 6: Full suite and regress** (background): `make -C sim test` exit 0 / 0 FAIL;
  `make -C sim regress` "UMBRAVOX unchanged (landscape and portrait)".
- [ ] **Step 7: Firmware**: `~/.platformio/penv/bin/pio run -e nm-cyd-c5 -e nm-c5-node -e nm-c5-coordinator -e ws-c5-35-engine`
  all SUCCESS; `.dram0.bss` of ws-c5-35-engine <= 94720. Boot probe on each connected board (CYD flash with
  `-t upload --upload-port /dev/ttyUSB0`; Waveshare with esptool `--no-stub`, `docs/lessons.md` #15): 12/12, CYD
  draw median <= 14 ms. Boards absent -> report NOT RUN.
- [ ] **Step 8: Commit** `fix(ui): effects hidden under a card do not repaint; test the pending path; pack doc fixes`.

### Task 2: Bridge rules for the designer (effect cap, built-in card rect)

**Files:**
- Modify: `web/designer/wasm/bridge.cpp` (next to `uv_layout_default`)
- Modify: `web/designer/js/preview.js` (the rules object, ~lines 91-111)
- Test: `web/designer/test/preview-boards.test.mjs`

**Interfaces:**
- Consumes: `themelayout::fxCap` (Task 1); `scanr::alertCardRect(W, H, conf)` with no layout active.
- Produces (wasm): `int uv_fx_cap(int board)` (0 Waveshare, 1 CYD; -1 for a bad board);
  `void uv_card_default(int board, int conf, int* out)` writes x,y,w,h of the built-in card (clears the active
  layout for the call and restores it).
- Produces (JS, on the object `createPreview` returns): `fxCap(board: 'waveshare'|'cyd') -> number`;
  `cardDefault(board, level: 'low'|'med'|'high') -> {x,y,w,h}`.

- [ ] **Step 1: Failing tests** in `preview-boards.test.mjs`: `fxCap('waveshare') === 4`, `fxCap('cyd') === 2`;
  `cardDefault('waveshare','high')` deep-equals `{x:0,y:0,w:480,h:320}`; `cardDefault('cyd','high')` is
  `{x:0,y:0,w:320,h:240}`; `cardDefault(b,'low')` equals `cardDefault(b,'med')` and equals the hero rect
  (the hero: assert y 52 and h = screen height - 104 on both boards, from `uieng::scan`); a bad board throws.
- [ ] **Step 2: Run** `tools/build-designer-wasm.sh && node --test web/designer/test/preview-boards.test.mjs`: FAIL
  (`fxCap is not a function`).
- [ ] **Step 3: Implement** the two exports and the two JS methods (mirror `defaultRect`'s malloc/Int32Array pattern).
- [ ] **Step 4: Run** again: PASS. Then `npm test`: all pass.
- [ ] **Step 5: Commit** `feat(designer): the effect cap and built-in card rects from the device code`.

### Task 3: The layout model: cards, effects, card pictures

**Files:**
- Modify: `web/designer/js/layout.js` (`clone`, `newLayout`, `isEmpty`, `toJson`, `fromJson`, `draftOther`, piece style out/in)
- Modify: `web/designer/js/layoutmedia.js` (`refs.pic` option `cardFor`)
- Test: `web/designer/test/layout.test.mjs`, `web/designer/test/layoutmedia.test.mjs`, `web/designer/test/pack-layout.test.mjs`

**Interfaces:**
- Consumes: Task 2 `cardDefault`, `fxCap` (via `rules`).
- Produces in `layout.js`:
  - `L.cards = { waveshare: {low?, med?, high?}, cyd: {...} }`, each card `{ rect:{x,y,w,h}, font?, scale?, pic? }`
    (font/pic are source keys, as for pieces). `newLayout()` and `clone()` include it; `isEmpty` counts it.
  - `export const LEVELS = ['low', 'med', 'high']`.
  - `export function setCard(L, board, level, patch) -> L'`: merges `patch` (`rect`, `font`, `scale`, `pic`; null/''
    removes a key); `patch === null` removes the card (built-in card again). A card created without `rect` throws.
    Sets `master` like `setPiece`.
  - `export function cardOf(L, board, level, rules) -> {rect, font?, scale?, pic?, builtin: boolean}`: the
    card, or `{ rect: rules.cardDefault(board, level), builtin: true }`.
  - Piece style keys `effect` (`'flash'|'pulse'|'shake'`; `'none'` or null removes it) and `speed`
    (`'slow'|'fast'`; `'medium'` or null removes it, medium being the device default) via the existing `setStyle`.
  - `export function fxCount(L, board) -> number`: placed, not hidden, pieces whose style has an `effect`.
  - `toJson`: after the pieces, `"alert.card.<level>": { rect, font?: refs.font(k), scale?, clip?: refs.pic(k, b,
    null, null, { cardFor: rect }) }`; piece objects carry `effect`/`speed` when set.
  - `fromJson`: reads `alert.card.*` into `L.cards` (font/clip as `'file:'+stem`) and piece `effect`/`speed`;
    no longer lists them in `dropped`.
  - `draftOther`: cards on `from` are scaled with the same `k` and `anchor`, then grown to the target board's
    minimum (CYD 160x96, Waveshare 240x120) and clamped on screen.
- Produces in `layoutmedia.js`: `refs.pic(..., { cardFor: rect })` fits the picture within `rect.w x rect.h` and
  then shrinks it (aspect kept) until `w*h <= ANIM_AREA_MAX[board]` (import from `checks.js`).

- [ ] **Step 1: Failing tests** (`layout.test.mjs`):
  - `'a card round-trips through layout.json'`: `setCard` MED on waveshare `{rect:{x:90,y:80,w:300,h:160}, font:'f2'}`
    -> `toJson` contains `"alert.card.med":{"rect":[90,80,300,160],"font":"f2"}`; `fromJson` of it gives
    `cards.waveshare.med.rect` equal and `dropped` empty.
  - `'setCard(null) brings back the built-in card'`: `cardOf(...).builtin === true` and rect equals `rules.cardDefault`.
  - `'effect and speed are written and read; medium and none are not written'`.
  - `'fxCount ignores hidden pieces'`: two pieces with effects, hide one -> 1.
  - `'an opened pack with cards and effects is returned unchanged until edited'`: `toJson(fromJson(t)) === t`.
  - `'draftOther gives the CYD a card at least 160x96 and on screen'` from a Waveshare MED card `{0,200,240,120}`.
  - (`layoutmedia.test.mjs`) `'a card picture is fitted to the card and the CYD area cap'`: a 300x300 picture,
    CYD card `{0,0,200,120}` -> packed art header w,h with `w <= 200`, `h <= 120`, `w*h <= 10000`.
  - (`pack-layout.test.mjs`) a layout with all three cards (CYD and Waveshare) and 2 effects per board passes
    `preview.checkLayout` (wasm), and 3 effects on the CYD fails with "too many pieces with an effect (at most 2)".
- [ ] **Step 2: Run** `node --test web/designer/test/layout.test.mjs web/designer/test/layoutmedia.test.mjs web/designer/test/pack-layout.test.mjs`: FAIL.
- [ ] **Step 3: Implement** the interfaces above.
- [ ] **Step 4: Run** again: PASS; then `npm test` all pass.
- [ ] **Step 5: Commit** `feat(designer): alert cards, effects and card pictures in the layout model`.

### Task 4: Warnings

**Files:**
- Modify: `web/designer/js/checks.js` (`lookChecks`)
- Test: `web/designer/test/checks.test.mjs`

**Interfaces:**
- Consumes: `L.cards`, `cardOf`, `rectOf`, `LEVELS` (Task 3); `decodeFont` from `fontfmt.js` (font height `h`).
- Produces: `lookChecks(theme, L, media, rules = null)`: the new `rules` argument (`{cardDefault, defaultRect}`) is
  needed for default rects; without it, the card-over-control check is skipped. New `warn` items:
  - per board, per LOW/MED card (placed or built-in) overlapping `stop`, `menu` or any `nav.*` rect:
    `"<level> alert card on the <Board> board covers <piece>: while the alert shows, a tap there dismisses the alert first"`;
  - card font with `h < 7`: `"... alert card font is <h> px; the device needs 7 px and uses its built-in font"`;
  - card picture (only a `file:` key, which is not refitted) wider/taller than the card or over `ANIM_AREA_MAX`:
    `"... alert card picture does not fit; the device plays the rite instead"`.

- [ ] **Step 1: Failing tests**: one per warning, each with a CYD case for the cover warning (CYD built-in MED card
  over a default `nav.*` must warn only if the rects actually overlap: assert against `rectOf`, not a literal); a HIGH
  card over STOP gives no warning; a MED card not touching any control gives none.
- [ ] **Step 2: Run** `node --test web/designer/test/checks.test.mjs`: FAIL.
- [ ] **Step 3: Implement**; update the `lookChecks` call in `app.js` (~line 321) to pass `rules()`.
- [ ] **Step 4: Run** again, then `npm test`: PASS.
- [ ] **Step 5: Commit** `feat(designer): warn about cards over controls, small card fonts and pictures that do not fit`.

### Task 5: The Layout step's alert mode, card panel, effects and Play

**Files:**
- Create: `web/designer/js/layout-alert.js` (DOM-free)
- Modify: `web/designer/index.html` (mode tabs above `layout-canvas`, card panel, "During an alert" row in `layout-look`)
- Modify: `web/designer/js/app.js` (`syncLayout`, `syncLook`, pointer handlers, `paintLayout`, `selectBoard`)
- Modify: `web/designer/designer.css` (only what the new controls need)
- Test: `web/designer/test/layout-alert.test.mjs` (new), `web/designer/test/layout-wiring.test.mjs`

**Interfaces:**
- Consumes: Tasks 2-4.
- Produces in `layout-alert.js`:
  - `export const MODES = ['scan', 'low', 'med', 'high']`; `state.lmode` in app.js holds one (default `'scan'`).
  - `export const PLAY_MS = 5000` (one `TEST` alert), `export const STILL_MS = 3000` (a settled frame).
  - `export function alertFrame(mode, playStart, nowMs) -> number`: `'scan'` -> `Math.floor(nowMs/20)`; an alert
    mode with `playStart === null` -> `STILL_MS/20`; playing -> `Math.floor(((nowMs - playStart) % PLAY_MS)/20)`.
  - `export function nextMode(mode, event) -> {mode, playing:boolean}` for events `'board'` and `'screen'`
    (both -> `{mode:'scan', playing:false}`), `'mode:<m>'` (-> that mode, playing false).
  - `export function editableRects(mode, pieceRects, cardRect) -> {name: rect}`: Scan -> `pieceRects`; alert ->
    `{ ['alert.card.' + mode]: cardRect }` only.
  - `export function effectAllowed(L, board, piece, cap) -> boolean`: true if the piece already has an effect or
    `fxCount(L, board) < cap`.
- UI behaviour (app.js): tabs `Scan | Alert LOW | Alert MED | Alert HIGH` and a Play/Stop button, shown only when
  `state.lscreen === 'scan'` (Play only in an alert mode); in an alert mode the canvas renders `renderDraft('alert', alertFrame(...), {board,
  alertConf: mode})`, outlines/handles/drag/nudge/x-y-w-h act on the card through `setCard`; the card panel
  (font, scale, picture, "Use the built-in card") replaces the piece look panel; the piece panel gains
  "During an alert": effect select (`none|flash|pulse|shake`) and speed select (`slow|medium|fast`), the effect
  select disabled with the text "At most N pieces can have an effect on this board" when `!effectAllowed`;
  warnings from Task 4 appear inline under the card panel as well as in the checks list.

- [ ] **Step 1: Failing tests** (`layout-alert.test.mjs`): `alertFrame` cases (scan at 1000 ms -> 50; med not
  playing -> 150; playing started at 1000, now 6200 -> 10); `nextMode` for `'board'`, `'screen'`, `'mode:high'`;
  `editableRects('med', {stop:...}, r)` has only `alert.card.med`; `effectAllowed` false for a third CYD piece, true
  for a piece that already has one. (`layout-wiring.test.mjs`) ids exist: `layout-mode-scan`, `layout-mode-low`,
  `layout-mode-med`, `layout-mode-high`, `layout-play`, `lc-font`, `lc-scale`, `lc-pic`, `lc-builtin`, `ls-effect`,
  `ls-speed`; app.js imports `./layout-alert.js` and calls `setCard(`.
- [ ] **Step 2: Run** `node --test web/designer/test/layout-alert.test.mjs web/designer/test/layout-wiring.test.mjs`: FAIL.
- [ ] **Step 3: Implement** `layout-alert.js`, then the HTML and app.js wiring.
- [ ] **Step 4: Run** again, then `npm test`: PASS.
- [ ] **Step 5: Browser check** (`tools/build-designer-wasm.sh && tools/assemble-designer.sh web/dist`,
  `python3 -m http.server -d web/dist`): on both boards, place a MED card over STOP (warning shows), set a picture,
  give 2 pieces effects on the CYD (third is disabled), Play loops, switching board stops Play. Save a screenshot per
  board to `private/renders/` for the owner. Report anything not checked.
- [ ] **Step 6: Commit** `feat(designer): alert mode in the Layout step: cards, effects and Play`.

### Task 6: Pack docs for authors

**Files:**
- Modify: `docs/theme-packs.md` (a short "In the designer" paragraph under "Alerts: cards and effects")

- [ ] **Step 1:** Describe the mode tabs, Play, the effect cap message and the three warnings in 4-6 lines; no
  new rules (rules stay as in Task 1's wording).
- [ ] **Step 2:** `npm test` (the limitations/docs tests read this file): PASS.
- [ ] **Step 3: Commit** `docs: designing the alert in the designer`.
