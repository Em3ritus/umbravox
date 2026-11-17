# UI Fonts, Art and Styling Implementation Plan (plan 4 of 6)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A layout theme can give each Scan piece its own font, colours, frame art and background, place decorative
art (`decor.N`, still or animated), and give every other engine screen a style block (fonts per text role, background
art, panel frame, list highlight, menu icons); the designer supplies built-in pixel fonts, rasterises uploaded
TTF/OTF, and converts PNG/GIF to the device art format at the exact placed size.

**Architecture:** Two new device file formats (`.fnt` pixel font, `.art` indexed image/frame strip) with hostile-input
parsers in `ui/`. Text keeps one routine: `scanr::txt` and its helpers read an active font (null = today's 5×7, so
built-in output is byte-identical) chosen by a text role or by the piece being drawn. `layout.json` grows a per-piece
object form, a per-board `style` block and `decor.N` pieces; `themelayout` resolves them into a `uieng::BoardLook`
that drawing reads beside the existing `ChromeOverride`. Fonts and art load on theme selection through the existing
packmedia loader, generalised from two files to N; drawing treats a not-yet-loaded font or art as absent.

**Tech Stack:** C++17 in `ui/` and `device/`, host tests in `sim/`, PlatformIO firmware; designer JS (ES modules,
`node --test`), wasm via emscripten.

**Spec:** `docs/superpowers/specs/2026-11-17-ui-overhaul-design.md` (Pieces; Content area, last paragraph; Device
behaviour; Fonts; Art; The designer). Builds on plans 1-3, same branch `ui-overhaul`.

**Research:** `private/agent/notes/fonts-art-map.md` (every file:line below comes from it; re-check lines before editing).

## Global Constraints

- Built-in themes stay byte-identical: `sim/test_engine_pixels` (220 fingerprints) and `make -C sim regress` pass
  unchanged after every task. No `--write` re-capture in this plan.
- A theme with no `layout.json`, or a `layout.json` with no style keys, draws exactly as on plan 3's HEAD.
- `theme.json` stays `"format": 1`; `layout.json` stays `"format": 1` (new keys only; released firmware skips unknown
  keys, verified: `themelayout.cpp` `skipValue`).
- The built-in 5×7 stays caps only. Pack fonts carry ASCII 32..126 and lowercase is drawn as lowercase.
- Pack media lives in PSRAM (`heap_caps_malloc(MALLOC_CAP_SPIRAM)`), never `.bss`; boot heap stays above 55 KB.
- Limits (new, conservative until the plan-5 calibration; each goes into `ui/themepack.h` or `ui/themelayout.h`,
  `web/designer/js/limits.json` and `docs/theme-packs.md`, held together by `limits.test.mjs`):
  `MAX_LAYOUT_JSON = 16384` (was 4096), `MAX_FONTS = 8`, `MAX_FONT = 12800`, `FONT_MIN_H = 6`, `FONT_MAX_H = 32`,
  `FONT_MAX_GLYPH_W = 32`, `MAX_ARTS = 16`, `MAX_ART = 196608`, `MAX_ART_TOTAL = 393216` (per board, per selection),
  `MAX_ART_FRAMES = 64`, `MAX_DECOR = 8`, `ANIM_DECOR_MAX = 1` per board, `ANIM_AREA_MAX` Waveshare `25600`
  (160×160) / CYD `10000` (100×100), `ART_MIN_DELAY_MS` Waveshare `50` / CYD `100`.
- File names in a pack: fonts `f0.fnt`..`f7.fnt`, art `a0.art`..`a15.art`; `layout.json` refers to them by stem
  (`"f2"`, `"a11"`). No other new names.
