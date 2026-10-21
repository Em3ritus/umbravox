# Theme Packs Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Let anyone make a data-only theme pack (a folder on the SD card), load it on the CYD, and build/install one from a static web designer with a live preview of the real UI code.

**Architecture:** A pure, bounded parser (`ui/themepack.*`) turns `theme.json` + `emblem.bin` into a `Pack`; `palette.cpp` gains a small runtime table of packs after the `constexpr` built-ins (dynamic theme count, lookup by name). A generalised emblem/"motif" drawing routine (`ui/motif.*`) replaces the muffin-only code so every centrepiece/alert pair is defined. A device loader reads `/themes/*` at boot with a boot-loop guard. The designer is a static site: the real `ui/` compiled to WebAssembly for the preview, plain JS modules for conversion/checks/zip/install.

**Tech Stack:** C++17 (`ui/` shared host+device), PlatformIO, host tests (`sim/`, `make test`/`regress`), Node 20 `node --test` for the designer's JS, emscripten (WebAssembly, **not installed here**), GitHub Pages.

**Spec:** `docs/superpowers/specs/2026-10-21-theme-packs-design.md`

## Global Constraints

- Pack format `1`; unknown keys ignored; `format > 1` skipped with a reason.
- Limits: 8 packs (first 8 by folder name); `theme.json` ≤ 4 KB; `emblem.bin` ≤ 96×96, ≤ 16 colours, ≤ 9.5 KB (9728 B); whole pack ≤ 64 KB; `name`/`header`/`author` ≤ 20/18/24 chars; folder name `[A-Za-z0-9_-]{1,31}`.
- `name`: chars `A-Z 0-9 space - .`, unique, not `UMBRAVOX` / `BAPH0MAU5` / `CVLT OF BAPH0MAU5` / `COSMIC MUFFIN` (every name a built-in displays; ruled during Task 6). `header`: font chars only (ASCII 32..95, lowercase folded).
- 20 colour roles, `#RRGGBB` → RGB565: `void panel grid edge edgeLit dim text bright witch witchHi bone sweep sweepHi live liveHi magenta ice green amber red`. Pack ramp = `void, edge, dim, bright`.
- Sweeps offered: `radar pulse star candle galaxy orbit starfall pulsar comet aurora` (no `gaze`). Alerts: `ward supernova vortex comet lockon` (no `summon`). Centrepiece: `emblem` | `heptagram`. Dial: `compact` | `large`.
- Alert edge contrast minimum about 3 : 1 against `void` and `panel`, enforced where the edge is drawn with fixed safe colours; `amber`/`red` replaced with UMBRAVOX's values when they fail. Chrome is never rewritten.
- A pack is data only; it must never boot-loop, hang, hide alerts or run code.
- **UMBRAVOX render baselines stay byte-identical** (`make -C sim regress`); `Settings` stays 12 bytes (canary); `test_trig` keeps passing for pack themes.
- 160×120 canvas, no FPU: no new per-frame cost beyond one emblem blit.
- Designer is static (no backend). Every limitation in the spec's list of 13 must appear in the designer, on the Install page, in `docs/theme-packs.md`, linked from the README, and on the device (skipped count on ASPECT + diag log).
- Commit trailer on every commit: `Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>`. Run `make -C sim test` before each commit.

## Review Focus

1. **Hostile `theme.json` / `emblem.bin`** (truncated, NUL bytes, huge numbers, 10 000-deep nesting, giant strings, duplicate keys): must be rejected with a reason, never crash or read out of bounds. → Task 1, Task 2 (ASan fuzz).
2. **Selected pack removed/renamed/invalidated between boots:** device falls back to UMBRAVOX and logs, never to an arbitrary pack. → Task 4 (`resolveSaved`), Task 7.
3. **Sweeps list that is empty, omits the start sweep, or omits `radar`:** pack skipped (empty / start not offered); a missing `radar` must fall back to the pack's start sweep. → Task 1, Task 4.
4. **Unreadable palette (all black, text = background):** pack still loads, alerts stay visible (safe edge colours), nothing divides by zero in the designer's checks. → Task 3, Task 10.
5. **Emblem extremes on the smallest dial** (1×1, 96×96, 2 colours, on the `compact` dial radius 22): draws only inside the circle. → Task 5.

