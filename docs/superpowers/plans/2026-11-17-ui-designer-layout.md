# UI Designer Layout Step Implementation Plan (plan 3 of 6)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** The theme designer gets a **Layout** step: drag pieces on the live device preview to move and resize them,
board tabs (Waveshare 3.5" / CYD 2.8") with the other board drafted, a tray for hidden pieces, exact numbers in a side
panel, starting layouts on the Start page; packs carry `layout.json`; a radar clip is re-made at the placed size.

**Architecture:** The wasm bridge learns to load `layout.json` (the device's own `themelayout::build`) and to render
the engine UI at 320x240 as well as 480x320, and exports the layout rules (defaults, names, the resolve verdict). All
editing logic lives in small DOM-free modules (`js/layout.js` model + drafting, `js/layout-edit.js` pointer maths,
`js/layout-presets.js`) unit-tested with node:test; `app.js` and `index.html` only wire them to the page.

**Tech Stack:** vanilla ES modules (no framework), emscripten wasm bridge (`web/designer/wasm/bridge.cpp`), node:test.

**Spec:** `docs/superpowers/specs/2026-11-17-ui-overhaul-design.md` ("The designer", "Two boards"). Builds on plans 1 and 2
(same branch). **Research:** `private/agent/notes/designer-map.md` (file:line for every place named below).

## Global Constraints

- The layout the designer saves is checked by the device's own rules through wasm (`themelayout::resolve`); the designer
  never ships a `layout.json` the device would refuse. Any refusal shows as an error in the checks list and disables
  Download / Install / Share, as other errors do.
- A saved pack always carries **both** boards (spec "Two boards": the designer drafts the board the author did not lay
  out; the device never guesses).
- Pieces: exactly `themelayout::PIECE_NAME` (19, read from wasm, never retyped in JS). Defaults from
  `themelayout::defaultRect` via wasm.
- The CYD tab previews the **engine** UI at 320x240 (what `layout.json` changes). It says plainly that the default CYD
  release draws the older 160x120 screens and ignores the layout.
- Built-in behaviour unchanged: a theme with no layout produces no `layout.json` and the same pack bytes as today.
- Sharing a theme that has a layout is blocked with a clear message until plan 6 updates the community repository
  (its intake accepts only the four current files).
- No new runtime dependency; vanilla JS; node:test.
- Commits: author `Em3ritus <152051018+Em3ritus@users.noreply.github.com>`, trailer
  `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.

## Decisions made here

- **Drafting rule** (`draftOther(fromBoardLayout, from, to)`), per placed piece: scale `k = min(Wto/Wfrom, Hto/Hfrom)`;
  size `w*k, h*k` (the radar stays square: side = round(w*k), and tappable pieces never below 44); position anchored by
  which third of the source screen the piece's centre lies in, horizontally and vertically separately: left third →
  same distance from the left edge (scaled), right third → same from the right edge, middle → same offset from the
  centre; then clamp on screen; then, if it now overlaps a piece it did not overlap on the source board, nudge it along
  the shorter axis away from that piece by the overlap, up to 3 tries; then run `resolve`. If the draft fails, the
  other board's tab shows the reason and the author fixes it there (Download is blocked until both boards pass).
- **Hidden pieces** keep their last rect in the editor (so showing one again puts it back where it was).
- **Starting layouts** (Start page; each is a layout for both boards, tested to pass `resolve` on both):
  1. *Standard* (no layout.json), 2. *Radar left* (radar at the left of the body, tiles on the right),
  3. *Big radar* (radar at its largest, tiles hidden, footer moved to the top), 4. *Numbers* (radar hidden, tiles
  enlarged across the body), 5. *Bottom-up* (header at the bottom, footer at the top).
- **Snap:** dragging snaps to 2 px; arrow keys nudge 1 px (Shift: 8 px).
- **Radar clip at the placed size:** when the Waveshare radar's radius differs from the clip's side, the designer
  resamples the clip's frames (area average of the 4-bit levels) to side = radius at Download/Install time; the
  original clip stays in the draft so resizing again does not compound loss.
- The speed meter is plan 5 (where alert animation and effects add cost); a layout alone cannot draw more than the
  built-in pieces.

## Review Focus

1. Dragging a piece off the canvas edge or onto another piece: the model clamps on screen; overlap shows as a check error, never a silent change (Task 4).
2. A layout opened from a zip that the device would refuse (hand-written): the Layout step shows the reason and the pack cannot be re-saved until fixed (Task 2, Task 6).
3. Switching board tabs mid-drag, or resizing the radar non-square: the radar stays square; no state leaks between boards (Task 4, Task 5).
4. A theme with a radar clip and a shrunk radar: the saved `radar.vid` is the placed size, the draft keeps the original (Task 7).
5. Older packs (no layout.json) open, edit and save byte-identically to today (Task 6).

---

### Task 1: The bridge loads layout.json and exports the layout rules

**Files:** `web/designer/wasm/bridge.cpp`, its JS wrapper (per the map), `web/designer/test/wasm-parity.test.mjs` (or a new `test/layout-wasm.test.mjs`)

**Interfaces (Produces, JS side):**
```js
pieceNames()                    // -> string[19], from wasm (themelayout::PIECE_NAME)
defaultRect(board, piece)       // board 'waveshare'|'cyd' -> {x,y,w,h}
checkLayout(jsonText)           // -> {ok:true} | {ok:false, why:string}   (themelayout::build: parse + resolve both boards)
```
`uv_load_pack` reads `layout.json` when present (`themelayout::build` into a static `PackLayout`, then
`pal::setPackLayout`), exactly as `sim/packload.h` does.

- [ ] **Step 1: Tests:** names equal the C++ list (19, `"menu"` first, `"badge"` last); `defaultRect('waveshare','stop')`
  equals `{384,276,96,44}`; `checkLayout` of a valid two-board layout → ok; of `{"format":1,"cyd":{"menu":"hidden"}}` →
  `why` contains `menu cannot be hidden`; a pack with a layout moving `stop` renders (480x320) equal to the host
  reference (`sim/ws_preview_png` given the same pack folder) — reuse the `wsWasm`/`wsSim` helpers.
- [ ] **Step 2: Run, expect FAIL** — `cd web/designer && node --test test/layout-wasm.test.mjs`.
- [ ] **Step 3: Implement** (rebuild with `tools/build-designer-wasm.sh`).
- [ ] **Step 4: Run, expect PASS**; full `npm test`.
- [ ] **Step 5: Commit** — `feat(designer): the wasm bridge loads layout.json and exports the layout rules`.

### Task 2: Engine preview at 320x240

**Files:** `ui/preview_ws.h` (`prevws::render` takes W, H), `web/designer/wasm/bridge.cpp` (export `uv_render_engine(theme, screen, frame, sweep, alertStyle, alertConf, W, H)`; `uv_render_ws` stays as the 480x320 call), `sim/ws_preview_png.cpp` (a `--size 320x240` option), tests

- [ ] **Step 1: Tests:** the 320x240 wasm render equals the host render for every built-in theme on scan (and one
  other screen); the 480x320 path is byte-identical to before (existing parity tests unchanged); a pack with a CYD
  layout renders the moved piece at 320x240.
- [ ] **Step 2: Run, expect FAIL.**
- [ ] **Step 3: Implement.** Keep `uiproj::scan`'s 480x320 wiring as the default; add the 320x240 projection the
  device's CYD engine build uses (find it in `device/scan_adapter.cpp` / the `nm-cyd-c5-engine` path; reuse, do not copy).
- [ ] **Step 4: Run, expect PASS**; `make -C sim test_engine_pixels`; `npm test`.
- [ ] **Step 5: Commit** — `feat(designer): engine preview at 320x240 for the CYD tab`.

### Task 3: Layout model, drafting and presets (DOM-free)

**Files:** create `web/designer/js/layout.js`, `web/designer/js/layout-presets.js`; tests `test/layout.test.mjs`, `test/layout-presets.test.mjs`

**Interfaces (Produces):**
```js
newLayout()                                   // {boards:{waveshare:{}, cyd:{}}, master:null, adjusted:{waveshare:Set, cyd:Set}}
setPiece(L, board, name, rect|'hidden')       // returns a new L; radar forced square (side = max(w,h) clamped to its limits)
showPiece(L, board, name)                     // back to its last rect
toJson(L) / fromJson(text)                    // layout.json text (both boards, pieces in PIECE_NAME order, only non-default ones)
draftOther(L, from, to, rules)                // rules = {defaultRect, checkLayout}; the drafting rule above; marks nothing adjusted
isEmpty(L)                                    // true -> no layout.json
PRESETS                                       // [{id,name,description, layout:{waveshare:{...},cyd:{...}}}]
```
- [ ] **Step 1: Tests:** round trip `fromJson(toJson(L))`; radar squareness; hidden keeps last rect; `toJson` of an
  empty layout is `null`; drafting: a piece in the right third keeps its distance from the right edge (scaled), a
  centred piece stays centred, a 44-px button never drafts under 44, a draft that overlaps is nudged apart; every preset
  passes `checkLayout` on both boards (tests load the wasm for `checkLayout`).
- [ ] **Step 2: Run, expect FAIL.**
- [ ] **Step 3: Implement.**
- [ ] **Step 4: Run, expect PASS.**
- [ ] **Step 5: Commit** — `feat(designer): layout model, drafting and starting layouts`.

### Task 4: Pointer editing maths (DOM-free)

**Files:** create `web/designer/js/layout-edit.js`; test `test/layout-edit.test.mjs`

**Interfaces (Produces):**
```js
toDevice(clientX, clientY, canvasRect, canvasW, canvasH)  // -> {x,y} device px
pickPiece(L, board, x, y, rects)                          // topmost visible piece at (x,y) (z: radar group < tiles < chrome), or null
handleAt(rect, x, y, slop)                                // 'nw'|'ne'|'sw'|'se'|null (8 px slop)
dragTo(rect, start, now, mode, bounds, opts)              // mode 'move' or a corner; snap 2 px; clamp to bounds; radar keeps square
nudge(rect, key, shift)                                   // arrows: 1 px, Shift 8 px
```
- [ ] **Step 1: Tests:** device mapping at 50% and 200% CSS scale; topmost pick when the radar overlaps the badge;
  corner resize never below 1 px and never past the screen; moving clamps inside the screen; radar corner drag stays
  square; nudge values.
- [ ] **Step 2: Run, expect FAIL.**  - [ ] **Step 3: Implement.**  - [ ] **Step 4: Run, expect PASS.**
- [ ] **Step 5: Commit** — `feat(designer): pointer maths for the Layout step`.

### Task 5: The Layout step in the page

**Files:** `web/designer/index.html` (Layout view: board tabs, preview canvas reuse, side panel, hidden tray, preset
picker on Start), `web/designer/js/app.js` (state.layout; pointer/keyboard wiring; tick draws the engine preview for
the active board with selection handles drawn over it; drafting when a board is first opened; "adjusted" marks),
`web/designer/designer.css`; test `test/layout-wiring.test.mjs` (regex/structure tests like `share-wiring.test.mjs`)

- [ ] **Step 1: Tests (structure):** the Layout view exists with `#layout-board-waveshare`, `#layout-board-cyd`,
  `#layout-tray`, `#layout-x/y/w/h` inputs; app.js imports `layout.js`, `layout-edit.js`, `layout-presets.js`;
  pointerdown/move/up and keydown handlers are wired on the preview; the Start page lists every `PRESETS` entry; the CYD
  tab carries the "default CYD release ignores layouts" note.
- [ ] **Step 2: Run, expect FAIL.**  - [ ] **Step 3: Implement.**  - [ ] **Step 4: Run, expect PASS**; `npm test`.
- [ ] **Step 5: Manual check (headless Chrome, as for sharing):** load the local designer, pick *Radar left*, drag
  STOP, resize the radar, hide the wordmark, switch tabs, check the CYD draft; screenshot both boards into
  `private/renders/` for the owner. Record what was and was not exercised.
- [ ] **Step 6: Commit** — `feat(designer): the Layout step`.

### Task 6: Packs carry layout.json through the designer

**Files:** `js/pack.js` (`buildPack` adds `layout.json` when the layout is not empty), `js/model.js`
(`selectPackFiles`, `openPackFiles`), `js/install.js` (stale-file list: remove a stale `layout.json` when the new pack
has none), `js/usbinstall.js` (`CAPS`, `ordered`, `SHAPE`), `js/checks.js` (layout verdict + "both boards pass"),
`js/limits.json` (`MAX_LAYOUT_JSON` 4096; the `presets` limitation text no longer says "no custom screen layout"; a new
limitation "layouts change the engine screens only" — keep `limits.test.mjs`'s count and the docs heading in step),
share: block with a message when the pack has a layout (until plan 6); `js/gallery.js` unchanged (plan 6)
**Tests:** `test/pack.test.mjs` (or the existing pack tests), `test/limits.test.mjs`, `test/share-wiring.test.mjs`

- [ ] **Step 1: Tests:** a pack with a layout has 5 files including a `layout.json` that `checkLayout` accepts; a pack
  with no layout is byte-identical to today's (`buildPack` output hash for a fixture theme unchanged); opening a zip with
  `layout.json` restores the layout (both boards); opening one the device would refuse shows the reason; install
  removes a stale `layout.json`; USB install sends it; Share is disabled with the message when a layout exists; limits
  count and doc heading agree.
- [ ] **Step 2: Run, expect FAIL.**  - [ ] **Step 3: Implement.**  - [ ] **Step 4: Run, expect PASS**; `npm test`.
- [ ] **Step 5: Commit** — `feat(designer): packs carry layout.json`.

### Task 7: Radar clip at the placed size, docs

**Files:** `js/radarvideo.js` (`resizeClip(clipBytes, side)`; the clip maker takes a size), `js/pack.js` (at build:
resize the clip to the Waveshare radar's radius when it differs), `docs/theme-packs.md` ("Make a pack in the designer":
the Layout step; layout.json no longer "hand-written only"), `CHANGELOG.md`; test `test/radarvideo-resize.test.mjs`

- [ ] **Step 1: Tests:** `resizeClip` keeps header fields (version, fps, frames, palette) and sets side; frame bytes =
  side²/2 rounded up; a uniform-level clip stays uniform; a 102→48 resize parses in wasm; the pack's `radar.vid` side
  equals the placed radius; the draft keeps the original 102 clip.
- [ ] **Step 2: Run, expect FAIL.**  - [ ] **Step 3: Implement.**  - [ ] **Step 4: Run, expect PASS**; full `npm test`;
  `make -C sim test` (background).
- [ ] **Step 5: Commit** — `feat(designer): radar clip at the placed size; docs for the Layout step`.