- Commits: author `Em3ritus <152051018+Em3ritus@users.noreply.github.com>`, trailer
  `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>` (as on this branch; the owner has not yet ruled on the
  CLAUDE.md Sonnet/`Claude-Session` wording, so do not change the branch's convention mid-branch).
- Firmware changes are not done until the six PlatformIO environments build; the boot probe is the owner's
  hardware gate and is NOT claimed by any task.

## Decisions made here (builder's calls the spec left open; flag any that are wrong)

- **`.fnt` (version 1):** 12-byte header `'U','V','F','N', 1, h, ascent, first=32, count=95, 0,0,0`; then 95 ×
  `{adv u8, w u8, off u16 LE}` (offset into the bitmap area); then bitmaps, each glyph `h` rows × `ceil(w/8)` bytes,
  MSB = leftmost. Proportional, fixed line height `h`. One file = one face at one pixel size. Integer scale still
  applies (a 6 px font at scale 2 is legal).
- **`.art` (version 1):** 16-byte header `'U','V','A','R', 1, flags, w u16, h u16, frames u16, ncol u8, 0,0,0`
  (`flags` bit 0 = index 0 is transparent); then `ncol` (2..16) × RGB565 LE; then `frames` × delay u16 ms (present
  even for 1 frame); then `frames` × `ceil(w*h/2)` bytes, 4-bit pixels, high nibble first, row-major. w 1..480,
  h 1..320. Exact length or refused.
- **Art is placed at its exact size.** Decor art must equal its rect's w×h; background art must equal the board's
  W×H; frame art is nine-sliced and only needs `w > l+r`, `h > t+b`. The device never scales art.
- **Decor draws under every functional piece**, in `decor.N` order, after the background. So no decor can cover
  MENU, STOP, nav, bridge or (plan 5) an alert card, and the existing "not covered" checks need no decor case.
- **Colours** in a style are a role name from `theme.json`'s `colors` keys or `"#rrggbb"`. A style has three:
  `ink` (stands in for TEXT and BRIGHT), `fill` (PANEL), `edge` (EDGE and EDGE_LIT). Status colours (AMBER, RED,
  band colours) are never overridden. Device check: for `menu`, `stop`, `nav.*`, `bridge`, contrast of resolved
  `ink` vs `fill` must be at least `MIN_EDGE_CONTRAST_X10` (3:1) or the layout is skipped with a reason.
- **Text roles** for the other screens: `title`, `body`, `numbers`. A screen marks its titles and numbers with a
  `RoleScope`; everything unmarked is `body`. Scan pieces with text use the piece's own font when set, else the
  board style's role font (`count.*` → numbers, `wordmark` → title, the rest → body).
- **The style block is per board** (art sizes differ), as `"style": {...}` inside the board object.
- **Text from a pack font is clipped to the rect being drawn** (the piece rect on Scan, the content area elsewhere).
  The 5×7 path is not clipped (unchanged).
- **Before its media has loaded** (the 0.4 s settle plus read time) a theme draws with the 5×7 font and no art; a
  file that fails to parse at selection leaves that font/art absent for the rest of the boot (logged), never
  retried, never fatal.
- **Icons:** one optional art per Menu entry (`icons: ["a4", ...]`, six entries, `null` allowed), drawn centred
  above the label on Menu tiles when the tile has room (icon h + label h + 8 ≤ tile h); otherwise not drawn.
- **Built-in fonts** ship as BDF sources in `web/designer/fonts/` with `LICENSES.md`; `tools/bdf2fnt.mjs` converts them
  at designer build time. Target 12; at least 8; a font whose licence cannot be verified from its upstream
  repository is dropped, not shipped "probably fine".
- **The "right to share" agreement** for fonts lives in the community repo's issue template (not in this repo);
  its wording change belongs to plan 6. This plan only lists it in the plan-6 notes of the ledger.
- **Out of scope, flagged:** sim `packload.h` and wasm `bridge.cpp` load an emblem only for `CENTRE_EMBLEM`, while the
  device also does for `CENTRE_RADAR_EMBLEM`; `MAX_PACK = 65536` is declared but never enforced. Neither is fixed
  here; both go to the owner as findings.

## Review Focus

1. A `.fnt`/`.art` file that is truncated, has offsets past the end, a glyph wider than 32, `frames = 0`, or a delay of 0: refused with a reason, no read past the buffer (Task 1, Task 3: fuzz cases).
2. Text in a pack font taller than its piece, or a long string: no ink outside the piece rect, in any strip window (Task 2, Task 6).
3. A layout that references `f5` or `a9` with no such file in the pack, or a file that fails to parse at selection: the theme still draws, with the 5×7 font and no art; no crash; the layout is skipped at boot only when the file is missing or over its cap (Task 4, Task 5, Task 9).
4. Switching themes while fonts/art are mid-load, and switching back: no dangling pointer drawn (registry cleared before buffers are reused) (Task 9).
5. An animated decor on the CYD: only its rect is invalidated per frame change, frame delay floor enforced, steady-state frames unchanged elsewhere (Task 6).

---

### Task 1: The `.fnt` pixel font format

**Files:**
- Create: `ui/fontpack.h`, `ui/fontpack.cpp`
- Create: `sim/test_fontpack.cpp`, `sim/fontfix.h` (`fontfix::make() -> std::vector<uint8_t>`: the valid test
  font below; reused by Tasks 2, 5, 6)
- Modify: `sim/Makefile` (`PAL_DEPS`, `SRC`, `TESTS`, rule), `platformio.ini` if its src filter lists `ui/*.cpp` explicitly

**Interfaces:**
- Produces: `namespace fontpack { struct Font { uint8_t h, ascent; const uint8_t* meta; const uint8_t* bits; };`
  `bool parse(const uint8_t* d, size_t n, Font& out, const char** why);`
  `int  advance(const Font&, char c);` (0 for chars outside 32..126)
  `bool pixel(const Font&, char c, int x, int y);`
  `int  width(const Font&, const char* s, int scale);` (sum of advances × scale, minus the last glyph's
  `adv - w` trailing gap × scale; 0 for "") `}`; constants `MAX_FONT = 12800`, `FONT_MIN_H = 6`,
  `FONT_MAX_H = 32`, `FONT_MAX_GLYPH_W = 32`.

- [ ] **Step 1: Write the failing tests** in `sim/test_fontpack.cpp` (the `check(what, ok)` style of
  `test_themepack.cpp`). Build a valid font in the test with a tiny encoder helper (`h=8`, `'A'` w=5 a fixed bitmap,
  `'a'` w=4, space adv 3, others w=0 adv 4). Assert: parse ok; `pixel(f,'A',0,0)` matches the bitmap;
  `pixel(f,'a',...)` differs from `'A'` (lowercase kept); `advance(f,'~') == 4`; `advance(f,'\x7f') == 0`;
  `width(f,"Aa",2)` exact. Refusals, each with a non-empty `why`: wrong magic, version 2, `h` 5 and 33, `count != 95`,
  `first != 32`, a glyph `w > 32`, an offset whose glyph runs past `n`, `n > MAX_FONT`, truncated header, and 2000
  random mutations of the valid file (no ASan/UBSan error; result either ok with every glyph in bounds or refused).
- [ ] **Step 2:** `make -C sim test_fontpack && ./sim/test_fontpack`. Expected: build fails (no `fontpack.h`).
- [ ] **Step 3: Implement** `ui/fontpack.{h,cpp}` with the format above; `parse` validates every glyph's
  `off + h*ceil(w/8) <= bitsLen` once, so `pixel` does no bounds work beyond `x < w, y < h`.
- [ ] **Step 4:** run again; Expected: all pass, clean under the sanitizer flags the Makefile uses for parser tests.
- [ ] **Step 5: Commit** `feat(ui): .fnt pixel font format and parser`.

### Task 2: Text draws through an active font

**Files:**
- Modify: `ui/scan_render.h` (`txt` :209, `fitScale` :229, `centered` :232; new hooks next to them), every
  `ui/*_render.h` and `ui/scan_alert.h` call site that hard-codes `7*scale` / `Font::H*` / `Font::ADV*` for layout
- Create: `sim/test_text_font.cpp`

**Interfaces:**
- Consumes: `fontpack::Font`, `fontpack::pixel/advance/width` (Task 1).
- Produces, in `namespace scanr`:
  `enum TextRole : uint8_t { ROLE_BODY, ROLE_TITLE, ROLE_NUMBERS, ROLE_N };`
  `void setRoleFonts(const fontpack::Font* const fonts[ROLE_N]);` (nullptr entries = 5×7)
  `struct RoleScope { explicit RoleScope(TextRole); ~RoleScope(); };` (nests, restores)
  `struct FontScope { FontScope(const fontpack::Font* f, uieng::Rect clip); ~FontScope(); };` (overrides the role
  font and sets the text clip rect while alive; `f == nullptr` leaves the role font in force)
  `int textW(const char* s, int sc);` `int lineH(int sc);` (both 5×7 metrics when no font is active:
  `Font::width(s,sc)` and `7*sc`)
  `txt`, `centered`, `fitScale` keep their signatures and use the active font.

- [ ] **Step 1: Write the failing tests** in `sim/test_text_font.cpp`: with no font set, `txt` output equals a copy of
  today's routine's output for "UMBRAVOX 123" at scales 1-3 (byte compare on a 64×32 strip); with the Task-1 test
  font under `FontScope(&f, {0,0,64,32})`: `lineH(2) == 16`, `textW("Aa",1) == fontpack::width(f,"Aa",1)`, the
  'a' glyph's pixels appear (lowercase); a string drawn at x=50 in a clip `{0,0,60,32}` leaves columns ≥ 60 untouched;
  rendering through 4-row strips and through a column window `[16,48)` gives the same pixels as one full strip;
  `RoleScope(ROLE_TITLE)` with `setRoleFonts({nullptr,&f,nullptr})` uses `f`, and after the scope ends, 5×7.
- [ ] **Step 2:** `make -C sim test_text_font && ./sim/test_text_font`; Expected: FAIL (hooks missing).
- [ ] **Step 3: Implement** the hooks: one file-static "active font + clip" pair resolved from the FontScope stack, else
  the role table, else null. `txt` keeps today's exact loop when the active font is null (no new branch inside the
  loop); a separate loop draws pack glyphs with `advance`, `pixel`, and the clip test. `fitScale` uses `textW`.
