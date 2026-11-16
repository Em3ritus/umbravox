# Theme art switches Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Two optional `theme.json` keys, `heptagram` and `contacts` (`"on"`/`"off"`), that hide the drawn heptagram and the contact blips, with designer checkboxes.

**Architecture:** Parse into two new `themepack::Pack` bools (default true), expose `pal::showHeptagram(id)` / `pal::showContacts(id)` next to `pal::artOpacity`, gate the three heptagram draw sites and the two contact draw sites on them. The designer preview runs the same C++ through wasm, so it follows for free; the designer only needs model, JSON writer, checks and two checkboxes.

**Tech Stack:** C++ (`ui/`, host tests in `sim/`), ES modules + `node:test` (`web/designer/`), PlatformIO firmware.

**Spec:** `docs/superpowers/specs/2026-11-15-art-switches-design.md`

## Global Constraints

- `format` stays `1`. Keys are optional, default `"on"`. Old firmware ignores unknown keys.
- Values are strings `"on"` / `"off"`, never JSON booleans. Any other value fails the pack ("heptagram must be on or off", "contacts must be on or off"). A repeated key fails the pack ("duplicate key", already generic).
- Built-in themes and existing packs render pixel-identical: `make -C sim regress` shows no changed images.
- Passive only; contacts are still collected and logged, only the drawing is skipped.
- Tests first, show RED. Run tests as `make X && ./X`. Hardware gate `tools/bootprobe.py` after firmware change; CYD median scan draw stays about 12 ms (no more than 14). Never claim a hardware check not run.
- No push, no GitHub. Commits end with `Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>`.

## Review Focus

- `"heptagram": "off"` with centrepiece `emblem` or a radar video: emblem/video pixels identical to the default case.
- Value typos (`"false"`, `"OFF"`, `true`, `""`): pack is skipped with the reason, never half-applied.
- Key present twice: pack skipped.
- Pack with neither key: bytes drawn identical to before (defaults on).
- `contacts: off` while a contact is hot/flaring: nothing of it is drawn, but the sweep and needle still draw.
- Designer round trip: load a pack with `off`, save, load again keeps `off`; a pack with both `on` writes no extra keys (keeps old files byte-stable).

---

### Task 1: Parser and palette accessors

**Files:**
- Modify: `ui/themepack.h` (struct `Pack`, after `artScale`), `ui/themepack.cpp` (`parseObject`, key list at the `K_FORMAT...` enum ~line 203), `ui/palette.h` (~line 110), `ui/palette.cpp` (~line 252)
- Test: `sim/test_themepack.cpp`, `sim/test_palette.cpp`

**Interfaces:**
- Produces: `bool Pack::showHeptagram;` `bool Pack::showContacts;` (both default true); `bool pal::showHeptagram(uint8_t id);` `bool pal::showContacts(uint8_t id);` (true for built-ins and for any id without a pack slot).