---

### Task 1: Pack parser (`theme.json`)

**Files:**
- Create: `ui/themepack.h`, `ui/themepack.cpp`, `sim/test_themepack.cpp`
- Modify: `sim/Makefile` (rule + `TESTS`), `.gitignore`

**Interfaces:**
- Produces (`ui/themepack.h`, namespace `themepack`): constants `FORMAT=1, MAX_PACKS=8, MAX_NAME=20, MAX_HEADER=18, MAX_AUTHOR=24, MAX_JSON=4096, MAX_EMBLEM=9728, MAX_PACK=65536, MAX_EMBLEM_DIM=96, MAX_EMBLEM_COLORS=16, N_COLORS=20`; `enum Dial{DIAL_COMPACT,DIAL_LARGE}`, `enum Centre{CENTRE_HEPTAGRAM,CENTRE_EMBLEM}`, `enum Rite{RITE_WARD,RITE_SUPERNOVA,RITE_VORTEX,RITE_COMET,RITE_LOCKON}`; `enum Role : uint8_t{R_VOID,R_PANEL,R_GRID,R_EDGE,R_EDGELIT,R_DIM,R_TEXT,R_BRIGHT,R_WITCH,R_WITCHHI,R_BONE,R_SWEEP,R_SWEEPHI,R_LIVE,R_LIVEHI,R_MAGENTA,R_ICE,R_GREEN,R_AMBER,R_RED}`; `struct Pack{char name[21],author[25],header[19]; Dial dial; Centre centre; Rite rite; uint16_t sweeps; uint8_t sweep; uint16_t color[N_COLORS];}` (`sweeps` is a bitmask over `ui::Sweep`, `color` RGB565); `bool parse(const char* json, size_t n, Pack& out, char* why, size_t whyCap)`.

- [ ] **Step 1: Write the failing test** `sim/test_themepack.cpp`. A helper builds a valid JSON string from a base (all 20 colours) so each case mutates one thing. Cases, each asserting the `bool` and, for rejects, that `why` is non-empty: valid pack parses and fields/colours convert (`#09071C` → `pal::rgb(9,7,28)` value); `format` 2 rejected; unknown key ignored; missing `name`/each colour rejected; `name` 20 chars ok / 21 rejected / lowercase or `$` rejected; `header` 18 ok / 19 rejected, lowercase folded to capitals; `sweeps` empty rejected, `sweep` not in `sweeps` rejected, `gaze` rejected, `radar` absent but `sweep:"galaxy"` accepted; `alert:"summon"` rejected; `dial`/`centrepiece` bad strings rejected; bad hex (`#GG0000`, `#12345`, missing `#`) rejected; duplicate key rejected; wrong types (number for string) rejected; input of 4096 bytes accepted and 4097 rejected; empty input, `{`, `[]`, trailing garbage, 10 000-deep nesting, a 100 KB string, embedded NUL: all rejected without crashing.
- [ ] **Step 2: Run it, verify it fails** — `make -C sim test_themepack` → FAIL (no `ui/themepack.h`).
- [ ] **Step 3: Implement `themepack::parse`** in `ui/themepack.cpp`. Hand-written bounded recursive-descent over the `n` bytes (never assumes NUL termination), object-of-scalars plus one array of strings plus one object (`colors`); depth cap 4; reject duplicate keys; no heap allocation; every string copied with its limit checked first; `why` filled with a short reason. Sweep names map to `ui::Sweep` ids.
- [ ] **Step 4: Add a mutation fuzz case** to the test: 20 000 iterations flipping/inserting/deleting random bytes of the valid JSON (fixed seed), build target with `-fsanitize=address,undefined` (same Makefile style as `test_ble_ad`); the assertion is only "no crash, no sanitizer report".
- [ ] **Step 5: Run tests** — `make -C sim test` → all pass.
- [ ] **Step 6: Commit** (`feat: theme pack parser, bounded and fuzzed`).

---

### Task 2: Emblem parser (`emblem.bin`)

**Files:** Modify `ui/themepack.h`, `ui/themepack.cpp`, `sim/test_themepack.cpp`