- [ ] **Step 4: Replace the hard-coded metrics** at every layout call site (`7*scale`, `Font::H*sc`,
  `Font::ADV*ns`, `Font::width(...)`) with `lineH`/`textW`. `grep -nE "7 ?\* ?(sc|scale|ns)|Font::(H|ADV|width)" ui/*_render.h ui/scan_alert.h ui/scan_render.h`
  must list only the 5×7 internals in `scan_render.h` afterwards.
- [ ] **Step 5:** `make -C sim test_text_font test_engine_pixels && ./sim/test_text_font && (cd sim && ./test_engine_pixels)`;
  Expected: all pass, 220 fingerprints unchanged.
- [ ] **Step 6: Commit** `feat(ui): text draws through an active pack font, by role or by piece`.

### Task 3: The `.art` image format and its blits

**Files:**
- Create: `ui/artpack.h`, `ui/artpack.cpp`, `sim/test_artpack.cpp`, `sim/artfix.h`
  (`artfix::make(w, h, frames, transparent) -> std::vector<uint8_t>`; reused by Tasks 5-8)
- Modify: `ui/scan_render.h` (new blits beside `emblemInto` :389), `sim/Makefile`

**Interfaces:**
- Produces: `namespace artpack { struct Art { uint16_t w, h, frames; uint8_t ncol; bool transparent;
  uint16_t pal[16]; const uint8_t* delays; const uint8_t* px; uint32_t frameBytes, cycleMs; };`
  `bool parse(const uint8_t* d, size_t n, Art& out, const char** why);`
  `uint16_t frameAt(const Art&, uint32_t nowMs);` (by cumulative delays mod `cycleMs`; 0 for one frame)
  `bool animated(const Art& a) { return a.frames > 1; }` `}`; constants `MAX_ART = 196608`, `MAX_ART_FRAMES = 64`.
