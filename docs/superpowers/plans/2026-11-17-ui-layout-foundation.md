# UI Layout Foundation Implementation Plan (plan 1 of 6)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A pack's optional `layout.json` moves, resizes or hides the shared header and footer pieces and Scan's three
count tiles on either engine board; every engine screen draws and takes touches inside the space they leave; built-in
themes stay byte-identical.

**Architecture:** A new bounded parser (`ui/themelayout.*`) turns `layout.json` into a `Layout`; `resolve()` fills
defaults from the built-in geometry and computes the content area; `check()` enforces the safety rules. The resolved
per-board `ChromeOverride` is installed as a global pointer read by `uieng::chrome()` and `uieng::scan()`, so drawing
and hits read the same rects. `drawChrome` is refactored so each piece draws relative to its own rect.

**Tech Stack:** C++17 header/impl in `ui/`, host tests in `sim/` (ASan/UBSan for the parser), Arduino firmware in
`device/`, PlatformIO builds.

**Spec:** `docs/superpowers/specs/2026-11-17-ui-overhaul-design.md`

The later plans, each written when the previous one has merged: 2 radar rework (radar, spectrum, channel and badge as
resizable pieces, Scan's hero), 3 designer Layout step and drafting, 4 fonts and art and styling of other screens,
5 alerts (cards per level, clip, effects, CYD takeover), 6 sharing (themes repo, previews, FIRMWARE_PIN).

## Global Constraints

- `theme.json` stays `"format": 1`; the layout is a separate file `layout.json` with its own `"format": 1`, at most
  4096 bytes. A device without this loader ignores the file.
- Engine boards: Waveshare 480×320 (`W >= 400`) and CYD engine build 320×240. The legacy 160×120 UI ignores layouts.
- Built-in themes (no override installed): every rect, hit and pixel identical to today. `test_layout_engine` (oracle)
  and `make -C sim regress` must pass unchanged.
- Every tappable piece is at least 44 px each way on both boards.
- Only `wordmark`, `sd`, `count.wifi`, `count.5g`, `count.ble` may be hidden (spec: pieces without another route
  cannot be hidden; in this plan every other piece has an action with no second route checked yet).
- A failed layout check skips the layout only; the theme still loads; the reason goes to `/diag.log` and the ASPECT
  skipped count.
- Pack layouts live in PSRAM on the device (`heap_caps_malloc(MALLOC_CAP_SPIRAM)`), never `.bss`; boot heap must stay
  above 55 KB.
- The parser takes the hostile-input treatment of `themepack::parse`: bounded, allocation-free, no NUL assumption,
  depth cap, ASan/UBSan mutation fuzz with an invariant.
- Commits: author `Em3ritus <152051018+Em3ritus@users.noreply.github.com>`, trailer
  `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`. Never write a `/home/<user>` path into a tracked file.

## Format (decided here)

```json
{ "format": 1,
  "waveshare": { "menu": [0, 276, 44, 44], "wordmark": "hidden", "count.ble": [16, 196, 100, 68] },
  "cyd": { } }
```

- Board keys: `waveshare`, `cyd`. Each optional; an absent board means "built-in layout on that board".
- Piece keys (`PIECE_NAME`): `menu wordmark status.gps status.nodes status.power bridge sd nav.scan nav.domain
  nav.codex nav.poi stop count.wifi count.5g count.ble`. Value: `[x, y, w, h]` (integers, board pixels) or
  `"hidden"`. An absent piece keeps its built-in rect (shifted with the content area for `count.*`).
- Unknown top-level and piece keys are ignored (later plans add pieces), except a key `script` anywhere, which fails:
  `script pieces are not supported`. Duplicate keys fail. Nesting deeper than 3 fails.

## Content area (decided here)

The content area is the largest axis-aligned rectangle on screen that no visible header/footer piece (every piece
except `count.*`) overlaps. The **body** every non-Scan screen receives is exactly today's body size
(`W-16 × H-104`), centred in the content area (offsets rounded down). If the content area is smaller than that size
in either direction, `check()` fails: `header and footer leave too little room`. Rationale: every screen is then
drawn at a size its tests already cover, only shifted; cost if wrong: layouts that shrink the content area are
refused until a later plan lets screens reflow.

Scan's own rects (`body`, `hero`, `counts`, and any `count.*` left at default) shift by
`delta = body.xy - builtinBody.xy`.

## Review Focus

1. A layout that places a piece at a negative coordinate or past the screen edge: refused, never drawn or hit
   outside the canvas (Task 2).
2. A hidden piece: no hit entry at all, not a zero-size hit at (0,0) that steals taps (Task 3).
3. Two pieces overlapping, or a count tile overlapping the shifted hero: refused (Task 2).
4. A layout.json that is valid JSON but sets only the `cyd` board, used on a Waveshare: the Waveshare stays
   byte-identical to the built-in (Task 3).
5. Switching from a layout theme back to a built-in theme at runtime: the override is cleared and the next frame is
   a full built-in frame (Task 6).

---

### Task 1: The `layout.json` parser

**Files:**
- Create: `ui/themelayout.h`, `ui/themelayout.cpp`
- Test: `sim/test_themelayout.cpp`
- Modify: `sim/Makefile` (target built like `test_themepack`, with `-fsanitize=address,undefined -fno-sanitize-recover=undefined`; add to `TESTS`)

**Interfaces:**
- Produces:
```cpp
namespace themelayout {
constexpr int FORMAT = 1;
constexpr size_t MAX_JSON = 4096;
enum Board : uint8_t { B_WAVESHARE, B_CYD, B_N };
enum Piece : uint8_t { P_MENU, P_WORDMARK, P_GPS, P_NODES, P_POWER, P_BRIDGE, P_SD,
                       P_NAV_SCAN, P_NAV_DOMAIN, P_NAV_CODEX, P_NAV_POI, P_STOP,
                       P_COUNT_WIFI, P_COUNT_5G, P_COUNT_BLE, P_N };
extern const char* const PIECE_NAME[P_N];
enum How : uint8_t { DEFAULT = 0, PLACED, HIDDEN };
struct Slot { How how; int16_t x, y, w, h; };
struct Layout { bool has[B_N]; Slot slot[B_N][P_N]; };   // zero-initialised = all DEFAULT
bool parse(const char* json, size_t n, Layout& out, char* why, size_t whyCap);
}
```

- [ ] **Step 1: Write the failing tests** in `sim/test_themelayout.cpp` (same `check(what, ok)` style as `test_themepack.cpp`):
  - `parses a waveshare menu rect`: `{"format":1,"waveshare":{"menu":[0,276,44,44]}}` → true, `has[B_WAVESHARE]`, `!has[B_CYD]`, slot `{PLACED,0,276,44,44}`.
  - `hidden wordmark`: `"wordmark":"hidden"` → `HIDDEN`.
  - `absent piece is DEFAULT`.
  - `format 2 refused` (why contains `format`), `missing format refused`.
  - `script key refused` both at top level and inside a board.
  - `duplicate piece refused`, `duplicate board refused`.
  - `array of 3 refused`, `non-integer refused`, `value outside -32768..32767 refused`.
  - `unknown piece ignored` (`"radar":[1,2,3,4]` parses, nothing set).
  - `depth 4 refused`, `over MAX_JSON refused`, `not NUL terminated` (parse a buffer whose byte n is `'x'`).
  - Fuzz: 200,000 mutations (byte flips, truncations, insertions of `[]{}",:` ) of 6 valid seeds; invariant: on
    true, every `PLACED` slot has `w > 0 && h > 0` and every `How` is one of the three values; never crashes under
    ASan/UBSan.
- [ ] **Step 2: Run, expect FAIL** — `make -C sim test_themelayout && sim/test_themelayout` → build error (no `themelayout.h`).
- [ ] **Step 3: Implement `parse`** in `ui/themelayout.cpp`: its own bounded cursor reader in the style of
  `ui/themepack.cpp` (struct with `p, end, why`; `ws`, `eat`, `str`, `integer`, `skipValue` with depth); do not
  refactor `themepack.cpp`. A `PLACED` slot with `w <= 0 || h <= 0` fails (`<piece>: width and height must be
  positive`). Header must not include Arduino or `palette.h`.
- [ ] **Step 4: Run, expect PASS** — same command; all ok, fuzz line printed.
- [ ] **Step 5: Commit** — `feat(ui): bounded parser for layout.json`.

### Task 2: Resolve and check

**Files:**
- Modify: `ui/layout_engine.h` (rename today's `chrome` body to `builtinChrome`, `scan` to `builtinScan`; add `ChromeOverride`; `chrome`/`scan` unchanged in behaviour for now — they call the builtins)
- Modify: `ui/themelayout.h`, `ui/themelayout.cpp`
- Test: `sim/test_themelayout.cpp`

**Interfaces:**
- Consumes: Task 1 `Layout`.
- Produces:
```cpp
namespace uieng {
inline Chrome builtinChrome(int W, int H);   // today's chrome(), verbatim
inline ScanLayout builtinScan(int W, int H); // today's scan(), verbatim
struct ChromeOverride {
    Chrome c;               // every chrome rect resolved; hidden = {0,0,0,0}; c.body = the placed body
    Rect tiles[3];          // Scan count tiles resolved (hidden = zero rect)
    Rect hero, counts, scanBody;   // Scan's rects shifted by delta
};
}
namespace themelayout {
Board boardFor(int W);                                   // W >= 400 -> B_WAVESHARE
bool resolve(const Layout& L, Board b, uieng::ChromeOverride& out, char* why, size_t whyCap);  // false = check failed
}
```
`resolve` does the content area and every check; it is the only way to get a `ChromeOverride`.

- [ ] **Step 1: Write the failing tests** (both boards each):
  - `empty board resolves to builtin`: a `Layout` with `has[b]` and all DEFAULT → `out.c` equals `builtinChrome` rect
    for rect, `out.tiles/hero/counts/scanBody` equal `builtinScan`'s.
  - `menu moved to bottom-left`: header/footer moved so the content area shifts; assert `out.c.body` is
    `W-16 × H-104`, centred in the free rectangle, and `out.hero` = builtin hero + delta.
  - `off screen refused` (x = -1; x + w = W + 1), `under 44 refused` for every tappable piece (43 wide),
    `wordmark may be 20 tall` (not tappable).
  - `hiding menu refused`, `hiding stop refused`, `hiding bridge refused`, `hiding count.ble allowed` → tile zero.
  - `overlap refused` (stop over nav.poi), `tile over shifted hero refused`.
  - `too little room refused` (a 480-wide, 100-tall menu across the middle).
- [ ] **Step 2: Run, expect FAIL** — `make -C sim test_themelayout && sim/test_themelayout` → `resolve` undefined.
- [ ] **Step 3: Implement.** Largest free rectangle: brute force over candidate edges (0, W, H and every visible
  piece's left/right/top/bottom), O(n⁴) with n ≤ 15 pieces; ties: smallest y then smallest x. Run
  `make -C sim test_layout_engine && sim/test_layout_engine` to confirm the rename changed nothing.
- [ ] **Step 4: Run, expect PASS** — both tests.
- [ ] **Step 5: Commit** — `feat(ui): resolve and check a layout into a chrome override`.

### Task 3: Install the override; hits follow

**Files:**
- Modify: `ui/layout_engine.h`
- Test: `sim/test_layout_override.cpp` (new; add Makefile target like `test_layout_engine` plus `../ui/themelayout.cpp`; add to `TESTS`)

**Interfaces:**
- Produces:
```cpp
namespace uieng {
struct LayoutOverride { const ChromeOverride* board[2]; };   // [B_WAVESHARE], [B_CYD]; null = builtin on that board
inline const LayoutOverride*& activeLayout();               // function-local static; nullptr = built-in everywhere
inline Chrome chrome(int W, int H);    // override for this board if set, else builtinChrome
inline ScanLayout scan(int W, int H);  // same; status/nav/statusCells/navCells/wordmark/stop/bridge from the override's chrome
}
```
  `HitList::add` ignores a rect with `w <= 0 || h <= 0`.

- [ ] **Step 1: Write the failing tests:**
  - `no override = oracle`: with `activeLayout() == nullptr`, `scanHits` for both sizes equals the oracle set (reuse
    `oracle_hits.inc` the way `test_layout_engine.cpp` does for one state).
  - `moved stop hit follows`: install an override with `stop` at a new rect → the `stop` hit's rect equals it, on
    Scan and on `domainHits`.
  - `hidden tile has no hit`: hide `count.5g` → no hit with slot `"5 GHZ"`, and no hit with a zero-size rect at all.
  - `cyd-only layout leaves waveshare builtin` (Review Focus 4): override with `board[B_CYD]` only → `chrome(480,320)`
    equals `builtinChrome(480,320)`.
  - `every screen's body is the override body`: for `codex menu mesh omens echoes attune aspect detail files domain`,
    the layout's body/content rect lies inside `ChromeOverride::c.body`.
- [ ] **Step 2: Run, expect FAIL** — `make -C sim test_layout_override && sim/test_layout_override`.
- [ ] **Step 3: Implement** the accessor, the two branches and the `add` guard. Screens that call `chrome(320, H)`
  for a width-independent height (`attune` helper) must keep using `builtinChrome` there (a CYD override must not
  leak into the Waveshare's knob count); note it in a comment.
- [ ] **Step 4: Run, expect PASS**, then `make -C sim test_layout_engine && sim/test_layout_engine` (unchanged).
- [ ] **Step 5: Commit** — `feat(ui): an installed layout moves chrome and count tiles; hits follow`.

### Task 4: `drawChrome` draws each piece in its own rect

**Files:**
- Modify: `ui/scan_render.h` (`drawChrome`, and the count tiles loop near line 1052)
- Test: `sim/test_layout_override.cpp`; regress baseline

- [ ] **Step 0: Baseline in the worktree.** `cp -r ../../private/baseline private/` (from the worktree root; `private/` is
  git-ignored) and run `make -C sim regress` → passes before any change.
- [ ] **Step 1: Write the failing test** `chrome ink stays in its pieces`: install an override that moves every
  header piece to the bottom and every footer piece to the top (a valid layout); render Scan and Codex at both sizes
  (fixtures `scan_fixture.h`, `codex_fixture.h`); assert every pixel that differs from `pal::VOID` lies inside the
  union of the visible piece rects and the body. And `hidden wordmark draws nothing`: no non-VOID pixel in the
  builtin wordmark rect's area when that area is not covered by another piece.
- [ ] **Step 2: Run, expect FAIL** (today's code draws at fixed y = 10, 26, 37, H-44 ...).
- [ ] **Step 3: Refactor** `drawChrome`: every coordinate of a piece becomes an offset from that piece's rect
  (`r.x + dx, r.y + dy`, with today's numbers as the offsets from today's rects). Built-in path (no override):
  keep the full-width band fills and the edge lines at `y = 43` and `H - 46` exactly. Override path: each visible
  piece gets a `pal::PANEL` fill of its own rect; no band fills or band lines. A zero rect draws nothing. Count
  tiles: skip a zero rect.
- [ ] **Step 4: Run** the test → PASS; `make -C sim regress` → passes with **no** changed image; full
  `make -C sim test` → all pass. Record the regress output line in the commit body.
- [ ] **Step 5: Commit** — `refactor(ui): chrome pieces draw relative to their own rects`.

### Task 5: Every screen fits the content area

**Files:**
- Test: `sim/test_layout_override.cpp`
- Modify: any `ui/*_render.h` / `ui/layout_engine.h` screen code the test shows drawing or hitting outside its body

- [ ] **Step 1: Write the test** `screens stay in the body`: for each of the 11 engine screens (Scan, Domain, Codex,
  Menu, Mesh, Omens, Echoes, Attune, Aspect, Detail, Files) at both sizes, with the Task 4 override installed:
  render via the screen's fixture; every non-VOID pixel lies in the union of visible piece rects and
  `ChromeOverride::c.body` (Scan: `scanBody`); every hit from the screen's `*Hits` lies inside the screen and in
  that same union.
- [ ] **Step 2: Run, expect FAIL** where a screen uses absolute numbers (look first at `attune`'s `chrome(320,H)`
  helper, any literal `52`, `H-44`, `H-104` outside `builtinChrome`/`builtinScan`).
- [ ] **Step 3: Fix** each offender to derive from its layout's body. No change to a built-in pixel.
- [ ] **Step 4: Run, expect PASS**; `make -C sim regress` → no changed image; `make -C sim test` → all pass.
- [ ] **Step 5: Commit** — `fix(ui): every engine screen draws inside the layout body`.

### Task 6: Packs carry a layout; the theme installs it

**Files:**
- Modify: `ui/palette.h`, `ui/palette.cpp` (`setPackLayout`, `setTheme` installs/clears)
- Modify: `sim/packload.h` (read `layout.json` after `theme.json`)
- Modify: `device/themeload.cpp` (same, PSRAM, guard)
- Test: `sim/test_layout_pack.cpp` (new; fixtures under `sim/packs/layout-*`), Makefile target + `TESTS`

**Interfaces:**
- Consumes: Task 2 `resolve`, Task 3 `activeLayout`.
- Produces:
```cpp
namespace pal {
void setPackLayout(int packId, const uieng::LayoutOverride* lo);  // pointer kept; lo may be nullptr
}
```
  `setTheme(id)`: `uieng::activeLayout() = (id is a pack with a layout) ? that pointer : nullptr`. Built-ins always
  `nullptr`.

- [ ] **Step 1: Write the failing tests:**
  - `pack with layout installs override`: load fixture `sim/packs/layout-ok/` (theme.json + layout.json moving
    `stop`) via `packload.h`, `setTheme(id)` → `chrome(480,320).stop` is the placed rect.
  - `back to builtin clears it` (Review Focus 5): `setTheme(TH_UMBRAVOX)` → `activeLayout() == nullptr`.
  - `bad layout skips only the layout`: fixture `layout-bad/` (hides `menu`) → pack loads (id ≥ 0), no override,
    `why` contains `layout.json: menu cannot be hidden`.
  - `oversize layout.json skipped` (4097 bytes) with reason `layout.json over 4096 bytes`.
  - `no layout.json is fine`: existing fixture packs unchanged.
- [ ] **Step 2: Run, expect FAIL** — `make -C sim test_layout_pack && sim/test_layout_pack`.
- [ ] **Step 3: Implement.** `packload.h`: static storage for `MAX_PACKS` `LayoutOverride` + 2 `ChromeOverride`
  each. `themeload.cpp`: measure `layout.json` with the others; read it inside the same boot-loop guard bracket as
  `theme.json`; one PSRAM block of `MAX_PACKS * (sizeof(LayoutOverride) + 2 * sizeof(ChromeOverride))` allocated
  once at first use; on failure log `layout skipped: <why>` to `/diag.log` and `countSkip()`, keep the pack.
- [ ] **Step 4: Run, expect PASS**; `make -C sim test` → all pass; firmware builds:
  `~/.platformio/penv/bin/pio run -e nm-cyd-c5 -e nm-c5-node -e nm-c5-coordinator -e ws-c5-35-engine` → all SUCCESS.
- [ ] **Step 5: Commit** — `feat: packs may carry layout.json; the active theme installs it`.
- [ ] **Step 6 (hardware, owner present):** flash the CYD engine build and the Waveshare with a fixture layout pack on
  the card, run `~/.platformio/penv/bin/python tools/bootprobe.py`; record boot heap and median scan draw (must stay
  ≤ ~14 ms, heap > 55 KB). If the boards are not connected, write "not run" in the ledger; never claim it.

### Task 7: Docs

**Files:**
- Modify: `docs/theme-packs.md` (new section "layout.json (early: hand-written; the designer cannot make it yet)":
  format, piece names, rules, what fails and where the reason goes), `CHANGELOG.md` (Unreleased entry)

- [ ] **Step 1:** Write the section from this plan's Format, Content area and Global Constraints; every rule a
  person can break gets one line.
- [ ] **Step 2:** `npm test` (designer: `limits.json` and docs consistency tests still pass) and
  `git grep -nE "/home/[a-z]+"` → empty.
- [ ] **Step 3: Commit** — `docs: layout.json`.