**Interfaces:**
- Produces: `struct Emblem{uint8_t w,h,ncol; uint16_t pal[16]; const uint8_t* px;}` (`px` points into the caller's buffer, `w*h` indices); `bool parseEmblem(const uint8_t* data, size_t n, Emblem& out, char* why, size_t whyCap)`. Format: bytes `'U','V','E',1`, `w`, `h`, `ncol`, reserved `0`, `ncol` little-endian RGB565, then `w*h` indices.

- [ ] **Step 1: Write failing tests** (same file): valid 72×72/16 colours parses; 96×96 ok, 97×96 and 96×97 rejected; `w` or `h` 0 rejected; `ncol` 1 and 17 rejected, 2 and 16 ok; wrong magic/version rejected; reserved byte non-zero rejected; truncated at every length from 0 to full-1 rejected; extra trailing bytes rejected; an index `>= ncol` rejected; size above 9728 rejected. Add an ASan mutation loop as in Task 1.
- [ ] **Step 2: Run, verify fail** — undefined `parseEmblem`.
- [ ] **Step 3: Implement `parseEmblem`**; validate the total length arithmetic in 32-bit without overflow before touching `px`; check every index.
- [ ] **Step 4: Run tests** — `make -C sim test` → pass.
- [ ] **Step 5: Commit** (`feat: emblem.bin parser`).

---

### Task 3: Alert-edge contrast and status sanitising

**Files:** Create `ui/contrast.h`, `sim/test_contrast.cpp`; Modify `ui/screen_scan.cpp` (`drawAlert` edge selection ~line 584), `ui/themepack.h`/`.cpp`, `sim/Makefile`

**Interfaces:**
- Produces (`ui/contrast.h`, namespace `ui`): `int contrastX10(uint16_t a565, uint16_t b565)` (WCAG ratio × 10); `constexpr int MIN_EDGE_CONTRAST_X10 = 30`; `uint16_t safeEdge(uint16_t col, uint16_t safe, uint16_t bg1, uint16_t bg2)` (returns `col` if contrast vs **both** backgrounds ≥ minimum, else `safe`); fixed safe colours `SAFE_HIGH=#EF4444`, `SAFE_MED=#F59E0B`, `SAFE_LOW=#D6C2FF` (RGB565). `themepack::sanitizeStatus(Pack&)` replaces `amber`/`red` with `#F59E0B`/`#EF4444` when their contrast vs `void` and `panel` is below the minimum and returns whether it changed anything.
- Consumes: `pal::VOID`, `pal::PANEL`, `pal::RED/AMBER/WITCH`.

- [ ] **Step 1: Write failing tests:** black on white = 210 (21.0), identical colours = 10; `safeEdge` keeps a high-contrast colour and swaps a near-background one; threshold at and one below 30; `sanitizeStatus` swaps only failing roles; **an all-black pack** (every role `#000000`) through `drawAlert` at each confidence level still produces edge pixels that differ from `void` (the safe colours), for every rite.
- [ ] **Step 2: Run, verify fail.**
- [ ] **Step 3: Implement** the helpers; in `drawAlert` wrap the three confidence edge choices with `safeEdge(..., SAFE_*, pal::VOID, pal::PANEL)`.
- [ ] **Step 4: Run** `make -C sim test` and `make -C sim regress` → pass, "UMBRAVOX unchanged".
- [ ] **Step 5: Commit** (`feat: alert edges stay visible on any palette`).

---

### Task 4: Runtime theme table, dynamic count, saved-by-name

**Files:** Modify `ui/palette.h`, `ui/palette.cpp`, `ui/muffinseal.h`/`.cpp` (add `muffinEmblem()`), `ui/nav.cpp` (`adjustAspect`, `clampSettings`), `device/prefs.cpp`, `sim/Makefile` (every rule that links `palette.cpp` must also link `themepack.cpp` and `muffinseal.cpp`: `test_palette`, `test_sweep`, and any other), `sim/test_palette.cpp`, `sim/test_settings.cpp`, `sim/test_nav.cpp`

**Interfaces:**
- Consumes: `themepack::Pack`, `themepack::Emblem`, `themepack::sanitizeStatus` (Tasks 1-3).
- Also produces `const themepack::Emblem& ui::muffinEmblem()` in `ui/muffinseal.h` (built from `ui/muffin.h`'s `MUFFIN_PAL`/`MUFFIN_PX`, 72×72, 16 colours); `pal::emblem(TH_COSMICMUFFIN)` returns it.
- Produces (`namespace pal`): `int themeCount()` (built-ins + packs); `int addPack(const themepack::Pack&, const themepack::Emblem* e)` → theme id (`TH_COUNT + slot`) or `-1` when full (the emblem is stored by pointer; the caller keeps its buffer alive); it runs `sanitizeStatus` on its own copy; `int findPack(const char* name)`; `uint8_t resolveSaved(const char* name, uint8_t savedId)` (non-empty `name` → `findPack` or `TH_UMBRAVOX`; empty → `savedId` if a built-in else `TH_UMBRAVOX`); `enum Centrepiece{CP_HEPTAGRAM,CP_SEAL,CP_EMBLEM}`; `Centrepiece centrepiece(uint8_t id)`; `const themepack::Emblem* emblem(uint8_t id)` (the built-in muffin for COSMIC, the pack's for packs, else null); `const char* header(uint8_t id)`; `uint8_t startSweep(uint8_t id)` (RADAR for built-ins); `uint8_t packRite(uint8_t id)` (`themepack::Rite`, meaningful for packs). `TH_COUNT` keeps meaning "built-in themes" (3). A pack's `Row` takes its geometry from the UMBRAVOX row (`compact`) or the COSMIC row (`large`), its colours from the pack (ramp `void,edge,dim,bright`), its sweep mask from `Pack::sweeps`.
- `Settings` is unchanged; `Settings.theme >= TH_COUNT` means "pack slot". `prefs` stores the pack name in NVS key `"pack"` whenever `theme >= TH_COUNT` (clears it otherwise), and `load` applies `resolveSaved` before clamping.

- [ ] **Step 1: Write failing tests:** adding 8 packs returns ids 3..10 and the 9th returns -1; `themeCount()` tracks; `findPack` exact-match only; `setTheme(packId)` applies the pack's colours and dial geometry (compact vs large rows); `sweepMask(packId)` equals the pack's mask; `resolveSaved("X", 4)` with X present at id 5 → 5, with X absent → 0, with empty name and savedId 4 → 0, with empty name and savedId 2 → 2; theme stepping in `test_nav` visits built-ins then packs and wraps; `clampSettings` keeps a valid pack id, snaps an id ≥ `themeCount()` to 0; **a pack that omits `radar` and whose selected sweep is withdrawn clamps to its own `startSweep`**; `addPack` of a pack whose `amber`/`red` fail the contrast check stores UMBRAVOX's values; `test_settings` canary unchanged at 12 bytes and its exhaustive theme loop covers packs.
- [ ] **Step 2: Run, verify fail.**
- [ ] **Step 3: Implement** `PACKS[themepack::MAX_PACKS]` beside `THEMES[]` in `palette.cpp` with `row(id)` covering both; switch `adjustAspect` and `clampSettings` to `themeCount()`; fall back to `startSweep(theme)` in `clampSettings`; add the `"pack"` NVS key to `prefs::write`/`load` (device-only, no host test; covered by the Task 7 hardware step).
- [ ] **Step 4: Run** `make -C sim test`, `regress` → pass.
- [ ] **Step 5: Commit** (`feat: runtime pack table, dynamic theme count, saved by name`).

---

### Task 5: Emblem and motif drawing

**Files:** Create `ui/motif.h`, `ui/motif.cpp`, `sim/test_motif.cpp`; Modify `ui/muffinseal.cpp` (becomes a thin wrapper), `ui/screen_scan.cpp` (`drawDialAt` centrepiece branch, `alertFigure`, the four cosmic rites, masthead text), `sim/Makefile` (`SRC` += `../ui/motif.cpp`, new test)

**Interfaces:**
- Consumes: `pal::centrepiece`, `pal::emblem`, `pal::header`.
- Produces (`namespace ui`): `void drawEmblem(Canvas&, const themepack::Emblem&, int cx, int cy, int r, uint32_t frame, int liftOverride)` (the existing `drawMuffin` algorithm: scale to diameter `2r`, mask to the circle, same lift contract); `void heptagram(Canvas&, int cx, int cy, int r, uint16_t col)` (moved from `screen_scan.cpp`, unchanged output); `void drawMotif(Canvas&, uint8_t theme, int cx, int cy, int r, uint32_t frame, int lift, uint16_t col)` (emblem blit with `lift` when `pal::emblem(theme)` is set, else a heptagram in `col`); `drawMuffin` stays (consuming `muffinEmblem()` from Task 4) and calls `drawEmblem(…, muffinEmblem(), …)`.
- `drawDialAt` branches on `pal::centrepiece(theme)`: `CP_SEAL` → `drawSeal`, `CP_EMBLEM` → `drawEmblem(pal::emblem(theme))`, else the lattice/rings/runes/heptagram path. The cosmic rites call `drawMotif`; `alertFigure` for non-seal themes calls `drawMotif` (heptagram themes unchanged). The masthead uses `pal::header(theme)`.

- [ ] **Step 1: Write failing tests:** `drawEmblem` for a 1×1, a 96×96 and a 2-colour emblem at `r=22` (the compact dial) draws only inside the circle (the `test_muffin` helpers); built-in muffin through `drawEmblem` is pixel-identical to the pre-change `drawMuffin` output (render once against a stored checksum from the current code); `drawMotif` on a heptagram theme equals `heptagram` output; on an emblem theme draws the emblem; each of the five rites, on both centrepiece kinds, draws something inside the card and nothing outside (extend the `test_nav` alert-containment loop).
- [ ] **Step 2: Run, verify fail.**
- [ ] **Step 3: Implement** by moving code, not rewriting: `drawMuffin`'s body becomes `drawEmblem` parameterised on the emblem; keep behaviour identical.
- [ ] **Step 4: Run** `make -C sim test`, `regress`, `pio run -e nm-cyd-c5 -e nm-c5-node -e nm-c5-coordinator` → all pass/build.
- [ ] **Step 5: Commit** (`feat: generalised emblem and motif drawing`).

---

### Task 6: Pack admission rules

**Files:** Modify `ui/themepack.h`/`.cpp`, `sim/test_themepack.cpp`

**Interfaces:**
- Produces: `bool folderNameOk(const char*)`; the boot-loop guard's decision as pure functions: `void formatLoading(char* out, size_t cap, const char* folder, uint32_t totalSize)` (writes `<folder>:<size>`) and `bool skipAfterCrash(const char* record, const char* folder, uint32_t totalSize)` (true only when `record` names this folder **and** this size); `enum Admit{ADMIT_OK,ADMIT_BAD_FOLDER,ADMIT_RESERVED,ADMIT_DUPLICATE,ADMIT_FULL}`; `Admit admit(const char* folder, const Pack& p, const char* const* takenNames, int nTaken)`; `void sortNames(char (*names)[32], int n)` (byte-wise ascending, so the same 8 always win); `const char* admitWhy(Admit)`.

- [ ] **Step 1: Write failing tests:** folder names `ok_1-A`, 31 chars ok, 32 chars / empty / `.` / `..` / `a/b` / `a b` / `é` rejected; names `UMBRAVOX`, `CVLT OF BAPH0MAU5`, `COSMIC MUFFIN` reserved; a second pack with an earlier pack's name rejected; the 9th admitted pack returns `ADMIT_FULL`; `sortNames` order is stable and byte-wise; `skipAfterCrash`: record `foo:1234` skips `foo` at size 1234, not at size 1235 (the pack changed), not `bar`, an empty or malformed record (`foo`, `:`, `foo:abc`, a record longer than the buffer) skips nothing, and `formatLoading` truncates safely.
- [ ] **Step 2: Run, verify fail.** **Step 3: Implement.** **Step 4: Run tests.**
- [ ] **Step 5: Commit** (`feat: pack admission and ordering rules`).

---

### Task 7: Device loader, boot-loop guard, ASPECT skipped line

**Files:** Create `device/themeload.h`, `device/themeload.cpp`; Modify `device/sdlog.h`/`.cpp`, `device/main.cpp`, `ui/model.h` (`State::packsSkipped`), `ui/screen_aspect.cpp`, `ui/screen_scan.cpp`/`screen_aspect.cpp` (name shown for packs), `sim/test_nav.cpp`

**Interfaces:**
- Consumes: `themepack::{parse,parseEmblem,admit,sortNames}`, `pal::addPack`.
- Produces: `sdlog::listDir(const char* path, void (*cb)(const char* name, bool isDir, void* ctx), void* ctx)` and `uint32_t sdlog::readFile(const char* path, uint8_t* buf, uint32_t cap)` (bytes read, 0 on failure, never more than `cap`); `themeload::scan()` (call once in `setup()` after `sdlog::begin()` and `elog::begin()`) and `uint8_t themeload::skipped()`. Skip reasons go to `elog` (`WARN theme: <folder>: <why>`). Emblem pixels for every admitted pack are held in PSRAM (≤ 8 × 9.5 KB).
- Boot-loop guard: before parsing a folder write NVS `"loading"` = `formatLoading(folder, size)`; clear after; at the start of `scan()` read the record and skip any folder for which `skipAfterCrash(record, folder, size)` is true (the record is kept until that folder's size changes) with `WARN theme: <folder>: skipped, a reset happened while loading it`.
- `State::packsSkipped` is set from `themeload::skipped()`; `drawAspect` shows `N SKIPPED - SEE DIAG LOG` (all-caps font) when non-zero; the THEME row shows a pack's name.

- [ ] **Step 1: Write failing test** (host, `test_nav`): with `packsSkipped=2` the ASPECT screen draws pixels in the new line's rect; with 0 it does not; the THEME row text for a pack id is the pack name.
- [ ] **Step 2: Run, verify fail.**
- [ ] **Step 3: Implement** the SD helpers (bounded, FAT only), `themeload`, the `main.cpp` hook, the `State` field and the ASPECT line.
- [ ] **Step 4: Run** host tests, `regress`, and `pio run` for all three envs.
- [ ] **Step 5: Commit** (`feat: load theme packs from the SD card, with a boot-loop guard`).
- [ ] **Step 6 (HUMAN-GATED, needs the SD card): hardware pass.** After Task 8 produces sample packs: copy `sim/packs/cosmic-clone/` and a deliberately bad pack (`{`) to `/themes/` on the card, flash the CYD, confirm the clone appears after the built-ins, selecting it persists across a reboot, the bad pack is skipped with a diag-log line and the ASPECT count, and removing the pack falls back to UMBRAVOX. Record the result in `docs/`. If the card is not available, say so in the final message; do not claim it.

---

### Task 8: Sim `--pack`, parity fixtures, pack checker

**Files:** Create `sim/packs/umbravox-clone/theme.json`, `sim/packs/cosmic-clone/{theme.json,emblem.bin}`, `tools/muffin_to_emblem.py`, `sim/test_pack_parity.cpp`, `sim/packload.h` (host file loading); Modify `sim/main_sim.cpp`, `sim/Makefile`

**Interfaces:**
- Produces: sim flags `--pack <folder>` (loads, registers, renders it as the selected theme) and `--check-pack <folder>` (prints `OK` or the reason; exit 0/1) for use by the designer's tests.
- Fixtures reproduce the built-ins as packs: `umbravox-clone` (heptagram, compact, header `UMBRAVOX`, ward, all sweeps but gaze, start `radar`) and `cosmic-clone` (emblem, large, header `COSMIC MVFFIN`, supernova, same sweeps) using the built-in rows' exact colours; `tools/muffin_to_emblem.py` converts `ui/muffin.h` to `emblem.bin` deterministically.

- [ ] **Step 0: Extend `sim/test_trig.cpp`** to also register both fixture packs and run its scan and alert loops over their theme ids (largest trig argument still ≤ 200 rad).
- [ ] **Step 1: Write failing test** `test_pack_parity`: render the scan screen (idle) at frames 0, 24 and 600 and an alert at ages 100/400/900 for each fixture and for the built-in theme it clones; assert the 160×120 pixel arrays are **identical**.
- [ ] **Step 2: Run, verify fail.** **Step 3: Implement** the host loader, flags and fixtures. **Step 4: Run** all tests and `regress`.
- [ ] **Step 5: Commit** (`feat: sim pack loading and built-in parity fixtures`).

---

### Task 9: WebAssembly preview build (gated on the toolchain)

**Files:** Create `tools/build-designer-wasm.sh`, `web/designer/wasm/bridge.cpp`, `web/designer/test/wasm-parity.test.mjs`

**Interfaces:**
- Produces: `uv_load_pack(json_ptr,json_len,emblem_ptr,emblem_len) -> int` (0 ok; nonzero error, message via `uv_error()`); `uv_render(screen, frame, alert_style_or_-1, out_rgba_ptr)` filling 160×120 RGBA; screens `0 scan 1 mesh 2 alert 3 menu`. Built from `ui/*.cpp` + `ui/themepack.cpp` + `ui/demo.cpp` (the same sources as the sim).

- [ ] **Step 1: Toolchain gate.** Check `which emcc`. **If absent, stop and ask the user before installing** (state the download size and that it installs into the home directory, outside the repo). If they decline, or if `ui/` cannot be compiled to WebAssembly without a separate renderer, **stop and ask** — do not substitute a second implementation of the renderer.
- [ ] **Step 2: Write the failing parity test** (`node --test`): load the wasm, render the built-in COSMIC theme and each fixture pack for scan/mesh/alert/menu at fixed frames, and compare with PNGs rendered by the sim for the same arguments (decode RGB565→RGBA identically); expect byte-equal.
- [ ] **Step 3: Run, verify fail.** **Step 4: Implement** the bridge and script (`-O2`, `EXPORTED_FUNCTIONS`, no filesystem). **Step 5: Run** the parity test → pass.
- [ ] **Step 6: Commit** (`feat: designer WebAssembly preview of the real UI code`).

---

### Task 10: Designer core modules (JS, pure)

**Files:** Create `web/designer/js/{limits.json,emblem.js,palette.js,checks.js,zip.js,pack.js}`, `web/designer/test/{emblem,palette,checks,zip,pack,limits}.test.mjs`, `package.json` (test script only)

**Interfaces:**
- Produces: `limits.json` (every number from the spec's Limits table and the 13 limitation texts, keyed); `emblem.js: imageToEmblem(rgba, w, h) -> {w,h,ncol,pal565[],px[]}` and `emblemToBin(e) -> Uint8Array` (cover-crop to square, BOX-downsample to ≤ 96, median-cut to ≤ 16 colours, RGB565); `palette.js: fromAccent(hex) -> {20 roles}`, `toRgb565(hex)`, `clamp565(hex)` (the colour the panel will show); `checks.js: runChecks(theme, emblem) -> [{level:'ok'|'warn'|'error', text}]` (all 20 colours set, text/panel and status/void contrast with ratios, emblem size/colours, header chars and length, pack size vs 64 KB, 8-pack note); `zip.js: makeZip(files) -> Uint8Array` (store-only, correct CRC32); `pack.js: buildPack(theme, emblem) -> {files}` writing `theme.json` exactly as Task 1 expects.

- [ ] **Step 1: Write failing tests:** the converter output is accepted by `umbravox --check-pack` (build a pack in a temp folder and run the sim binary), for a 1×1 image, a 500×300 photo-like gradient, and a 96×96 image; `emblemToBin` of the `ui/muffin.h` data equals `tools/muffin_to_emblem.py` output; `clamp565` matches `pal::rgb`; `fromAccent` yields 20 valid roles for black, white and a saturated colour; checks fire on an all-black theme (no crash, no divide by zero), on low `dim` contrast (warn with the ratio), a 19-char header, a lowercase header (note folded), a 97×97 emblem; the zip round-trips through `unzip -t` and `unzip -p`; `limits.json` numbers equal the `constexpr` values in `ui/themepack.h` (parsed by regex) and each number appears in `docs/theme-packs.md` (this test fails until Task 12 writes it, so Task 12 owns making it pass).
- [ ] **Step 2: Run** `node --test web/designer/test` → FAIL. **Step 3: Implement.** **Step 4: Run** → pass (except the docs assertion, which stays red until Task 12; mark it with `skip` + a TODO that Task 12 removes).
- [ ] **Step 5: Commit** (`feat: designer core modules`).

---

### Task 11: Designer pages and install

**Files:** Create `web/designer/index.html`, `web/designer/js/{app.js,install.js,preview.js}`, `web/designer/designer.css`; Modify `web/index.html` (link to the designer)

**Interfaces:**
- Consumes: Task 9's wasm API, Task 10's modules.
- Produces: the three pages from `private/theme-designer/1-start.png`, `2-design.png`, `3-install.png` (Start with the three built-ins as templates, blank, open an existing `theme.json`/folder; Design with identity, palette, centrepiece, motion, live preview tabs, live checks, Download/Install/Share-placeholder; Install with the three steps). `install.js: installToCard(dirHandle, files) -> Promise<{wrote:[],verified:boolean}>` writes `/themes/<name>/` via the File System Access API then **reads every file back and compares**; where the API is missing the page says so and offers the zip with copy-it-yourself instructions. Limitations from `limits.json` are shown inline where they apply (3,4,5,6,7,8 on Design; 1,2,10,11 on Install).

- [ ] **Step 1: Write failing tests** (`node --test` with a stub `FileSystemDirectoryHandle`): `installToCard` writes the expected paths and returns `verified:true`; a corrupted read-back returns `verified:false` and reports which file; missing API → reports unsupported; the Design page's inline limitation list renders every key it is responsible for (DOM via a minimal fake, or assert the template function output).
- [ ] **Step 2: Run, verify fail.** **Step 3: Implement** with plain ES modules, no framework; style per the mockups and `web/index.html`. **Step 4: Run** → pass.
- [ ] **Step 5 (manual): browser check.** Serve `web/` locally, open the designer in desktop Chrome, build a theme from each template, confirm the preview matches the sim PNG, download the zip, install to a folder via the picker, and confirm `umbravox --check-pack` accepts it. Record what was checked; if no desktop Chromium is available say so — do not claim it.
- [ ] **Step 6: Commit** (`feat: theme designer pages and SD install`).

---

### Task 12: Pages build, documentation, limitations reference

**Files:** Create `docs/theme-packs.md`; Modify `.github/workflows/pages.yml`, `tools/make-web-flasher.sh` (or a sibling script) to build and copy `web/designer`, `README.md`, `docs/design-notes.md`, `web/designer/test/limits.test.mjs` (remove the skip)

- [ ] **Step 1: Write `docs/theme-packs.md`:** the format, the limits table, how to make and install a pack, **all 13 limitations stated plainly** (reboot, 8 packs, emblem size/colours, uppercase header, RGB565, presets only, built-in sweeps/alerts and which two are withheld, alerts cannot be hidden, saved by name, install browser support, FAT32, skipped packs, format-not-taste). Numbers must match `limits.json`.
- [ ] **Step 2: Remove the `skip`** from the docs assertion in `limits.test.mjs`; run `node --test web/designer/test` → pass.
- [ ] **Step 3: Pages workflow:** install emscripten in CI (e.g. `mymindstorm/setup-emsdk`, pinned version), run the wasm build, `node --test`, copy `web/designer` into `web/dist/designer`. Locally validate the workflow YAML and run the build script; the CI run itself is verified after push.
- [ ] **Step 4: README:** one short section and link to `docs/theme-packs.md`; `design-notes.md`: pack format, motif, contrast rule, boot-loop guard.
- [ ] **Step 5: Run everything:** `make -C sim test`, `make -C sim regress`, `node --test web/designer/test`, `pio run` for the three envs.
- [ ] **Step 6: Commit** (`docs: theme pack reference, README, Pages build for the designer`).

---

## Notes for the executor

- Task 9 is the one real unknown (toolchain). Everything before it is independent of it; Tasks 10-11 depend on it for the preview only.
- **Ruling expected (record it):** the spec loads an emblem only for the selected pack; this plan keeps all admitted emblems (≤ 76 KB) in PSRAM at boot because PSRAM is ~7 MB free and it avoids a load callback inside the shared UI code.
- Do not add scripting hooks. The `format` field and ignoring unknown keys are the only forward-compatibility provision.
- Never claim a hardware or browser check that was not run (Task 7 step 6 and Task 11 step 5 are human/browser-gated).