- Produces, in `namespace scanr`:
  `void artInto(S&, const artpack::Art&, int x, int y, uint16_t frame);` (rect blit, index 0 skipped when
  `transparent`)
  `void nineInto(S&, const artpack::Art&, uieng::Rect r, const uint8_t slice[4]);` (`l,t,r,b`; corners copied,
  edges and centre repeated, not scaled)

- [ ] **Step 1: Write the failing tests**: parse ok for a 3-frame 7×5 art with delays 100,200,300; `frameAt` at
  0/99/100/299/300/599/600 → 0,0,1,1,2,2,0; refusals (magic, version, w 0 / 481, h 321, ncol 1 / 17, frames 0 / 65,
  any delay 0, length ±1, `n > MAX_ART`) plus 2000 mutations under the sanitizer. Blits: `artInto` with
  `transparent` leaves index-0 pixels untouched and draws the rest with `pal[]`; partially off-screen and negative x/y
  draw only the visible part; full-strip vs 3-row strips vs column window `[5,40)` identical; `nineInto` of a 9×9
  art with slice `{3,3,3,3}` into `{10,10,40,20}` has the corners equal to the source corners and nothing outside the
  rect.
- [ ] **Step 2:** `make -C sim test_artpack && ./sim/test_artpack`; Expected: FAIL.
- [ ] **Step 3: Implement** both files and the two blits, following `videoInto`'s clip pattern (rows of the strip,
  then `[lo,hi)`, index by `stride`).
- [ ] **Step 4:** run; Expected: PASS; `./sim/test_engine_pixels` unchanged.
- [ ] **Step 5: Commit** `feat(ui): .art indexed image format, rect and nine-slice blits`.

### Task 4: `layout.json` styles, style block and decor

**Files:**
- Modify: `ui/themelayout.h`, `ui/themelayout.cpp`, `ui/layout_engine.h` (new `BoardLook`, pointer in
  `LayoutOverride`), `device/themeload.cpp` (layout read buffer to `MAX_LAYOUT_JSON`, in the PSRAM `Scratch`),
  `web/designer/js/limits.json`, `docs/theme-packs.md` (layout.json section: object form, style block, decor,
  limits), `sim/test_themelayout.cpp`

**Interfaces:**
- Consumes: `themepack::Pack` colours (role resolution), `MIN_EDGE_CONTRAST_X10`.
- Produces, in `namespace uieng`:
  `struct ColorRef { uint8_t kind; uint16_t v; };` (`kind` 0 none, 1 role index, 2 RGB565)
  `struct PieceStyle { int8_t font = -1; uint8_t scale = 0; ColorRef ink, fill, edge; int8_t frame = -1; uint8_t slice[4]; int8_t bg = -1; };`
  `struct Decor { Rect r; int8_t art; };`
  `struct ScreenStyle { int8_t roleFont[3] = {-1,-1,-1}; int8_t bg = -1; int8_t panel = -1; uint8_t panelSlice[4]; ColorRef hiFill, hiEdge; int8_t icon[6] = {-1,-1,-1,-1,-1,-1}; };`
  `constexpr int PIECE_SLOTS = 19, MAX_DECOR = 8;` (`themelayout.h` adds `static_assert(P_N == uieng::PIECE_SLOTS)`:
  `themelayout` includes `layout_engine.h`, never the reverse)
  `struct BoardLook { bool any = false; PieceStyle piece[PIECE_SLOTS]; ScreenStyle screen; Decor decor[MAX_DECOR]; uint8_t nDecor = 0; uint8_t fontsUsed = 0; uint16_t artsUsed = 0; };`
  (`fontsUsed`/`artsUsed` are bitmasks of referenced `fN`/`aN`; `LayoutOverride` gains `const BoardLook* look[2]`;
  `uieng::activeLook(W)` returns it or nullptr)
  `uint16_t resolve(ColorRef c, uint16_t fallback);` (role index reads the live `pal::` value).
- `themelayout::PackLayout` gains `uieng::BoardLook look[B_N]`; `build(json, len, pack, out, why)` fills it.
  `themelayout::MAX_LAYOUT_JSON = 16384`, `MAX_DEPTH = 4`.
- JSON shape (documented in `docs/theme-packs.md`): a piece value is `[x,y,w,h]`, `"hidden"`, or
  `{"rect":[x,y,w,h], "font":"f0", "scale":2, "ink":"bright", "fill":"#102030", "edge":"edge", "frame":"a2",
  "slice":[4,4,4,4], "bg":"a3"}` (every key but `rect` optional; unknown keys skipped, so plan 5's `effect` is
  forward-compatible); a board key `"style": {"title":"f0","body":"f1","numbers":"f2","bg":"a0","panel":"a1",
  "panelSlice":[4,4,4,4],"highlight":{"fill":"witch","edge":"witchHi"},"icons":["a4",null,...]}`; board keys
  `"decor.0"`..`"decor.7"`: `{"rect":[...],"art":"a5"}`.