- [ ] **Step 1: Failing tests in `test_themepack.cpp`** using the existing `Base` builder (`extra` field carries extra keys): omitted keys give `showHeptagram==true && showContacts==true`; `extra=",\"heptagram\":\"off\""` gives false/true; `"contacts":"off"` gives true/false; both off; `"on"` explicit stays true; each of `"false"`, `"OFF"`, `""`, a bare `true` fails with the reasons "heptagram must be on or off" / "contacts must be on or off"; a duplicated key fails with "duplicate key".
- [ ] **Step 2: Failing test in `test_palette.cpp`**: after `addPack` of a pack with both off, `pal::showHeptagram(id)==false`, `pal::showContacts(id)==false`; for `TH_UMBRAVOX` and `TH_COSMICMUFFIN` both true.
- [ ] **Step 3: Run RED:** `make -C sim test_themepack test_palette && ./sim/test_themepack` (use the Makefile's actual target names; confirm they fail to compile or assert, not pass on a stale binary).
- [ ] **Step 4: Implement.** Add `K_HEPTAGRAM, K_CONTACTS` to the key enum and `keys[]` (before `K_N`; update the array lengths; `char k[16]` already fits). Init `out.showHeptagram = out.showContacts = true` beside `out.artOpacity=100`. In the switch, parse a string with the existing `str(p,tmp,31,&tl)` pattern like `K_BADGE`. Accessors follow `artOpacity`: `slotOf(id)` then `PACKS[s].showX`, else true.
- [ ] **Step 5: GREEN** for both tests, then the whole `make -C sim test` (ASan fuzz in `test_themepack` must stay green).
- [ ] **Step 6: Commit** `feat(themepack): heptagram and contacts on/off keys`.

### Task 2: Gate the draw sites

**Files:**
- Modify: `ui/screen_scan.cpp` (CYD heptagram branch ~829-860; contacts loop ~864), `ui/scan_render.h` (wide heptagram `else if(!clean)` ~1137-1146; blips loop ~1184; `drawArtwork` ~749), `sim/Makefile`
- Create: `sim/test_art_switches.cpp`

**Interfaces:**
- Consumes: `pal::showHeptagram(uint8_t)`, `pal::showContacts(uint8_t)` from Task 1.

- [ ] **Step 1: Failing test `sim/test_art_switches.cpp`.** Load three packs through `addPack` (same helper style as `test_palette.cpp`): default, `heptagram off`, `contacts off`, with identical colours and a fixed `nContacts`/`nBlips` scene. Render the CYD scan (`screen_scan` via the sim canvas) and the wide scan (`scan_render.h`, W>=400, with a `RadarVideo` filled with a known flat colour). Assert: (a) default render equals a render with no switch keys; (b) `heptagram off` differs from default only inside the dial and the video-clip pixels are unchanged (pixel at the dial centre equals the video colour on the wide render, where the default render shows the heptagram line colour on a known heptagram vertex); (c) `contacts off` removes the blip pixels at a known contact position and leaves sweep/needle pixels equal; (d) emblem theme with `heptagram off` renders identical to emblem default. Add the file to `sim/Makefile` `test` target like its siblings.
- [ ] **Step 2: RED** `make -C sim test_art_switches && ./sim/test_art_switches` fails on (b) and (c).
- [ ] **Step 3: Implement.** CYD: wrap the whole heptagram `else` branch (lattice, heptagram, range rings, crosshair, circles, runes) in `pal::showHeptagram(s.set.theme)`; keep `drawEmblem`/seal branches as is. Contacts loop: `if (pal::showContacts(s.set.theme))`. Wide: add `&& pal::showHeptagram(sc.theme)` to the `else if(!clean)` heptagram branch; guard the `sc.blips` loop with `pal::showContacts(sc.theme)`. `drawArtwork`: skip the `heptaInto` fallback when `!pal::showHeptagram(theme)`.
- [ ] **Step 4: GREEN**, then `grep -nE "heptagram\(|heptaInto|nContacts|nBlips" ui device` and gate any other draw site this finds (portrait scan, alert takeover are out of scope per spec; list what was left alone in the commit message).
- [ ] **Step 5: `make -C sim test && make -C sim regress`**: regress must report no changed images. Also `make -C sim bench` and compare to the baseline in `private/`.
- [ ] **Step 6: Commit** `feat(scan): honour the heptagram and contacts switches`.

### Task 3: Designer

**Files:**
- Modify: `web/designer/js/model.js` (`parseTheme` after centrepiece ~line 144, `blankTheme`), `web/designer/js/pack.js` (`themeJson`), `web/designer/js/checks.js`, `web/designer/js/app.js` (`syncForm` ~205, handlers ~255), `web/designer/index.html` (Centrepiece card ~line 75-90), `web/designer/js/limits.json` if enum lists live there
- Test: `web/designer/test/model.test.mjs`, `pack.test.mjs`, `checks.test.mjs`

**Interfaces:**
- Produces: theme fields `heptagram` and `contacts`, each `'on' | 'off'`, absent means `'on'`. `themeJson` writes a key only when it is `'off'`, placed right after `centrepiece`, in the order `heptagram`, `contacts`.

- [ ] **Step 1: Failing tests.** `model.test.mjs`: parsing a theme.json with `"heptagram":"off"` yields `theme.heptagram==='off'`; a bad value yields a warning and `'on'`; missing keys yield `'on'`. `pack.test.mjs`: `themeJson` of an all-on theme has the same key list as the existing test (no new keys); with both off the keys are `[... 'centrepiece','heptagram','contacts','sweeps' ...]`; run the pack through `checkPack` (sim oracle, `_util.mjs`) and assert it is accepted; a value `'maybe'` is rejected by `checks.js` with `Heptagram must be on or off.`.
- [ ] **Step 2: RED:** `npm test` shows the three new tests failing.
- [ ] **Step 3: Implement** the model/pack/checks changes as above. In `index.html` add two `<label><input type="checkbox" id="f-heptagram"> Show heptagram</label>` and `f-contacts` "Show contact dots" inside `#panel-centre`, with a one-line hint "Hide them when your own image or video should show clean. Older firmware ignores these." In `app.js` `syncForm` set `checked = t.heptagram !== 'off'`; change handlers set `state.theme.heptagram = checked ? 'on' : 'off'` then `changed()`.
- [ ] **Step 4: GREEN** `npm test`. Then run the designer locally (`tools/build-designer-wasm.sh && tools/assemble-designer.sh web/dist`, `python3 -m http.server -d web/dist`), upload a video with the heptagram box off and look at the preview; save an image to `private/renders/` for the owner (they cannot see the terminal).
- [ ] **Step 5: Commit** `feat(designer): heptagram and contact-dot switches`.

### Task 4: Docs and hardware gate

**Files:**
- Modify: `docs/theme-packs.md` (key table after `centrepiece` ~line 74, and the designer paragraph ~197-200), `CHANGELOG`/README "Recent updates" if the repo keeps one

- [ ] **Step 1:** Add two table rows (`heptagram`, `contacts`: optional, `on`/`off`, default `on`, what each hides, "older firmware ignores them").
- [ ] **Step 2:** Build all four envs: `~/.platformio/penv/bin/pio run -e nm-cyd-c5 -e nm-c5-node -e nm-c5-coordinator -e ws-c5-35-engine`; all succeed.
- [ ] **Step 3:** Ask the owner before flashing (the Waveshare uses `esptool --no-stub`, see `docs/lessons.md` #15). After flashing the exact build, run `tools/bootprobe.py` and report heap and frame timing; do not claim hardware results otherwise.
- [ ] **Step 4:** `git grep -nE "/home/[a-z]+"` is empty; `git log --format=%B | grep -c Opus` is 0.
- [ ] **Step 5: Commit** `docs(theme-packs): heptagram and contacts keys`.