- [ ] **Step 1: Write the failing tests** in `sim/test_themelayout.cpp`: object form equals the array form's rect;
  each style key lands in `PieceStyle`; `"#ff0000"` → kind 2, `0xF800`; an unknown role name, `"f8"`, `"a16"`,
  `"g1"`, scale 0 or 5, `slice` with 3 numbers, a 9th decor, `decor.8`: refused with a reason; `fontsUsed`/`artsUsed`
  bitmasks exact; `menu` with ink = fill = `"panel"` → layout refused for contrast; decor rect off screen refused;
  a 16000-byte valid layout parses, 16385 bytes refused; depth-5 nesting refused; the existing hostile-input fuzz
  run extended with the new shapes. `test_layout_override` still passes (a layout with no style keys gives
  `look.any == false`).
- [ ] **Step 2:** `make -C sim test_themelayout && ./sim/test_themelayout`; Expected: FAIL on the new cases only.
- [ ] **Step 3: Implement** in `themelayout.cpp` (`slotValue` accepts the object; a `parseStyle`, a `parseDecor`;
  name checks `f[0-7]`, `a([0-9]|1[0-5])`; checks in `resolve`); keep `char k[16]` (`"panelSlice"` fits).
- [ ] **Step 4: Raise the cap everywhere**: `limits.json` `MAX_LAYOUT_JSON` 16384; the device read buffer; docs.
  Run `make -C sim test_themelayout test_layout_override test_layout_pack test_engine_pixels` and each binary, and
  `cd web/designer && node --test test/limits.test.mjs test/limitations.test.mjs`. Expected: PASS.
- [ ] **Step 5: Commit** `feat(ui): layout.json piece styles, per-board style block and decor pieces`.

### Task 5: The asset registry and host loading

**Files:**
- Create: `ui/packassets.h`, `ui/packassets.cpp`, `sim/test_packassets.cpp`
- Modify: `sim/packload.h` (load referenced `fN.fnt` / `aN.art` from the pack folder), `web/designer/wasm/bridge.cpp`
  (new export), `web/designer/js/preview.js` (pass fonts/art through), `sim/Makefile`

**Interfaces:**
- Produces: `namespace assets { void set(uint8_t themeId, int board, const fontpack::Font* fonts[MAX_FONTS],
  const artpack::Art* arts[MAX_ARTS]); void clear(uint8_t themeId);`
  `const fontpack::Font* font(int i); const artpack::Art* art(int i);` (nullptr unless the active theme's set is
  installed and that entry parsed) `}`. `pal::setTheme` calls `scanr::setRoleFonts` from the active `BoardLook`'s
  `screen.roleFont` via `assets::font`.
- wasm: `int uv_load_media(int themeIndex, int kind /*0 font, 1 art*/, int slot, const uint8_t* data, int len);`
  returns 0 or the existing `UV_ERR_*` codes (new `UV_ERR_MEDIA = -8`); `preview.loadDraft(theme, emblem, clip,
  layout, media)` where `media = { fonts: {f0: Uint8Array, ...}, arts: {a0: ...} }`.

- [ ] **Step 1: Write the failing tests**: `test_packassets`: `font(0)` null before `set`; after `set` for theme 3,
  `font(i)` returns the entry while theme 3 is active and null after switching to theme 4; `clear(3)` nulls it;
  `test_layout_pack` gains a fixture pack folder (`sim/packs/styled/`: theme.json, layout.json, `f0.fnt`,
  `a0.art`, written by a small generator in the test or checked-in bytes) that loads with its media installed.
  Designer: in `test/wasm-parity.test.mjs` (not a new file: a second wasm test file races the build) a case loads
  the styled fixture through `uv_load_media` and renders Scan identical to the sim's render of the same pack.
- [ ] **Step 2:** run `make -C sim test_packassets test_layout_pack` and each; Expected: FAIL.
- [ ] **Step 3: Implement** the registry (pointers only; buffers belong to the loader), `packload.h` and the bridge.
- [ ] **Step 4:** run both sim tests and `npm test`; Expected: PASS (`npm test` count = previous 368 + new).
- [ ] **Step 5: Commit** `feat(ui): pack font and art registry; sim and wasm load them`.

### Task 6: Scan draws piece styles, decor and background

**Files:**
- Modify: `ui/scan_render.h` (`drawChrome` :816-828: background art after the VOID clear, decor, per-piece surface
  and text styles; count tiles, channel, wordmark text), `ui/scan_present.h` (animated decor dirty region),
  `ui/themelayout.cpp` (animated decor caps in `resolve`, needs art header sizes: see Step 3)
- Create: `sim/test_style_render.cpp`

**Interfaces:**
- Consumes: `BoardLook`, `uieng::activeLook`, `assets::font/art`, `FontScope`, `artInto`, `nineInto`, `resolve`.
- Produces: `scanr::pieceSurface(S&, int piece, uieng::Rect r)` (fill + edge, or nine-slice frame when set and
  loaded; exactly today's box/frame when the piece has no style) and `scanr::PieceTextScope` (a `FontScope` on the
  piece's font, or its role font, clipped to the piece rect; also swaps `pal::TEXT/BRIGHT`, `PANEL`,
  `EDGE/EDGE_LIT` to the piece's resolved colours and restores them on exit).

- [ ] **Step 1: Write the failing tests** in `test_style_render.cpp` using the Task-5 fixture pack on both boards:
  a piece with `fill:"#ff0000"` has red surface pixels only inside its rect; `wordmark` in `f0` shows the font's
  'a' glyph; background art pixels appear where no piece draws; decor pixels appear in the decor rect and are
  overdrawn by a functional piece that overlaps it; nothing outside rects + body in any strip height 1..40;
  with the art and font entries missing from the registry, the render equals the render of the same layout with
  the style keys stripped except the fill/ink/edge colours (graceful absence); `pal::` colours after a frame equal
  those before (scopes restored); an animated decor's rect is in the frame's dirty list exactly when `frameAt`
  changes. `test_engine_pixels` unchanged.
- [ ] **Step 2:** `make -C sim test_style_render && (cd sim && ./test_style_render)`; Expected: FAIL.
- [ ] **Step 3: Implement.** Animated caps: `resolve` cannot see art headers at boot, so the boot check covers
  the decor count only (which decor is animated is unknown until its file loads); the per-frame cap is enforced in drawing: only the first
  animated decor (by `decor.N` order) whose area ≤ `ANIM_AREA_MAX[board]` animates, others show frame 0, and
  frame delays below `ART_MIN_DELAY_MS[board]` are treated as that floor. Same rule in the designer checks (Task 12).
- [ ] **Step 4:** run `test_style_render`, `test_engine_pixels`, `test_layout_override`, `test_clean_radar` (from
  inside `sim/`); Expected: PASS, fingerprints unchanged.
- [ ] **Step 5: Commit** `feat(ui): Scan draws piece styles, decor and background art`.

### Task 7: Style block on the other screens (panels, highlight, roles): Detail, Codex, Aspect, Omens, Files

**Files:**
- Modify: `ui/scan_render.h` (new helpers), `ui/detail_render.h`, `ui/codex_render.h`, `ui/aspect_render.h`,
  `ui/omens_render.h`, `ui/files_render.h`; `sim/test_style_render.cpp` (new cases)

**Interfaces:**
- Produces, in `namespace scanr`: `void panel(S&, uieng::Rect r, uint16_t fill, uint16_t edge);` (the screen
  style's nine-slice panel art when set and loaded, else exactly `box(fill)` + `frame(edge)`),
  `void highlight(S&, uieng::Rect r, uint16_t fill, uint16_t edge);` (the style's `hiFill`/`hiEdge` when set, else
  the caller's colours), and screen background art drawn by `drawChrome` after the VOID clear when
  `screen.bg` is set and loaded (Scan included).
- Mapping (from the research map §6): every `box(...mix(VOID,PANEL,..)) + frame(EDGE)` pair that draws a panel
  becomes `panel`; Files selected row (:98-99) and Codex active filter cell (:83-85) become `highlight`; titles
  (`txt(..., 2, BRIGHT)` / `centered(... fitScale(..,2), BRIGHT)`) go in `RoleScope(ROLE_TITLE)`; Detail's RSSI
  numbers and any scale ≥ 2 numeric readout in `RoleScope(ROLE_NUMBERS)`.

- [ ] **Step 1: Write the failing tests**: per screen in this task, with the fixture's style block: panel art pixels
  inside a panel rect, highlight colour on the selected row (Files) and active filter (Codex), the title font's
  'a' glyph in the title, nothing drawn outside the content area plus the header/footer pieces.
- [ ] **Step 2:** run; Expected: FAIL.
- [ ] **Step 3: Implement** the helpers and the five screens' conversions.
- [ ] **Step 4:** `test_style_render`, `test_engine_pixels` (unchanged), and the five screens' existing tests
  (`grep -l "detail_render\|codex_render\|aspect_render\|omens_render\|files_render" sim/test_*.cpp`); Expected: PASS.
- [ ] **Step 5: Commit** `feat(ui): style block on Detail, Codex, Aspect, Omens, Files`.

### Task 8: Style block on Domain, Mesh, Menu (with icons), Echoes, Attune

**Files:** `ui/domain_render.h`, `ui/mesh_render.h`, `ui/menu_render.h`, `ui/echoes_render.h`,
`ui/attune_render.h`, `ui/settings_controls.h`, `ui/sub_render.h`; `sim/test_style_render.cpp`.

**Interfaces:** Consumes Task 7's `panel`, `highlight`, `RoleScope`; Menu reads `screen.icon[i]` via `assets::art`.
Mesh and Aspect tab selection use `highlight`.

- [ ] **Step 1: Write the failing tests** as Task 7 for these screens, plus: Menu icon pixels centred above the label
  when the tile has room; on the CYD Menu tile without room, no icon pixels and the label unchanged; an icon slot
  of `null` draws nothing.
- [ ] **Step 2:** run; Expected: FAIL.
- [ ] **Step 3: Implement.**
- [ ] **Step 4:** `test_style_render`, `test_engine_pixels` (unchanged), `make -C sim regress` (unchanged); Expected: PASS.
- [ ] **Step 5: Commit** `feat(ui): style block on Domain, Mesh, Menu icons, Echoes, Attune`.

### Task 9: The device loads fonts and art on selection; USB install accepts them

**Files:**
- Modify: `ui/packmedia.h` (`Progress` from 2 files to N), `sim/test_packmedia.cpp`, `device/themeload.cpp`
  (`PackMedia` manifest, `measure` :160, `totalOf` :171, `Scratch` size arrays :64, `visit` :352 pre-checks,
  `beginLoad` :247, `stepLoad`, `finishFile`, buffers), `device/sdcmd.h` (:252-258 file kinds), `tools/sdput.py`
  (if it lists file names), `sim/` sdcmd console tests

**Interfaces:**
- Produces: `packmedia::Progress::start(const uint32_t* sizes, int n)` with `n ≤ MEDIA_FILES_MAX = 2 + MAX_FONTS +
  MAX_ARTS` (26); `Slice.file` is the manifest index; `done()` is `file_ >= n_`. Manifest per pack (PSRAM, built at
  boot in `visit` from the board's `BoardLook.fontsUsed/artsUsed`): `{kind, slot, size}`; a referenced file missing or
  over `MAX_FONT`/`MAX_ART`, or arts over `MAX_ART_TOTAL`, skips the **layout** at boot with a logged reason.
  `fontBuf` (`MAX_FONTS × MAX_FONT`) and `artBuf` (`MAX_ART_TOTAL`) allocated in PSRAM on first need and reused.
  On UNLOAD: `assets::clear(id)` **before** any buffer is overwritten; on LOAD success: parse each file, `assets::set`.
  `sdcmd` file kinds gain `K_LAYOUT` (`layout.json`, ≤ `MAX_LAYOUT_JSON`; missing today though the designer sends
  it), `K_FONT` (`f[0-7].fnt`), `K_ART` (`a([0-9]|1[0-5]).art`), each with its cap.

- [ ] **Step 1: Write the failing tests**: `test_packmedia`: N-file progress with sizes `{0, 5000, 40000, 0, 7}`
  slices in order, skips zero-size files, `done()` exact; a 26-file manifest; the existing two-file cases rewritten
  to the new `start` and still passing. sdcmd console tests: `put /themes/x/layout.json`, `f7.fnt`, `a15.art`
  accepted; `f8.fnt`, `a16.art`, `font.fnt`, oversize files refused with today's reason style.
- [ ] **Step 2:** `make -C sim test_packmedia sdcmd_console` and run; Expected: FAIL.
- [ ] **Step 3: Implement** `Progress`, then `themeload.cpp` and `sdcmd.h`. The guard key (`totalOf`) includes every
  manifest file's size, so editing a font gives a crashed pack another try.
- [ ] **Step 4:** host tests above PASS; `~/.platformio/penv/bin/pio run -e nm-cyd-c5 -e nm-cyd-c5-engine
  -e ws-c5-35-engine -e ws-c5-28-engine -e nm-c5-node -e nm-c5-coordinator` builds all six. Record RAM/flash deltas
  from the build summary in the ledger. Do NOT claim the boot probe.
- [ ] **Step 5: Commit** `feat(device): fonts and art load on theme selection; USB install accepts layout, font and art files`.

### Task 10: Designer fonts: format, built-in set, TTF/OTF rasteriser

**Files:**
- Create: `web/designer/js/fontfmt.js`, `web/designer/fonts/*.bdf`, `web/designer/fonts/LICENSES.md`,
  `tools/bdf2fnt.mjs`, `web/designer/test/fontfmt.test.mjs`
- Modify: `tools/assemble-designer.sh` (convert BDF → `fonts/*.fnt` + `fonts/index.json` at build), `bridge.cpp`
  (`int uv_font_check(const uint8_t*, int)` → 0 or `UV_ERR_MEDIA`), `test/wasm-parity.test.mjs`

**Interfaces:**
- Produces (`fontfmt.js`): `encodeFont({h, ascent, glyphs: [{code, adv, w, rows: Uint8Array[]}]}) -> Uint8Array`,
  `decodeFont(bytes) -> same shape`, `glyphsFromAlpha(alpha, w, h, {threshold=128}) -> rows`,
  `async rasteriseFont(fontFace, px) -> encodeFont input` (canvas `fillText` per glyph 32..126, alpha threshold;
  thin, tested in the browser run only), `parseBdf(text) -> encodeFont input` (used by `bdf2fnt.mjs`).
  `fonts/index.json`: `[{id, name, px, licence, file}]`.

- [ ] **Step 1: Write the failing tests**: encode/decode round trip; `glyphsFromAlpha` on a synthetic 8×8 alpha
  square; `parseBdf` on a 3-glyph inline BDF; every `fonts/*.bdf` converts and decodes with `h` in 6..32 and 95
  glyphs; `LICENSES.md` has one entry per BDF with licence name and upstream URL (test checks the file list matches);
  wasm: `uv_font_check` accepts every converted built-in and refuses a corrupted one (in `wasm-parity.test.mjs`).
- [ ] **Step 2:** `cd web/designer && node --test test/fontfmt.test.mjs`; Expected: FAIL.
- [ ] **Step 3: Choose the fonts.** Candidates to verify (licence from the upstream repository, not memory):
  Spleen, Terminus, Cozette, Unscii, ProFont, Gohufont, Scientifica, Tamzen, Creep, Tom Thumb, Dina, Lemon.
  Record each verified licence and URL in `LICENSES.md`; drop any not verified; ship at least 8.
- [ ] **Step 4: Implement** `fontfmt.js`, `bdf2fnt.mjs`, the assemble step and the bridge export.
- [ ] **Step 5:** `npm test`; Expected: PASS.
- [ ] **Step 6: Commit** `feat(designer): pixel font format, built-in font set, TTF/OTF rasteriser`.

### Task 11: Designer art: PNG and GIF to `.art` at the placed size

**Files:**
- Create: `web/designer/js/artfmt.js`, `web/designer/test/artfmt.test.mjs`
- Modify: `web/designer/js/radarvideo.js` (`grabGifFrames` :219 gains `{w, h, crop:'cover'}` and returns per-frame
  delays; the radar path keeps its 102 square and existing tests), `bridge.cpp` (`uv_art_check`)

**Interfaces:**
- Produces: `imageToArt(rgbaFrames, srcW, srcH, {w, h, delays, transparent}) -> {w,h,frames,pal,delays,px}` (cover
  crop then box downsample to exactly `w×h`; one shared ≤16-colour palette over all frames via the existing
  `quantizeColour`; alpha < 128 → index 0 when `transparent`), `artToBin(a) -> Uint8Array`, `readArt(bytes)`,
  `artBytes(w, h, frames, ncol)` (for the designer's size checks).

- [ ] **Step 1: Write the failing tests**: round trip; exact output size for 3 targets (decor 37×61, CYD background
  320×240, frame 12×12); transparent pixels → index 0; a 5-frame GIF plan keeps the per-frame delays, floored to
  `ART_MIN_DELAY_MS` of the board; `artBytes` matches `artToBin(...).length`; wasm `uv_art_check` accepts the
  outputs (in `wasm-parity.test.mjs`).
- [ ] **Step 2:** run; Expected: FAIL. **Step 3:** implement. **Step 4:** `npm test` PASS.
- [ ] **Step 5: Commit** `feat(designer): PNG and GIF art converted to the device format at the placed size`.

### Task 12: Designer Layout step: style controls, decor, style block, pack files

**Files:**
- Modify: `web/designer/index.html` (`#view-layout` side panel :175-205: style controls; a "Screens" style panel with
  a screen picker for the preview), `web/designer/js/app.js` (`syncLayout` :809, `layoutText` :768, `currentPack`
  :489), `web/designer/js/layout.js` (per-piece value `{rect, hidden, style}`, `style` block and `decor` in
  `toJson`/`fromJson`/`withDrafts`), `web/designer/js/pack.js` (`buildPack` takes `media`), `checks.js`,
  `usbinstall.js` (`CAPS`, `SHAPE`, `ordered` gain the new names), `install.js` (:36 stale list),
  `limits.json` `limitations`, `docs/theme-packs.md`; tests `layout.test.mjs`, `pack.test.mjs`,
  `checks.test.mjs`, `usbinstall.test.mjs`, `layout-wiring.test.mjs`

**Interfaces:**
- Consumes: Tasks 10-11 (`fontfmt`, `artfmt`), Task 5 (`preview.loadDraft(..., media)`).
- Produces: `buildPack(theme, emblem, clip, sounds, layout, media)` writes `fN.fnt` per distinct (face, px) in use
  (≤ 8, error beyond) and `aN.art` per distinct (image, w, h) per board (≤ 16, error beyond), and rewrites the
  layout's references to those stems; `withDrafts` re-converts art to the drafted board's placed sizes (art is never
  shared across boards unless sizes match).

- [ ] **Step 1: Write the failing tests**: `layout.test.mjs`: style and decor survive `toJson`/`fromJson`; drafting
  carries styles and recomputes art sizes. `pack.test.mjs`: two pieces with the same font at the same px → one
  `.fnt`; 9 distinct fonts → error naming the limit; per-board art files; the emitted layout.json passes
  `preview.checkLayout`. `checks.test.mjs`: menu ink/fill contrast below 3:1 → error; a second animated decor →
  warn ("shows its first frame on the device"); an animated decor over `ANIM_AREA_MAX` → warn. `usbinstall.test.mjs`:
  a pack with fonts and art installs in the order art/fonts → `layout.json` → `theme.json`, each under its cap.
- [ ] **Step 2:** `npm test`; Expected: FAIL on the new cases.
- [ ] **Step 3: Implement** the model, pack and checks changes, then the UI: piece panel (font: built-in list +
  "Upload TTF/OTF…" + px size; scale; ink/fill/edge role dropdown or colour; frame art + slice; background art),
  "Add picture" (new `decor.N` at the board centre, drag/resize like any piece), Screens panel (role fonts,
  background, panel frame + slice, highlight colours, six icon slots) with the preview switchable to each screen
  through `uv_render_engine`.
- [ ] **Step 4:** `npm test` PASS; headless-Chrome run of the Layout step (as plan 3 did) with screenshots to
  `private/renders/style-step-*.png`: a styled piece, decor, a non-Scan screen with the style block, both boards.
- [ ] **Step 5: Commit** `feat(designer): piece styles, decor and the screens style block in the Layout step`.

### Task 13: Whole-branch verification

- [ ] **Step 1:** `make -C sim test` (30-45 min, run in the background), `make -C sim regress`,
  `(cd sim && ./test_engine_pixels)`, `npm test`, the six-environment `pio run`. Record every result in the ledger.
- [ ] **Step 2:** `git grep -nE "/home/[a-z]+"` empty; `git log main..HEAD --format=%B | grep -c "Co-Authored-By"`
  equals the commit count.
- [ ] **Step 3:** Update `private/agent/HANDOFF.md` hardware list: boot probe now also with a pack carrying 8 fonts,
  16 arts at `MAX_ART_TOTAL` and an animated decor on each board (heap > 55 KB, median scan draw ≤ ~14 ms,
  selection load time noted). Not run by this plan.
