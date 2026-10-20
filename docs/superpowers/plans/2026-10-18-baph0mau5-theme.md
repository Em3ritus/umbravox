# CVLT OF BAPH0MAU5 Theme Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Ship a second UI theme built around a Baphomet sigil, with the theme and the dial's search animation both selectable on the device, and UMBRAVOX rendering exactly as it does today.

**Architecture:** Four independent seams. The palette and the dial geometry stop being `constexpr` and become globals written by `pal::setTheme(id)` from a table with one row per theme — this is why 239 `pal::` call sites across nine files are never edited. The dial's centrepiece and its search animation are branches inside `drawDial`. A new ASPECT screen, reached as the eighth GRIMOIRE tile, previews and sets both.

**Tech Stack:** C++17, no libraries. `g++` and `make` for the host simulator and tests; PlatformIO / Arduino-ESP32 for the device. Python 3 with Pillow, NumPy and SciPy for the one-off asset generator. Framebuffer is 160×120 RGB565, blitted 1:2 to a 320×240 ST7789.

**Spec:** `docs/superpowers/specs/2026-10-18-baph0mau5-theme-design.md`

## Global Constraints

- **Work happens on the `baph0mau5-theme` branch.** The repository was initialised at the start of execution; commit `866d4df` is UMBRAVOX exactly as it stood before any of this work. Commit at the end of every task. Never merge to `master` — that is the human's call.
- **UMBRAVOX must render byte-for-byte as it does today.** The contract is `private/baseline/MANIFEST.md5`, nine screens at `--frame 0 --scale 4`. `scan.png` is `2886dd6c61e38ada6f4f3c00afd35fcb`.
- **Never edit a `pal::` call site.** If a change seems to require editing one of the 239 references in the nine screen files, the seam is wrong — fix the seam.
- Build flags stay `-std=c++17 -O2 -Wall -Wextra`. New code compiles with no new warnings.
- The framebuffer is 160 wide, 120 high. The body runs y=13..102; `HDR_H` is 12 and `FOOT_Y` is 103.
- Host tests use the existing idiom: a file-scope `static int fails`, a `check(const char* what, bool ok)` helper, `printf`, and `return fails ? 1 : 0`. No test framework.
- The asset generator writes `ui/sigil.h`. **That header is never hand-edited.** Regenerate it.
- Tile labels must fit `GRIM_W` = 50px at 6px per character, so a label is at most 8 characters.

## Review Focus

Five failure modes the spec implies that no task's happy path exercises. Each has its test assigned to the task that owns the code.

1. **A settings field added later but missed in the equality comparator** silently stops the debounce timer from ever firing, so the setting never persists. `prefs.cpp` already carries a comment about a bug of this exact shape. → Task 3, a test that mutates *every* field of `Settings` in a loop and asserts inequality is detected for each.
2. **A stored sweep that is not available on the stored theme** — gaze saved on BAPH0MAU5, then the theme is changed to UMBRAVOX — must fall back to radar rather than draw a gaze with no eyes. → Task 3.
3. **An out-of-range theme or sweep id out of corrupt NVS** must not index past the end of the theme table. → Task 3.
4. **Changing the theme leaves stale pixels**, because the dirty-rect machinery assumes the palette and geometry are constant between frames. → Task 9, which forces a full-frame blit on change.
5. **Tapping where the spectrum used to be** must not open MESH on a theme that has no spectrum strip. → Task 4.

---

## File Structure

**Created:**
- `ui/palette.cpp` — the theme table and `setTheme`. The only file that knows a theme exists.
- `ui/sigil.h` — generated 4bpp seal bitmaps and feature offsets.
- `ui/seal.h`, `ui/seal.cpp` — the BAPH0MAU5 centrepiece: bitmap blit, witchfire tints, procedural figure, alchemical marks.
- `ui/sweep.h`, `ui/sweep.cpp` — the five search animations and the per-mode contact test.
- `ui/screen_aspect.cpp` — the ASPECT screen.
- `tools/gen_sigil.py` — the asset generator.
- `sim/test_settings.cpp` — host tests for clamping, equality and the sweep mask.

**Modified:**
- `ui/palette.h` — `constexpr` values become `extern`; theme ids, `setTheme`, `themeName`, `sweepMask`.
- `ui/layout.h` — dial and spectrum constants move out to the theme table.
- `ui/model.h` — `Settings` gains `theme` and `sweep`; the `Sweep` enum.
- `ui/nav.h`, `ui/nav.cpp` — `settingsEqual`, extended `clampSettings`, ASPECT routing and controls.
- `ui/screens.h` — `drawAspect` declaration; `drawDial` becomes externally callable and parameterised.
- `ui/screen_scan.cpp` — `drawDial` takes centre and radii; centrepiece branch; spectrum skipped when absent.
- `ui/screen_grimoire.cpp` — an eighth tile.
- `ui/demo.h`, `ui/demo.cpp` — demo state carries a theme and sweep.
- `sim/main_sim.cpp` — `--theme` and `--sweep` flags; `aspect` screen.
- `sim/Makefile` — `test` and `regress` targets; `palette.cpp`, `seal.cpp`, `sweep.cpp`, `screen_aspect.cpp` added to `SRC`.
- `device/prefs.cpp` — persist the two new fields; `VER` 3 → 4; delegate equality to `settingsEqual`.

---

### Task 1: Build plumbing — test and regression targets

Nothing else in this plan can be verified without this. The four existing host tests have never been wired into the build: they each carry their own `main()` and have never been compiled by anything.

**Files:**
- Modify: `sim/Makefile`

**Interfaces:**
- Consumes: nothing.
- Produces: `make -C sim test` (builds and runs every host test, non-zero on failure) and `make -C sim regress` (renders nine screens and checks them against `private/baseline/MANIFEST.md5`).

- [ ] **Step 1: Add the test and regress targets**

In `sim/Makefile`, replace the line `.PHONY: run sheet clean` with the block below, and add it above that line:

```make
# The host tests each carry their own main(), so each links as its own
# binary. They are listed explicitly rather than globbed: a test that stops
# being built is a test that stops failing, and nobody notices.
UI_CORE = ../ui/canvas.cpp ../ui/font5x7.cpp ../ui/runes.cpp ../ui/palette.cpp

test_chanplan: test_chanplan.cpp ../device/chanplan.h
	$(CXX) $(CXXFLAGS) -o $@ test_chanplan.cpp -lm

test_gnss: test_gnss.cpp ../ui/gnss.cpp
	$(CXX) $(CXXFLAGS) -o $@ test_gnss.cpp ../ui/gnss.cpp -lm

test_signatures: test_signatures.cpp ../ui/signatures.cpp
	$(CXX) $(CXXFLAGS) -o $@ test_signatures.cpp ../ui/signatures.cpp -lm

test_wifi_auth: test_wifi_auth.cpp ../ui/wifi_auth.cpp
	$(CXX) $(CXXFLAGS) -o $@ test_wifi_auth.cpp ../ui/wifi_auth.cpp -lm

test_settings: test_settings.cpp ../ui/nav.cpp ../ui/screen_grimoire.cpp $(UI_CORE)
	$(CXX) $(CXXFLAGS) -o $@ test_settings.cpp ../ui/nav.cpp \
	    ../ui/screen_grimoire.cpp $(UI_CORE) -lm

TESTS = test_chanplan test_gnss test_signatures test_wifi_auth

test: $(TESTS)
	@rc=0; for t in $(TESTS); do \
	   echo "== $$t"; ./$$t || rc=1; done; \
	 exit $$rc

# UMBRAVOX must render exactly as it did before this work started. With no
# version control there is nothing to diff against after the fact, so the
# baseline was captured up front and this target is the contract.
SCREENS = scan codex detail domain mesh omens echoes grimoire attune
REGRESS_DIR = /tmp/aether-regress

regress: $(BIN)
	@mkdir -p $(REGRESS_DIR)
	@for s in $(SCREENS); do \
	   ./$(BIN) $(REGRESS_DIR)/$$s.png --screen $$s --frame 0 --scale 4 >/dev/null; \
	 done
	@cd $(REGRESS_DIR) && md5sum -c --quiet \
	   $(CURDIR)/../private/baseline/MANIFEST.md5 \
	 && echo "regress: UMBRAVOX unchanged"

.PHONY: run sheet clean test regress
```

Note `test_settings` has a rule but is deliberately **not** in `TESTS` yet — its source does not exist until Task 3, and a `test` target that cannot build is a target nobody runs.

- [ ] **Step 2: Verify both targets work against the unchanged tree**

Run: `cd sim && make clean && make -s && make -s regress`
Expected: `regress: UMBRAVOX unchanged`

Run: `make -s test`
Expected: four `== test_*` blocks, each ending `all passed`, exit 0.

If `regress` fails here, before any source has been touched, the baseline is wrong — stop and re-capture it. Do not proceed.

- [ ] **Step 5: Commit**

```bash
git add -A
git commit -m "build: wire the host tests into make, add a regress target"
```

---

### Task 2: The palette becomes runtime

**Files:**
- Modify: `ui/palette.h`
- Create: `ui/palette.cpp`
- Modify: `sim/Makefile:7` (add `../ui/palette.cpp` to `SRC`)
- Modify: `sim/main_sim.cpp`

**Interfaces:**
- Consumes: Task 1's `regress` target.
- Produces: `pal::setTheme(uint8_t)`, `pal::themeName(uint8_t)`, `enum pal::ThemeId { TH_UMBRAVOX = 0, TH_BAPH0MAU5, TH_COUNT }`, and the palette globals `pal::VOID … pal::RED` plus `pal::SWEEP`, `pal::SWEEP_HI`, `pal::LIVE`, `pal::LIVE_HI`, `pal::RAMP[4]`.

- [ ] **Step 1: Rewrite the palette header**

Replace the body of `ui/palette.h` below the `rgb()` function. Keep `rgb()` `constexpr` — it builds the table.

```cpp
// --- structure -----------------------------------------------------------
extern uint16_t VOID;       // page
extern uint16_t PANEL;      // card fill
extern uint16_t GRID;       // lattice inside the circle
extern uint16_t EDGE;       // card border, ring work
extern uint16_t EDGE_LIT;   // border, emphasised

// --- type ----------------------------------------------------------------
extern uint16_t DIM;        // labels
extern uint16_t TEXT;       // body
extern uint16_t BRIGHT;     // emphasis

// --- chrome: ours ---------------------------------------------------------
extern uint16_t WITCH;      // live marks
extern uint16_t WITCH_HI;   // the hot edge
extern uint16_t BONE;       // runes, inscription, ceremony

// --- the search -----------------------------------------------------------
extern uint16_t SWEEP;      // the beam body
extern uint16_t SWEEP_HI;   // its leading edge

// --- the centrepiece, when a theme has a bitmap one ------------------------
extern uint16_t LIVE;       // parts of the seal that are alight
extern uint16_t LIVE_HI;
extern uint16_t RAMP[4];    // the bitmap's four anchor levels, dark to light

// --- data: theirs ---------------------------------------------------------
// These names are historical and mean the BAND, not the hue: MAGENTA is
// 2.4 GHz, ICE is 5 GHz, GREEN is BLE. BAPH0MAU5 paints them green, white
// and violet. Renaming them would mean editing all nine screen files, which
// is the exact cost this seam exists to avoid.
extern uint16_t MAGENTA;    // 2.4 GHz
extern uint16_t ICE;        // 5 GHz
extern uint16_t GREEN;      // BLE, and "good"

// --- status: never used for data -----------------------------------------
extern uint16_t AMBER;
extern uint16_t RED;

enum ThemeId : uint8_t { TH_UMBRAVOX = 0, TH_BAPH0MAU5, TH_COUNT };

// Point the palette at a theme. An out-of-range id falls back to UMBRAVOX
// rather than reading off the end of the table -- this is reached from NVS,
// which can hold anything.
void setTheme(uint8_t id);
const char* themeName(uint8_t id);

// Which sweeps this theme offers, as a bitmask over the Sweep enum. The gaze
// needs eyes, and UMBRAVOX has a heptagram rather than a face.
uint8_t sweepMask(uint8_t id);
```

Update the file's top comment to explain that these are variables so the theme can change at runtime, and that every call site keeps the name it always used.

- [ ] **Step 2: Write the theme table**

Create `ui/palette.cpp`:

```cpp
#include "palette.h"

namespace pal {

uint16_t VOID, PANEL, GRID, EDGE, EDGE_LIT;
uint16_t DIM, TEXT, BRIGHT;
uint16_t WITCH, WITCH_HI, BONE;
uint16_t SWEEP, SWEEP_HI;
uint16_t LIVE, LIVE_HI;
uint16_t RAMP[4];
uint16_t MAGENTA, ICE, GREEN;
uint16_t AMBER, RED;

namespace {

// One row per theme. A table rather than a switch: adding a theme is adding
// a row, and a row cannot silently omit a colour the way a hand-written
// block of assignments can.
struct Row {
    const char* name;
    uint8_t  sweeps;                 // bitmask over Sweep
    uint16_t void_, panel, grid, edge, edgeLit;
    uint16_t dim, text, bright;
    uint16_t witch, witchHi, bone;
    uint16_t sweep, sweepHi;
    uint16_t live, liveHi;
    uint16_t ramp[4];
    uint16_t magenta, ice, green;
    uint16_t amber, red;
};

constexpr Row THEMES[TH_COUNT] = {
    {   "UMBRAVOX",
        0b11101,                     // every sweep but the gaze
        rgb(0x07,0x04,0x0E), rgb(0x13,0x0B,0x22), rgb(0x18,0x0F,0x2C),
        rgb(0x2E,0x1F,0x52), rgb(0x56,0x36,0x97),
        rgb(0x6B,0x4F,0x9E), rgb(0xC2,0xAB,0xE4), rgb(0xF2,0xE8,0xFF),
        rgb(0x9D,0x7B,0xFF), rgb(0xD6,0xC2,0xFF), rgb(0xC9,0xB8,0x97),
        rgb(0x9D,0x7B,0xFF), rgb(0xD6,0xC2,0xFF),
        rgb(0xD6,0xC2,0xFF), rgb(0xF2,0xE8,0xFF),
        // No bitmap centrepiece here, but a working grey ramp rather than
        // four copies of VOID: a placeholder that renders nothing is the
        // kind that survives to a release.
        { rgb(0x07,0x04,0x0E), rgb(0x2E,0x1F,0x52),
          rgb(0x6B,0x4F,0x9E), rgb(0xF2,0xE8,0xFF) },
        rgb(0xD0,0x2B,0xB4), rgb(0x6F,0xD3,0xF5), rgb(0x4A,0xDE,0x80),
        rgb(0xF5,0x9E,0x0B), rgb(0xEF,0x44,0x44),
    },
    {   "BAPH0MAU5",
        0b11111,                     // all five
        rgb(0x06,0x03,0x0C), rgb(0x13,0x09,0x21), rgb(0x1D,0x11,0x33),
        rgb(0x3C,0x20,0x62), rgb(0x74,0x3C,0xB4),
        rgb(0x7E,0x56,0xBC), rgb(0xCA,0xB0,0xEA), rgb(0xFF,0xFA,0xFF),
        rgb(0x9B,0x62,0xF0), rgb(0xD6,0xBC,0xFF), rgb(0xE4,0xDA,0xF6),
        rgb(0x3B,0xFF,0x6E), rgb(0xC4,0xFF,0xD4),
        rgb(0x24,0xC4,0x50), rgb(0x3B,0xFF,0x6E),
        { rgb(0x06,0x03,0x0C), rgb(0x3E,0x30,0x55),
          rgb(0xB6,0xAE,0xCC), rgb(0xFF,0xFC,0xFF) },
        rgb(0x3B,0xFF,0x6E), rgb(0xF2,0xEE,0xFF), rgb(0x9B,0x62,0xF0),
        rgb(0xF5,0x9E,0x0B), rgb(0xD4,0x24,0x20),
    },
};

inline const Row& row(uint8_t id) {
    return THEMES[id < TH_COUNT ? id : (uint8_t)TH_UMBRAVOX];
}

} // namespace

void setTheme(uint8_t id) {
    const Row& t = row(id);
    VOID = t.void_;  PANEL = t.panel;  GRID = t.grid;
    EDGE = t.edge;   EDGE_LIT = t.edgeLit;
    DIM = t.dim;     TEXT = t.text;    BRIGHT = t.bright;
    WITCH = t.witch; WITCH_HI = t.witchHi; BONE = t.bone;
    SWEEP = t.sweep; SWEEP_HI = t.sweepHi;
    LIVE = t.live;   LIVE_HI = t.liveHi;
    for (int i = 0; i < 4; i++) RAMP[i] = t.ramp[i];
    MAGENTA = t.magenta; ICE = t.ice; GREEN = t.green;
    AMBER = t.amber; RED = t.red;
}

const char* themeName(uint8_t id) { return row(id).name; }
uint8_t     sweepMask(uint8_t id) { return row(id).sweeps; }

} // namespace pal
```

- [ ] **Step 2b: Add palette.cpp to the build**

In `sim/Makefile`, line 7, change `SRC = main_sim.cpp ` to `SRC = main_sim.cpp ../ui/palette.cpp `.

- [ ] **Step 3: Call setTheme before anything draws**

In `sim/main_sim.cpp`, add a `--theme` flag and call `setTheme` before the render loop.

After the line `int frame = 0, scale = 4, sheet = 0;` add:

```cpp
    uint8_t themeId = pal::TH_UMBRAVOX;
```

In the argument loop, after the `--screen` branch, add:

```cpp
        else if (!strcmp(argv[i], "--theme") && i + 1 < argc) themeId = (uint8_t)atoi(argv[++i]);
```

Immediately before `static ui::Canvas c;` add:

```cpp
    // Nothing may draw before the palette points somewhere.
    pal::setTheme(themeId);
```

- [ ] **Step 4: Verify UMBRAVOX is byte-identical**

Run: `cd sim && make clean && make -s && make -s regress`
Expected: `regress: UMBRAVOX unchanged`

This is the whole point of the seam. If any screen differs, a colour in the UMBRAVOX row does not match the original `palette.h`. Diff the row against `private/baseline/ui-pristine/palette.h`.

- [ ] **Step 5: Verify the switch actually does something**

Run: `./umbravox /tmp/t1.png --theme 1 --frame 0 --scale 4 && ./umbravox /tmp/t0.png --theme 0 --frame 0 --scale 4 && cmp /tmp/t0.png /tmp/t1.png`
Expected: `cmp` reports the files differ. If they are identical, `setTheme` is not being called or the table rows are the same.

- [ ] **Step 6: Verify no new warnings**

Run: `cd sim && make clean && make 2>&1 | grep -i warn`
Expected: no output.

- [ ] **Step 7: Commit**

```bash
git add -A
git commit -m "refactor: make the palette runtime-switchable per theme"
```

---

### Task 3: Settings fields, equality and clamping

The equality comparator moves out of `device/prefs.cpp` and into `ui/`. `prefs.cpp` includes `<Arduino.h>` and `<Preferences.h>`, so nothing in it can be host-tested — and the comparator is the single thing in this feature most likely to fail silently.

**Files:**
- Modify: `ui/model.h`
- Modify: `ui/nav.h`, `ui/nav.cpp`
- Create: `sim/test_settings.cpp`
- Modify: `sim/Makefile` (add `test_settings` to `TESTS`)

**Interfaces:**
- Consumes: `pal::sweepMask(uint8_t)`, `pal::TH_COUNT` from Task 2.
- Produces: `ui::Settings::theme`, `ui::Settings::sweep`, `enum ui::Sweep { SW_RADAR = 0, SW_GAZE, SW_PULSE, SW_STAR, SW_CANDLE, SW_COUNT }`, `bool ui::settingsEqual(const Settings&, const Settings&)`, and an extended `ui::clampSettings(Settings&)`.

- [ ] **Step 1: Write the failing tests**

Create `sim/test_settings.cpp`:

```cpp
// Host tests for the settings a theme switch introduces.
//
// The comparator is the case that matters. It lives here rather than in
// device/prefs.cpp because prefs.cpp pulls in Arduino.h and cannot be built
// on a host -- and a comparator that silently forgets a field stops the save
// timer from ever firing, which is a bug this project has already had once.
#include "../ui/nav.h"
#include "../ui/palette.h"
#include <stdio.h>
#include <string.h>

static int fails = 0;

static void check(const char* what, bool ok) {
    printf(ok ? "  ok   %s\n" : "  FAIL %s\n", what);
    if (!ok) fails++;
}

int main() {
    using namespace ui;

    printf("equality covers every field:\n");
    {
        // Walk the struct a byte at a time. A field added later but left out
        // of settingsEqual() is caught here without anyone remembering to
        // add a case, which is the only way this stays true.
        Settings base;
        clampSettings(base);
        int missed = 0;
        for (size_t off = 0; off < sizeof(Settings); off++) {
            Settings a = base, b = base;
            reinterpret_cast<unsigned char*>(&b)[off] ^= 0xFF;
            clampSettings(b);
            if (settingsEqual(a, b)) continue;      // difference seen: good
            // The clamp may have pulled the mutated byte straight back to
            // the original value, in which case there is nothing to detect.
            if (memcmp(&a, &b, sizeof(Settings)) == 0) continue;
            missed++;
        }
        check("no field differs without settingsEqual noticing", missed == 0);
    }

    printf("equality is reflexive and specific:\n");
    {
        Settings a, b;
        check("identical settings compare equal", settingsEqual(a, b));
        b.theme = pal::TH_BAPH0MAU5;
        check("a theme change is detected", !settingsEqual(a, b));
        b = a; b.sweep = SW_STAR;
        check("a sweep change is detected", !settingsEqual(a, b));
        b = a; b.bright = (uint8_t)(a.bright + 1);
        check("an existing field is still detected", !settingsEqual(a, b));
    }

    printf("clamping survives a corrupt store:\n");
    {
        Settings s;
        s.theme = 200;
        clampSettings(s);
        check("out-of-range theme falls back", s.theme < pal::TH_COUNT);

        s = Settings();
        s.sweep = 200;
        clampSettings(s);
        check("out-of-range sweep falls back", s.sweep < SW_COUNT);
    }

    printf("a sweep must be available on its theme:\n");
    {
        Settings s;
        s.theme = pal::TH_BAPH0MAU5;
        s.sweep = SW_GAZE;
        clampSettings(s);
        check("gaze survives on baph0mau5", s.sweep == SW_GAZE);

        s.theme = pal::TH_UMBRAVOX;        // the gaze has no eyes here
        clampSettings(s);
        check("gaze falls back to radar on umbravox", s.sweep == SW_RADAR);

        s.sweep = SW_STAR;
        clampSettings(s);
        check("star is kept on umbravox", s.sweep == SW_STAR);
    }

    printf("the mask matches the enum:\n");
    {
        check("umbravox offers radar",  pal::sweepMask(pal::TH_UMBRAVOX) & (1 << SW_RADAR));
        check("umbravox refuses gaze", !(pal::sweepMask(pal::TH_UMBRAVOX) & (1 << SW_GAZE)));
        check("baph0mau5 offers gaze",    pal::sweepMask(pal::TH_BAPH0MAU5) & (1 << SW_GAZE));
        for (int s = 0; s < SW_COUNT; s++)
            check("baph0mau5 offers every sweep",
                  pal::sweepMask(pal::TH_BAPH0MAU5) & (1 << s));
    }

    printf(fails ? "\n%d FAILED\n" : "\nall passed\n", fails);
    return fails ? 1 : 0;
}
```

- [ ] **Step 2: Add the test to the suite and run it to verify it fails**

In `sim/Makefile`, change `TESTS = test_chanplan test_gnss test_signatures test_wifi_auth` to append ` test_settings`.

Run: `cd sim && make test_settings`
Expected: FAIL to compile — `'theme' is not a member of 'ui::Settings'`, `'settingsEqual' was not declared`.

- [ ] **Step 3: Add the fields and the enum**

In `ui/model.h`, inside `namespace ui`, above `struct Settings`:

```cpp
// How the dial searches the field. A sweep finds by bearing, a pulse by
// distance, and a candle not at all -- these are different instruments, not
// different decorations, so the id travels with the setting.
enum Sweep : uint8_t { SW_RADAR = 0, SW_GAZE, SW_PULSE, SW_STAR, SW_CANDLE, SW_COUNT };
```

Then add two fields to `struct Settings`, after `uint8_t idleIdx = 2;`:

```cpp
    // Appearance. Not a capture trade-off like the rest of this struct, but
    // it is stored the same way and clamped by the same function.
    uint8_t  theme   = 0;     // pal::ThemeId
    uint8_t  sweep   = 0;     // Sweep
```

- [ ] **Step 4: Declare the comparator**

In `ui/nav.h`, below the `clampSettings` declaration:

```cpp
// True when two Settings hold the same values.
//
// This lives beside the struct rather than in device/prefs.cpp, where the
// comparison used to be: prefs.cpp cannot be built on a host, and a
// comparator that forgets a field stops the save timer from ever firing --
// which looks exactly like "the setting does not persist" and nothing else.
bool settingsEqual(const Settings& a, const Settings& b);
```

- [ ] **Step 5: Implement the comparator and extend the clamp**

In `ui/nav.cpp`, add inside `namespace ui` beside `clampSettings`:

```cpp
bool settingsEqual(const Settings& a, const Settings& b) {
    return a.alertMin == b.alertMin && a.bright  == b.bright  &&
           a.dwellMs  == b.dwellMs  && a.bleDuty == b.bleDuty &&
           a.bands    == b.bands    && a.idleIdx == b.idleIdx &&
           a.theme    == b.theme    && a.sweep   == b.sweep;
}
```

Add `#include "palette.h"` to the top of `ui/nav.cpp`, and extend `clampSettings` with:

```cpp
    if (s.theme >= pal::TH_COUNT) s.theme = pal::TH_UMBRAVOX;
    if (s.sweep >= SW_COUNT)      s.sweep = SW_RADAR;
    // A sweep the active theme does not offer is not a preference to be
    // preserved; it is a sweep that would draw nothing meaningful.
    if (!(pal::sweepMask(s.theme) & (1 << s.sweep))) s.sweep = SW_RADAR;
```

- [ ] **Step 6: Run the tests to verify they pass**

Run: `cd sim && make -s test`
Expected: five `== test_*` blocks, each ending `all passed`, exit 0.

- [ ] **Step 7: Verify UMBRAVOX is still byte-identical**

Run: `cd sim && make -s && make -s regress`
Expected: `regress: UMBRAVOX unchanged`

- [ ] **Step 8: Commit**

```bash
git add -A
git commit -m "feat: add theme and sweep settings with a host-tested comparator"
```

---

### Task 4: Dial geometry joins the theme table

**Files:**
- Modify: `ui/layout.h`, `ui/palette.h`, `ui/palette.cpp`
- Modify: `ui/screen_scan.cpp`, `ui/nav.cpp`

**Interfaces:**
- Consumes: `pal::setTheme` from Task 2.
- Produces: runtime `ui::DIAL_Y`, `DIAL_H`, `DIAL_CX`, `DIAL_CY`, `R_FIELD`, `R_RUNES`, `R_BOUND`, `R_STAR`, `SPEC_Y`, `SPEC_H`, all written by `pal::setTheme`. `SPEC_H == 0` means the theme has no spectrum strip.

- [ ] **Step 1: Move the constants out of layout.h**

In `ui/layout.h`, delete these lines:

```cpp
constexpr int DIAL_Y    = 13, DIAL_H = 70;
constexpr int SPEC_Y    = 85, SPEC_H = 16;
constexpr int DIAL_CX   = COL_R_X + COL_R_W / 2;   // 109
constexpr int DIAL_CY   = 48;
constexpr int R_FIELD   = 22;
constexpr int R_RUNES   = 26;
constexpr int R_BOUND   = 31;
constexpr int R_STAR    = 20;
```

and replace them with:

```cpp
// The dial's geometry is part of the theme, not of the layout: the two
// themes want different sizes, and the alternative is a dial that fits one
// of them badly. These are written by pal::setTheme().
//
// They stay in ui:: under the names they always had, so screen_scan.cpp and
// nav.cpp read them unchanged -- which is the point of this file. The code
// that draws the dial and the code that decides whether a finger landed on
// it must agree, and they still cannot disagree.
//
// SPEC_H == 0 means the theme has no spectrum strip.
extern int DIAL_Y, DIAL_H;
extern int DIAL_CX, DIAL_CY;
extern int R_FIELD, R_RUNES, R_BOUND, R_STAR;
extern int SPEC_Y, SPEC_H;
```

`COL_R_X`, `COL_R_W`, `COL_L_X`, `COL_L_W`, `TILE_H`, `TILE_Y`, `BODY_Y`, `BODY_H`, `ROWS` and everything else in the file stay `constexpr`.

- [ ] **Step 2: Add the geometry to the theme table**

In `ui/palette.cpp`, add `#include "layout.h"` at the top. Add these fields to `struct Row`, after `uint8_t sweeps;`:

```cpp
    int16_t dialY, dialH, dialCx, dialCy;
    int16_t rField, rRunes, rBound, rStar;
    int16_t specY, specH;
```

Add the values as the second line of each row, immediately after the mask.

UMBRAVOX — exactly today's numbers:

```cpp
        13, 70, 109, 48,   22, 26, 31, 20,   85, 16,
```

BAPH0MAU5 — the dial takes the body's full height and there is no spectrum:

```cpp
        13, 90, 109, 57,   33, 37, 42, 31,    0,  0,
```

Define the globals in `ui/palette.cpp` inside `namespace ui`, above `namespace pal`:

```cpp
namespace ui {
int DIAL_Y, DIAL_H, DIAL_CX, DIAL_CY;
int R_FIELD, R_RUNES, R_BOUND, R_STAR;
int SPEC_Y, SPEC_H;
} // namespace ui
```

and assign them at the end of `pal::setTheme`:

```cpp
    ui::DIAL_Y  = t.dialY;  ui::DIAL_H  = t.dialH;
    ui::DIAL_CX = t.dialCx; ui::DIAL_CY = t.dialCy;
    ui::R_FIELD = t.rField; ui::R_RUNES = t.rRunes;
    ui::R_BOUND = t.rBound; ui::R_STAR  = t.rStar;
    ui::SPEC_Y  = t.specY;  ui::SPEC_H  = t.specH;
```

- [ ] **Step 3: Skip the spectrum when a theme has none**

In `ui/screen_scan.cpp`, find the call to `drawSpectrum(c, s);` inside `drawScan` and guard it:

```cpp
    if (SPEC_H > 0) drawSpectrum(c, s);
```

Find the dirty-region entry for the spectrum in `drawScan` and guard it the same way, so a theme without a strip never marks that rectangle.

- [ ] **Step 4: Skip the spectrum hit test when a theme has none**

In `ui/nav.cpp`, in `onTap`, change the spectrum branch:

```cpp
        // The spectrum opens the mesh. A theme without a strip has nothing
        // there, and a tap into empty panel must not navigate.
        if (SPEC_H > 0 && hit(x, y, COL_R_X, SPEC_Y, COL_R_W, SPEC_H)) {
            n.screen = SCR_MESH;
            return true;
        }
```

- [ ] **Step 5: Verify UMBRAVOX is byte-identical**

Run: `cd sim && make clean && make -s && make -s regress && make -s test`
Expected: `regress: UMBRAVOX unchanged`, and all five tests pass.

- [ ] **Step 6: Verify the second theme's dial actually moved**

Run: `./umbravox /tmp/g1.png --theme 1 --frame 0 --scale 4`

Open `/tmp/g1.png`. Expected: the circle is larger and lower than on theme 0, and there is no spectrum bar under it. The rune ring is still the centrepiece — the seal arrives in Task 6.

- [ ] **Step 7: Commit**

```bash
git add -A
git commit -m "refactor: move the dial geometry into the theme table"
```

---

### Task 5: The asset generator and the seal bitmaps

**Files:**
- Create: `tools/gen_sigil.py`
- Create: `ui/sigil.h` (generated — never hand-edited)

**Interfaces:**
- Consumes: the source artwork at `private/5C687C2B-0DD7-4E8B-BE61-A3D8641B5426.png`.
- Produces: `ui::FACE` (50×50, 4bpp, 1250 bytes), `ui::FACE_W`, `ui::FACE_H`, `ui::BIG` (112×112, 4bpp, 6272 bytes), `ui::BIG_W`, `ui::BIG_H`, and the offsets `EYE_L_X/Y`, `EYE_R_X/Y`, `FLAME_X/Y`, `PENTA_X/Y`.

- [ ] **Step 1: Write the generator**

Create `tools/gen_sigil.py`:

```python
#!/usr/bin/env python3
"""Generate ui/sigil.h from the Baphomet artwork.

The source is a TEXTURED drawing: every stroke is speckled inside and ragged
at the edge. Resampling it directly averages that speckle into mid-grey noise
and the face reads as mush at 50px. The order of operations below is the
whole specification -- getting it wrong does not fail, it just looks bad:

  threshold to solid line work  ->  close the pinholes the texture left
  ->  dilate so a stroke survives a 20x reduction  ->  area-average down

Never Lanczos. It rings on hard edges and puts the noise straight back.

Usage:  python3 tools/gen_sigil.py            # writes ui/sigil.h
"""
import math
import os
import sys

import numpy as np
from PIL import Image
from scipy import ndimage

SRC = "private/5C687C2B-0DD7-4E8B-BE61-A3D8641B5426.png"
OUT = "ui/sigil.h"

THR, CLOSE, DIL, GAIN = 52, 5, 1, 1.15
FACE_H_PX = 50          # the face, sized to sit inside R_FIELD on BAPH0MAU5
BIG_PX = 112            # the whole seal, for the boot splash

# Where the live parts of the seal sit, as fractions of the trimmed artwork.
FEATURES = {
    "EYE_L": (0.447, 0.467),
    "EYE_R": (0.558, 0.467),
    "FLAME": (0.500, 0.335),
    "PENTA": (0.500, 0.653),
}


def solid(a0, rr, rmax):
    """Threshold, close and dilate -- line work, not texture."""
    m = (a0 > THR) & (rr <= rmax)
    m = ndimage.binary_closing(m, np.ones((CLOSE, CLOSE)))
    m = ndimage.binary_dilation(m, np.ones((DIL, DIL)))
    return m


def pack4(im):
    """16 levels, two pixels per byte."""
    a = np.array(im).astype(np.float32) / 255.0
    q = np.rint(np.clip(a * GAIN, 0, 1) * 15).astype(np.int32)
    h, w = q.shape
    flat = q.flatten()
    if len(flat) % 2:
        flat = np.append(flat, 0)
    packed = [(int(flat[i]) << 4) | int(flat[i + 1]) for i in range(0, len(flat), 2)]
    return packed, w, h


def emit(f, name, im):
    by, w, h = pack4(im)
    f.write("// %s: %dx%d, 4bpp, 2 px per byte -- %d bytes\n" % (name, w, h, len(by)))
    f.write("constexpr int %s_W = %d;\nconstexpr int %s_H = %d;\n" % (name, w, name, h))
    f.write("const uint8_t %s[] = {\n" % name)
    for i in range(0, len(by), 16):
        f.write("    " + "".join("0x%02X," % b for b in by[i:i + 16]) + "\n")
    f.write("};\n\n")


def main():
    if not os.path.exists(SRC):
        sys.exit("gen_sigil: cannot find %s -- run from the repo root" % SRC)

    src = Image.open(SRC).convert("L")
    art = src.crop(src.point(lambda v: 255 if v > 24 else 0).getbbox())
    a0 = np.array(art)
    h, w = a0.shape
    cx = w / 2.0
    cy = h / 2.0
    yy, xx = np.mgrid[0:h, 0:w]
    rr = np.hypot(xx - cx, yy - cy) / (w / 2.0)

    # The face: horns, head, flame, beard and pentacle. The ring and the
    # inscribed figure are NOT baked -- they are drawn, because a drawn
    # figure can answer to the state where a bitmap can only sit there.
    mface = solid(a0, rr, 0.45)
    ys, xs = np.where(mface)
    bb = (xs.min(), ys.min(), xs.max() + 1, ys.max() + 1)
    face = Image.fromarray((mface[bb[1]:bb[3], bb[0]:bb[2]] * 255).astype(np.uint8))
    fw, fh = face.size
    tw = max(1, round(fw * FACE_H_PX / fh))

    full = Image.fromarray((solid(a0, rr, 1.01) * 255).astype(np.uint8))

    with open(OUT, "w") as f:
        f.write("// GENERATED by tools/gen_sigil.py -- do not hand-edit.\n")
        f.write("//\n")
        f.write("// Regenerate with:  python3 tools/gen_sigil.py\n")
        f.write("//\n")
        f.write("// Level 0 is transparent, so the figure drawn behind the face shows\n")
        f.write("// through exactly as it does in the original artwork.\n")
        f.write("#pragma once\n#include <stdint.h>\n\nnamespace ui {\n\n")
        emit(f, "FACE", face.resize((tw, FACE_H_PX), Image.BOX))
        emit(f, "BIG", full.resize((BIG_PX, BIG_PX), Image.BOX))
        f.write("// Offsets from the FACE bitmap's centre. The animation code reads\n")
        f.write("// these rather than hardcoding a pixel, so resizing the asset does\n")
        f.write("// not silently move the eyes.\n")
        for name, (fx, fy) in FEATURES.items():
            ox = round((fx * w - bb[0]) * FACE_H_PX / fh - tw / 2)
            oy = round((fy * h - bb[1]) * FACE_H_PX / fh - FACE_H_PX / 2)
            f.write("constexpr int %s_X = %d, %s_Y = %d;\n" % (name, ox, name, oy))
        f.write("\n} // namespace ui\n")

    print("wrote %s: FACE %dx%d, BIG %dx%d" % (OUT, tw, FACE_H_PX, BIG_PX, BIG_PX))


if __name__ == "__main__":
    main()
```

- [ ] **Step 2: Run it**

Run: `cd /home/mummydust/Documents/squach/technomancer && python3 -I tools/gen_sigil.py`
Expected: `wrote ui/sigil.h: FACE 50x50, BIG 112x112`

- [ ] **Step 3: Verify the generated sizes and that it is valid C++**

Run: `grep -E "_W =|_H =|_X =|bytes" ui/sigil.h`
Expected, exactly:

```
// FACE: 50x50, 4bpp, 2 px per byte -- 1250 bytes
constexpr int FACE_W = 50;
constexpr int FACE_H = 50;
// BIG: 112x112, 4bpp, 2 px per byte -- 6272 bytes
constexpr int BIG_W = 112;
constexpr int BIG_H = 112;
constexpr int EYE_L_X = -6, EYE_L_Y = -4;
constexpr int EYE_R_X = 6, EYE_R_Y = -4;
constexpr int FLAME_X = 0, FLAME_Y = -18;
constexpr int PENTA_X = 0, PENTA_Y = 17;
```

If `FACE_W` is not 50, the mask radius or `FACE_H_PX` has drifted. Do not hand-correct the header — fix the generator and re-run.

Run: `echo '#include "ui/sigil.h"
int main(){ return ui::FACE[0] + ui::BIG[0]; }' > /tmp/sigil_check.cpp && g++ -std=c++17 -I. -o /tmp/sigil_check /tmp/sigil_check.cpp && echo "compiles"`
Expected: `compiles`

- [ ] **Step 4: Commit**

```bash
git add -A
git commit -m "feat: generate the Baph0mau5 seal bitmaps from the artwork"
```

---

### Task 6: The BAPH0MAU5 centrepiece

**Files:**
- Create: `ui/seal.h`, `ui/seal.cpp`
- Modify: `ui/screen_scan.cpp`, `sim/Makefile:7`

**Interfaces:**
- Consumes: `ui::FACE`, `ui::BIG` and the feature offsets from Task 5; `pal::RAMP`, `pal::LIVE`, `pal::LIVE_HI` from Task 2; `ui::R_FIELD`, `ui::R_RUNES`, `ui::R_STAR` from Task 4.
- Produces: `void ui::drawSeal(Canvas&, int cx, int cy, int rField, int rRunes, int rStar, int rBound, uint32_t frame, bool found, int foundAngle, uint8_t heat)`. `heat` is 0..255 possession; pass `0` for idle. `found` is whether the search has just turned a contact up.

- [ ] **Step 1: Write the seal header**

Create `ui/seal.h`:

```cpp
// The BAPH0MAU5 centrepiece.
//
// Their art carries the face; the figure around it is drawn rather than
// baked. Lines stay sharp at any size, cost nothing to store, and -- the
// reason that actually matters -- a drawn figure can answer to the state
// where a bitmap can only sit there.
#pragma once
#include <stdint.h>
#include "canvas.h"

namespace ui {

// Draw the seal centred on cx,cy.
//
//   frame       the animation clock
//   found       the search has just turned a contact up; the eyes answer it
//   foundAngle  its bearing, so the eyes can lean a pixel toward it
//   heat        0..255 possession -- how far the fire has spread. 0 is idle.
void drawSeal(Canvas& c, int cx, int cy, int rField, int rRunes, int rStar,
              int rBound, uint32_t frame, bool found, int foundAngle,
              uint8_t heat);

// Where the eyes sit, as offsets from the seal's centre.
//
// This exists so that ui/sigil.h is included by exactly one translation unit.
// Its arrays are `const` at namespace scope and therefore internal linkage,
// so a second #include does not fail to link -- it silently compiles another
// 7.5 KB copy of them into that object file. The gaze needs these two numbers
// and nothing else from the header.
void sealEyes(int& lx, int& rx, int& y);

} // namespace ui
```

- [ ] **Step 2: Write the seal implementation**

Create `ui/seal.cpp`:

```cpp
#include "seal.h"
#include "palette.h"
#include "sigil.h"
#include <math.h>

namespace ui {
namespace {

void polar(int cx, int cy, float r, float deg, int& x, int& y) {
    const float a = deg * 3.14159265f / 180.0f;
    x = cx + (int)lrintf(sinf(a) * r);
    y = cy - (int)lrintf(cosf(a) * r);
}

// A level of the bitmap, read in chalk.
uint16_t ramp16(int lv) {
    const int p = lv * 3, i = p / 15, f = p % 15;
    return i >= 3 ? pal::RAMP[3] : Canvas::mix(pal::RAMP[i], pal::RAMP[i + 1], f, 15);
}

// ...and the same level read in witchfire. Used to set parts of the seal
// alight by recolouring the strokes that are already there. Drawing a green
// mark ON TOP of the artwork read as a sticker; letting its own line work
// catch is what makes it look like the thing is doing it.
uint16_t ramp16live(int lv) {
    return lv < 8 ? Canvas::mix(pal::VOID, pal::LIVE, lv, 8)
                  : Canvas::mix(pal::LIVE, pal::LIVE_HI, lv - 8, 7);
}

// A circular patch of the seal. `dark` puts it out instead of lighting it,
// which is how the blink works: the eyes are not covered, they stop being
// there.
struct Tint { int x, y, r; bool dark; };

void blit(Canvas& c, const uint8_t* bm, int bw, int bh, int cx, int cy,
          int lift, int clipR, const Tint* tint, int nTint) {
    const int x0 = cx - bw / 2, y0 = cy - bh / 2;
    for (int j = 0; j < bh; j++)
        for (int i = 0; i < bw; i++) {
            const int k = j * bw + i;
            const int lv = (bm[k >> 1] >> (4 - 4 * (k & 1))) & 15;
            if (!lv) continue;
            const int px = x0 + i, py = y0 + j;
            const int dx = px - cx, dy = py - cy;
            if (dx * dx + dy * dy > clipR * clipR) continue;

            bool hot = false, dark = false;
            for (int t = 0; t < nTint && !hot; t++) {
                const int tx = px - (cx + tint[t].x), ty = py - (cy + tint[t].y);
                if (tx * tx + ty * ty <= tint[t].r * tint[t].r) {
                    hot = true; dark = tint[t].dark;
                }
            }
            if (hot) {
                if (!dark) c.pixel(px, py, ramp16live(lv));
                continue;
            }
            const uint16_t a = ramp16(lv);
            c.pixel(px, py, lift ? Canvas::mix(a, ramp16(lv < 13 ? lv + 2 : 15),
                                               lift, 255) : a);
        }
}

// The inscribed figure: a diamond and two triangles, on the same circle.
void figure(Canvas& c, int cx, int cy, int r, uint16_t col, uint16_t lit) {
    int x[4], y[4];
    for (int k = 0; k < 4; k++) polar(cx, cy, (float)r, (float)k * 90.0f, x[k], y[k]);
    for (int k = 0; k < 4; k++) c.line(x[k], y[k], x[(k + 1) % 4], y[(k + 1) % 4], col);
    int t[3], u[3], d[3], e[3];
    for (int k = 0; k < 3; k++) {
        polar(cx, cy, (float)r, (float)k * 120.0f,         t[k], u[k]);
        polar(cx, cy, (float)r, (float)k * 120.0f + 60.0f, d[k], e[k]);
    }
    for (int k = 0; k < 3; k++) {
        c.line(t[k], u[k], t[(k + 1) % 3], u[(k + 1) % 3], lit);
        c.line(d[k], e[k], d[(k + 1) % 3], e[(k + 1) % 3], col);
    }
}

// Eight marks from the seal's own vocabulary, at 5px so the band can walk.
// The bitmap holds the face; the ring is procedural, which is what makes it
// affordable to turn every frame.
constexpr int MARK_N = 8;
const uint8_t MARK[MARK_N][5] = {
    {0b01110, 0b11000, 0b10000, 0b11000, 0b01110}, // crescent
    {0b01010, 0b00100, 0b01110, 0b00100, 0b01110}, // mercury
    {0b00100, 0b01110, 0b00100, 0b01110, 0b00100}, // brimstone cross
    {0b01110, 0b00100, 0b01110, 0b00100, 0b01010}, // sulfur
    {0b01110, 0b00100, 0b11111, 0b00100, 0b01110}, // leviathan
    {0b00100, 0b11111, 0b01110, 0b01010, 0b10001}, // inverted pentacle
    {0b01010, 0b00000, 0b00100, 0b00000, 0b01010}, // the dots
    {0b11000, 0b00100, 0b01110, 0b00100, 0b00011}, // the ninth sign
};

void mark5(Canvas& c, int i, int cx, int cy, uint16_t col) {
    const uint8_t* m = MARK[((i % MARK_N) + MARK_N) % MARK_N];
    for (int j = 0; j < 5; j++)
        for (int k = 0; k < 5; k++)
            if ((m[j] >> (4 - k)) & 1) c.pixel(cx - 2 + k, cy - 2 + j, col);
}

// Rare, and the whole point is that it is rare: caught out of the corner of
// the eye, and not quite believed.
constexpr int BLINK_EVERY = 200;

} // namespace

void drawSeal(Canvas& c, int cx, int cy, int rField, int rRunes, int rStar,
              int rBound, uint32_t frame, bool found, int foundAngle,
              uint8_t heat) {
    // A double-thump, not a sine. A sine is what every interface does when it
    // wants to look alive and it reads as a bulb; two quick beats and a
    // longer rest is the shape of something with a heart in it.
    const int p = (int)(frame % 30);
    const int d = p < 5 ? 5 - p : (p >= 8 && p < 12) ? 12 - p : 0;
    const int lift = 118 + d * 26;

    figure(c, cx, cy, rStar,
           Canvas::mix(pal::VOID, pal::EDGE_LIT, 160, 255),
           Canvas::mix(pal::VOID, pal::WITCH,    130, 255));

    Tint tint[6];
    int n = 0;

    if (heat) {
        // POSSESSION. The fire spreads as the evidence mounts, so a glance at
        // the dial says how bad it is without reading a number.
        if (heat >= 40)  { tint[n++] = Tint{EYE_L_X, EYE_L_Y, 4, false};
                           tint[n++] = Tint{EYE_R_X, EYE_R_Y, 4, false}; }
        if (heat >= 110) { tint[n++] = Tint{-15, -8, 8, false};
                           tint[n++] = Tint{ 15, -8, 8, false}; }
        if (heat >= 180) { tint[n++] = Tint{PENTA_X, PENTA_Y, 6, false}; }
        if (heat >= 235) { n = 0; tint[n++] = Tint{0, 0, rField, false}; }
    } else {
        // The eyes answer a FINDING, not the clock. Tying the flare to the
        // search's position instead made the face strobe on a timer.
        if (found) {
            const int lean = foundAngle > 180 ? -1 : 1;
            tint[n++] = Tint{EYE_L_X + lean, EYE_L_Y, 4, false};
            tint[n++] = Tint{EYE_R_X + lean, EYE_R_Y, 4, false};
        }
        // The pentacle beats once per channel dwell, which makes the seal the
        // clock as well as the field.
        if ((frame % 22) < 4) tint[n++] = Tint{PENTA_X, PENTA_Y, 5, false};
    }

    // The flame above the horns licks: the patch that catches grows and
    // shrinks. At this size a flame is a handful of strokes disagreeing.
    const int lick = 3 + (int)(2.4f * sinf((float)frame * 0.37f));
    if (n < 6) tint[n++] = Tint{FLAME_X, FLAME_Y, lick, false};

    if ((frame % BLINK_EVERY) < 2 && n < 5) {
        tint[n++] = Tint{EYE_L_X, EYE_L_Y, 4, true};
        tint[n++] = Tint{EYE_R_X, EYE_R_Y, 4, true};
    }

    blit(c, FACE, FACE_W, FACE_H, cx, cy, lift, rField - 1, tint, n);

    c.circle(cx, cy, rField, pal::EDGE_LIT);
    c.circle(cx, cy, rBound, pal::EDGE_LIT);

    // The band walks against the search. Marks near the beam take its light;
    // the rest sit back, so the ring never reads as a plain border.
    const float beam = (float)((frame * 5) % 360);
    const float spin = -(float)frame * 0.7f;
    for (int k = 0; k < MARK_N * 2; k++) {
        const float a = spin + (float)k * (360.0f / (MARK_N * 2));
        int rx, ry;
        polar(cx, cy, (float)rRunes, a, rx, ry);
        const int da = ((int)(beam - a) % 360 + 360) % 360;
        const int near = da < 180 ? da : 360 - da;
        const int lum = near < 50 ? 255 - near * 2 : 90;
        mark5(c, k, rx, ry, Canvas::mix(pal::VOID, pal::BONE, lum, 255));
    }
}

void sealEyes(int& lx, int& rx, int& y) {
    lx = EYE_L_X; rx = EYE_R_X; y = EYE_L_Y;
}

} // namespace ui
```

- [ ] **Step 3: Add seal.cpp to the build**

In `sim/Makefile` line 7, append ` ../ui/seal.cpp` to `SRC`.

- [ ] **Step 4: Branch the centrepiece in drawDial**

In `ui/screen_scan.cpp`, add `#include "seal.h"` and `#include "nav.h"` at the top.

Inside `drawDial`, wrap the UMBRAVOX centrepiece. Everything from `latticeDisc(c, DIAL_CX, DIAL_CY, R_FIELD - 1);` down to and including the rune-ring loop becomes the `else` branch of:

```cpp
    if (s.set.theme == pal::TH_BAPH0MAU5) {
        drawSeal(c, DIAL_CX, DIAL_CY, R_FIELD, R_RUNES, R_STAR, R_BOUND,
                 s.frame, /*found=*/false, /*foundAngle=*/0, /*heat=*/0);
    } else {
        // ... the existing lattice, heptagram, range rings, circles and
        // rune ring, unchanged ...
    }
```

Leave the sweep, the contacts and the centre pixel outside the branch — both themes draw those, and Task 7 replaces the sweep.

- [ ] **Step 5: Verify UMBRAVOX is byte-identical and the seal appears**

Run: `cd sim && make clean && make -s && make -s regress && make -s test`
Expected: `regress: UMBRAVOX unchanged`, all five tests pass.

Run: `./umbravox /tmp/seal.png --theme 1 --frame 26 --scale 4`

Open `/tmp/seal.png`. Expected: a chalk-white horned face inside the circle, with horns, X-eyes, teeth and the beard pentacle all legible, and eight alchemical marks around the band. If the face reads as a blur, `ui/sigil.h` was generated with the wrong parameters — re-run Task 5.

- [ ] **Step 6: Commit**

```bash
git add -A
git commit -m "feat: draw the Baph0mau5 centrepiece"
```

---

### Task 7: The five searches

**Files:**
- Create: `ui/sweep.h`, `ui/sweep.cpp`
- Modify: `ui/screen_scan.cpp`, `sim/Makefile:7`

**Interfaces:**
- Consumes: `ui::Sweep` from Task 3; `pal::SWEEP`, `pal::SWEEP_HI` from Task 2.
- Produces: `void ui::drawSearch(Canvas&, uint8_t mode, int cx, int cy, int rField, int eyeLx, int eyeRx, int eyeY, uint32_t frame)` and `bool ui::searchTouches(uint8_t mode, uint32_t frame, int angle, int dist)`.

- [ ] **Step 1: Write the header**

Create `ui/sweep.h`:

```cpp
// How the dial searches the field.
//
// The rotating radar sweep is a convention borrowed from an instrument, and
// it is the most dashboard-shaped thing on the screen. These are the other
// ways of asking the same question, kept as a setting because which one
// reads best is taste rather than correctness.
#pragma once
#include <stdint.h>
#include "canvas.h"

namespace ui {

// Draw the search. `eyeLx`, `eyeRx` and `eyeY` are offsets from cx,cy and are
// only read by SW_GAZE, which emits from the seal's eyes rather than from the
// middle of the circle.
void drawSearch(Canvas& c, uint8_t mode, int cx, int cy, int rField,
                int eyeLx, int eyeRx, int eyeY, uint32_t frame);

// Is this contact being touched by the search, right now?
//
// Every mode needs its own answer and they are not interchangeable: a sweep
// finds by BEARING, a pulse by DISTANCE, and a wash does not find at all --
// it only shows what is already known.
bool searchTouches(uint8_t mode, uint32_t frame, int angle, int dist);

} // namespace ui
```

- [ ] **Step 2: Write the implementation**

Create `ui/sweep.cpp`:

```cpp
#include "sweep.h"
#include "palette.h"
#include "model.h"
#include <math.h>

namespace ui {
namespace {

void polar(int cx, int cy, float r, float deg, int& x, int& y) {
    const float a = deg * 3.14159265f / 180.0f;
    x = cx + (int)lrintf(sinf(a) * r);
    y = cy - (int)lrintf(cosf(a) * r);
}

// A ray from an arbitrary origin, clipped to the field. The gaze starts at
// the eyes rather than the centre, so it cannot just be drawn to a radius --
// it has to be walked and stopped at the bound.
void ray(Canvas& c, int ox, int oy, float deg, int cx, int cy, int R, uint16_t col) {
    const float a = deg * 3.14159265f / 180.0f;
    const float sx = sinf(a), sy = -cosf(a);
    for (float t = 0; t < 2.0f * (float)R; t += 0.7f) {
        const int x = ox + (int)lrintf(sx * t), y = oy + (int)lrintf(sy * t);
        const int dx = x - cx, dy = y - cy;
        if (dx * dx + dy * dy > R * R) break;
        c.pixel(x, y, col);
    }
}

float beamOf(uint32_t frame) { return (float)((frame * 5) % 360); }

} // namespace

bool searchTouches(uint8_t mode, uint32_t frame, int angle, int dist) {
    switch (mode) {
        case SW_PULSE: {
            const int r  = (int)(frame * 1.15f) % 31;
            const int dp = dist * 29 / 100;
            const int d  = dp - r;
            return d <= 0 && d > -5;
        }
        case SW_STAR: {
            const float spin = (float)frame * 2.4f;
            for (int k = 0; k < 5; k++) {
                const int d = (((int)(spin + (float)k * 72.0f) - angle) % 360 + 360) % 360;
                if (d <= 20) return true;
            }
            return false;
        }
        case SW_CANDLE:
            return false;   // nothing sweeps, so nothing is ever just found
        default:
            return (((int)beamOf(frame) - angle + 360) % 360) <= 26;
    }
}

void drawSearch(Canvas& c, uint8_t mode, int cx, int cy, int rField,
                int eyeLx, int eyeRx, int eyeY, uint32_t frame) {
    const float beam = beamOf(frame);

    switch (mode) {

    case SW_GAZE: {
        // Two beams, one from each eye, pointed the same way. The light comes
        // out of the face rather than out of the middle of the circle, so the
        // seal is doing the looking instead of sitting behind an instrument
        // that looks on its behalf.
        const int ox[2] = {cx + eyeLx, cx + eyeRx};
        const int oy    = cy + eyeY;
        const int TAIL  = 26;
        for (int s = 0; s < 2; s++)
            for (int d = TAIL; d >= 0; d--) {
                const int fade = TAIL - d;
                const int lum  = 220 * fade * fade / (TAIL * TAIL);
                if (lum < 10) continue;
                ray(c, ox[s], oy, beam - (float)d * 0.8f, cx, cy, rField - 1,
                    Canvas::mix(pal::VOID, d == 0 ? pal::SWEEP_HI : pal::SWEEP,
                                d == 0 ? 170 : lum / 4, 255));
            }
        break;
    }

    case SW_PULSE: {
        // A ring leaves the centre and dies at the bound. It finds by
        // DISTANCE rather than bearing -- near things answer first -- which
        // reads as divination rather than a dish going round.
        const int span = rField - 2;
        const int r0 = 2 + (int)(frame * 1.15f) % span;
        for (int t = 0; t < 6; t++) {
            const int r = r0 - t * 2;
            if (r < 2 || r > rField - 1) continue;
            const int out = 255 - r * 150 / span;
            const int lum = (t ? 165 - t * 26 : 255) * (160 + out * 95 / 255) / 255;
            if (lum < 10) continue;
            if (t == 0) {
                // Two radii, not one: a single-pixel ring crossing the chalk
                // simply disappears into it.
                const uint16_t hi = Canvas::mix(pal::VOID, pal::SWEEP_HI, lum, 255);
                c.circle(cx, cy, r, hi);
                if (r > 2) c.circle(cx, cy, r - 1, hi);
            } else {
                c.dottedCircle(cx, cy, r, Canvas::mix(pal::VOID, pal::SWEEP, lum, 255), 2);
            }
        }
        break;
    }

    case SW_STAR: {
        // The inscribed figure is the search. Five limbs means the dial
        // refreshes five times a revolution, so nothing waits a whole turn.
        const float spin = (float)frame * 2.4f;
        for (int k = 0; k < 5; k++)
            for (int d = 14; d >= 0; d--) {
                const int lum = 200 * (14 - d) / 14;
                if (lum < 12) continue;
                int x, y;
                polar(cx, cy, (float)(rField - 1), spin + (float)k * 72.0f - (float)d, x, y);
                c.line(cx, cy, x, y,
                       Canvas::mix(pal::VOID, d ? pal::SWEEP : pal::SWEEP_HI,
                                   d ? lum / 4 : 140, 255));
            }
        break;
    }

    case SW_CANDLE: {
        // No beam. The circle is lit from the middle by something guttering,
        // and what is in it is simply there to be seen.
        const float g = sinf((float)frame * 0.41f)
                      + 0.6f * sinf((float)frame * 1.07f)
                      + 0.3f * sinf((float)frame * 2.3f);
        const int jx = (int)lrintf(1.6f * sinf((float)frame * 0.23f));
        const int jy = (int)lrintf(1.2f * sinf((float)frame * 0.31f));
        for (int t = 0; t < 4; t++) {
            const int r = rField - 3 - t * 5;
            if (r < 3) break;
            const int lum = (int)((42 - t * 7) * (1.0f + 0.4f * g));
            if (lum < 6) continue;
            c.dottedCircle(cx + jx, cy + jy, r,
                           Canvas::mix(pal::VOID, pal::SWEEP, lum, 255), 4);
        }
        break;
    }

    default: {
        // The radar. One hot edge with a cubic afterglow.
        const int TAIL = 40;
        for (int d = TAIL; d >= 1; d--) {
            const int fade = TAIL - d;
            const int lum = 255 * fade * fade * fade / (TAIL * TAIL * TAIL);
            if (lum < 8) continue;
            int x, y;
            polar(cx, cy, (float)(rField - 1), beam - (float)d, x, y);
            c.line(cx, cy, x, y, Canvas::mix(pal::VOID, pal::SWEEP, lum, 255));
        }
        int bx, by;
        polar(cx, cy, (float)(rField - 1), beam, bx, by);
        c.line(cx, cy, bx, by, pal::SWEEP_HI);
        break;
    }
    }
}

} // namespace ui
```

**The radar branch must reproduce today's output exactly.** Today's code uses `pal::WITCH` and `pal::WITCH_HI`; here it uses `pal::SWEEP` and `pal::SWEEP_HI`, which the UMBRAVOX table row sets to those same two colours. The `regress` target in Step 5 is what proves it.

- [ ] **Step 3: Add sweep.cpp to the build**

In `sim/Makefile` line 7, append ` ../ui/sweep.cpp` to `SRC`.

- [ ] **Step 4: Call it from drawDial**

In `ui/screen_scan.cpp`, add `#include "sweep.h"`. Delete the existing sweep block (the `TAIL = 40` loop and the two `polar`/`c.line` calls after it) and replace with:

```cpp
    int eyeLx, eyeRx, eyeY;
    sealEyes(eyeLx, eyeRx, eyeY);
    drawSearch(c, s.set.sweep, DIAL_CX, DIAL_CY, R_FIELD,
               eyeLx, eyeRx, eyeY, s.frame);
```

`seal.h` is already included from Task 6. **Do not include `sigil.h` here** — its
arrays are `const` at namespace scope, so a second include compiles another
7.5 KB copy of them into this object file rather than failing to link, which is
the kind of waste that never announces itself.

Replace the contact hot test. Change:

```cpp
        const int behind = ((int)beam - (int)k.angle + 360) % 360;
        const bool hot = behind <= 26;
```

to:

```cpp
        const bool hot = searchTouches(s.set.sweep, s.frame, k.angle, k.dist);
```

Then pass the finding into the seal. Above the centrepiece branch, add:

```cpp
    // The eyes answer a finding, so the seal needs to know whether the search
    // has just turned one up and roughly where.
    bool found = false;
    int  foundAngle = 0;
    for (int i = 0; i < s.nContacts; i++)
        if (searchTouches(s.set.sweep, s.frame, s.contacts[i].angle, s.contacts[i].dist)) {
            found = true; foundAngle = s.contacts[i].angle; break;
        }
```

and change the `drawSeal` call from Task 6 to:

```cpp
        drawSeal(c, DIAL_CX, DIAL_CY, R_FIELD, R_RUNES, R_STAR, R_BOUND,
                 s.frame, found, foundAngle,
                 s.alertActive ? (uint8_t)(s.alertConf * 85) : 0);
```

- [ ] **Step 5: Verify UMBRAVOX is byte-identical**

Run: `cd sim && make clean && make -s && make -s regress && make -s test`
Expected: `regress: UMBRAVOX unchanged`, all five tests pass.

If `scan.png` differs, the radar branch is not reproducing the original. Compare against `private/baseline/ui-pristine/screen_scan.cpp` — the most likely cause is the leading-edge colour, which was `pal::WITCH_HI` unmixed.

- [ ] **Step 6: Render every sweep and look at them**

Run: `for m in 0 1 2 3 4; do ./umbravox /tmp/sw-$m.png --theme 1 --sweep $m --frame 26 --scale 4; done`

This needs a `--sweep` flag. In `sim/main_sim.cpp` add, beside the `--theme` branch:

```cpp
        else if (!strcmp(argv[i], "--sweep") && i + 1 < argc) sweepId = (uint8_t)atoi(argv[++i]);
```

with `uint8_t sweepId = ui::SW_RADAR;` declared beside `themeId`, and set `s.set.sweep = sweepId; s.set.theme = themeId;` on the `State` immediately after `ui::State s = ui::demoState(f);`.

Expected: five distinguishable images — a rotating beam, two beams from the eyes, an expanding ring, five turning limbs, and no beam at all.

- [ ] **Step 7: Commit**

```bash
git add -A
git commit -m "feat: add the five dial searches"
```

---

### Task 8: drawDial takes its geometry as arguments

The ASPECT screen previews the dial at a smaller radius in a different place. It must call the real `drawDial`, for the same reason `layout.h` exists: a preview that is a separate drawing of a dial will drift from the dial.

**Files:**
- Modify: `ui/screens.h`, `ui/screen_scan.cpp`

**Interfaces:**
- Consumes: everything from Tasks 4, 6 and 7.
- Produces: `void ui::drawDialAt(Canvas&, const State&, int cx, int cy, int rField, int rRunes, int rStar, int rBound)`, declared in `screens.h`.

- [ ] **Step 1: Declare it**

In `ui/screens.h`, below the `drawScan` declaration:

```cpp
// The dial, drawn anywhere at any size.
//
// ASPECT previews the dial while you choose a theme and a sweep for it, and
// the preview has to BE the dial rather than a picture of one -- the same
// reason ui/layout.h exists. drawScan() calls this with the active theme's
// geometry; ASPECT calls it with the preview's.
void drawDialAt(Canvas& c, const State& s, int cx, int cy,
                int rField, int rRunes, int rStar, int rBound);
```

- [ ] **Step 2: Parameterise the existing function**

In `ui/screen_scan.cpp`, rename `drawDial` to `drawDialAt`, move it out of the anonymous namespace into `namespace ui`, and give it the new signature. Inside the body, replace every `DIAL_CX` with `cx`, every `DIAL_CY` with `cy`, every `R_FIELD` with `rField`, every `R_RUNES` with `rRunes`, every `R_STAR` with `rStar` and every `R_BOUND` with `rBound`.

The panel and the `CH` / `-90` labels are **not** part of the dial — they belong to the scan screen's card. Move those out of `drawDialAt` and into `drawScan`, immediately before the call.

Then in `drawScan`, replace the old `drawDial(c, s);` call with:

```cpp
    panel(c, COL_R_X, DIAL_Y, COL_R_W, DIAL_H,
          Canvas::mix(pal::VOID, pal::PANEL, 1, 2), true);
    {
        char ch[8] = {'C', 'H', 0};
        u2s(ch + 2, s.chan, 2);
        c.textRight(COL_R_X + COL_R_W - 4, DIAL_Y + 3, ch, pal::DIM);
        c.text(COL_R_X + 3, DIAL_Y + DIAL_H - 9, "-90", pal::DIM);
    }
    drawDialAt(c, s, DIAL_CX, DIAL_CY, R_FIELD, R_RUNES, R_STAR, R_BOUND);
```

- [ ] **Step 3: Verify UMBRAVOX is byte-identical**

Run: `cd sim && make clean && make -s && make -s regress && make -s test`
Expected: `regress: UMBRAVOX unchanged`, all five tests pass.

This is a pure refactor. Any difference means a label or the panel moved.

- [ ] **Step 4: Commit**

```bash
git add -A
git commit -m "refactor: let drawDialAt take its geometry as arguments"
```

---

### Task 9: The ASPECT screen

**Files:**
- Create: `ui/screen_aspect.cpp`
- Modify: `ui/screens.h`, `ui/nav.h`, `ui/nav.cpp`, `ui/screen_grimoire.cpp`, `ui/layout.h`, `device/main.cpp`, `sim/Makefile:7`, `sim/main_sim.cpp`

**Interfaces:**
- Consumes: `drawDialAt` from Task 8; `pal::themeName`, `pal::sweepMask` from Task 2; `settingsEqual`, `clampSettings` from Task 3.
- Produces: `void ui::drawAspect(Canvas&, const State&, Dirty* = nullptr)`, `ui::SCR_ASPECT`, and `const char* ui::sweepName(uint8_t)`.

- [ ] **Step 1: Add the screen id and layout constants**

In `ui/nav.h`, add `SCR_ASPECT` to the `Screen` enum, **before** `SCR_COUNT`:

```cpp
enum Screen : uint8_t { SCR_SCAN = 0, SCR_CODEX, SCR_DETAIL, SCR_DOMAIN, SCR_MESH, SCR_OMENS, SCR_ECHOES, SCR_GRIMOIRE, SCR_ATTUNE, SCR_ASPECT, SCR_COUNT };
```

In `ui/layout.h`, add:

```cpp
// ASPECT: a live dial over two pickers. The preview is big enough to judge a
// sweep by; the rows reuse ATTUNE's button geometry so the controls are the
// same object in both places.
constexpr int ASP_CY     = 45;      // preview centre
constexpr int ASP_R      = 32;      // preview outer bound
constexpr int ASP_ROW_Y  = 78;      // first picker row
constexpr int ASP_ROW_H  = 12;
constexpr int ASP_ROWS   = 2;       // THEME, SWEEP
```

In `ui/screens.h`, add:

```cpp
// ASPECT -- what the instrument looks like: the theme, and how the dial
// searches the field.
void drawAspect(Canvas& c, const State& s, Dirty* d = nullptr);

// Display name for a Sweep id.
const char* sweepName(uint8_t id);
```

- [ ] **Step 2: Write the screen**

Create `ui/screen_aspect.cpp`:

```cpp
// ASPECT -- what the instrument looks like.
//
// Appearance is not a capture trade-off, so it does not belong in ATTUNE
// beside dwell and BLE duty. ATTUNE is also full: layout.h documents seven
// rows as its ceiling, and all seven are used.
//
// The preview calls the real dial rather than drawing a picture of one. A
// picture would drift, and the whole reason ui/layout.h exists is that two
// copies of the same geometry always do.
#include "screens.h"
#include "palette.h"
#include "font5x7.h"
#include "layout.h"
#include "nav.h"

namespace ui {
namespace {

void button(Canvas& c, int x, int y, const char* glyph, bool live) {
    const uint16_t edge = live ? pal::EDGE_LIT : pal::EDGE;
    c.fill(x, y, SET_BTN_W, SET_BTN_H, Canvas::mix(pal::PANEL, pal::VOID, 1, 3));
    c.rect(x, y, SET_BTN_W, SET_BTN_H, edge);
    c.textCenter(x + SET_BTN_W / 2, y + 1, glyph, live ? pal::WITCH_HI : pal::DIM);
}

} // namespace

const char* sweepName(uint8_t id) {
    switch (id) {
        case SW_GAZE:   return "GAZE";
        case SW_PULSE:  return "PULSE";
        case SW_STAR:   return "STAR";
        case SW_CANDLE: return "CANDLE";
        default:        return "RADAR";
    }
}

void drawAspect(Canvas& c, const State& s, Dirty* d) {
    c.clear(pal::VOID);
    drawHeader(c, s, "ASPECT");

    c.fill(1, BODY_Y, 158, BODY_H, pal::PANEL);
    c.rect(1, BODY_Y, 158, BODY_H, pal::EDGE);

    // Scale the ACTIVE theme's own radii down to the preview's bound, so the
    // proportions hold whichever theme is selected -- a fixed set of radii
    // fits one theme and mangles the other.
    //
    // FACE is a fixed bitmap and does not scale with them, so on BAPH0MAU5 the
    // preview crops the seal to its middle: horns and eyes, which is the part
    // that identifies it. That is a preview behaving like a window onto the
    // dial, not a defect.
    const int pf = R_FIELD * ASP_R / R_BOUND;
    const int pr = R_RUNES * ASP_R / R_BOUND;
    const int ps = R_STAR  * ASP_R / R_BOUND;
    drawDialAt(c, s, W / 2, ASP_CY, pf, pr, ps, ASP_R);

    for (int r = 0; r < ASP_ROWS; r++) {
        const int y = ASP_ROW_Y + r * ASP_ROW_H;
        if (r) c.hline(5, y - 2, 148, Canvas::mix(pal::PANEL, pal::EDGE, 1, 2));

        c.text(6, y + 2, r == 0 ? "THEME" : "SWEEP", pal::DIM);
        c.textRight(SET_MINUS_X - 4, y + 2,
                    r == 0 ? pal::themeName(s.set.theme) : sweepName(s.set.sweep),
                    pal::BRIGHT);
        button(c, SET_MINUS_X, y + 1, "-", true);
        button(c, SET_PLUS_X,  y + 1, "+", true);
    }

    drawFooter(c, s);
    drawReticle(c, s);
    c.vignette();
    c.scanlines();

    if (d) {
        static State prev;
        static bool  first = true;
        d->clear();
        if (first) {
            d->add(0, 0, W, H);
            first = false;
        } else if (prev.set.theme != s.set.theme) {
            // A theme change repaints everything: the dirty regions of the
            // old palette and geometry say nothing about the new ones.
            d->add(0, 0, W, H);
        } else {
            d->add(0, BODY_Y, W, BODY_H);   // the preview animates every frame
            dirtyChrome(*d, s, prev);
        }
        prev = s;
    }
}

} // namespace ui
```

- [ ] **Step 3: Add the grimoire tile**

In `ui/layout.h`, nothing changes — three columns already hold nine cells in three rows and only seven are used.

In `ui/screens.h`, change `constexpr int GRIMOIRE_N = 7;` to `constexpr int GRIMOIRE_N = 8;`.

In `ui/screen_grimoire.cpp`, add to the end of the `GRIMOIRE` table:

```cpp
    {SCR_ASPECT, 11, "ASPECT"},
```

Rune 11 is the crown and is not used by any of the other seven. "ASPECT" is six characters, which fits `GRIM_W` = 50 at 6px per character.

- [ ] **Step 4: Wire up navigation and the controls**

In `ui/nav.cpp`, add to `adjust()` a sibling function above `onTap`:

```cpp
// Step a theme or a sweep. The sweep steps to the next AVAILABLE id rather
// than clamping: stepping onto the gaze on UMBRAVOX and being bounced back
// reads as a broken control, not as an unavailable option.
void adjustAspect(Settings& st, int row, int dir) {
    if (row == 0) {
        const int v = (int)st.theme + dir;
        st.theme = (uint8_t)((v + pal::TH_COUNT) % pal::TH_COUNT);
        clampSettings(st);          // the new theme may not offer this sweep
        return;
    }
    const uint8_t mask = pal::sweepMask(st.theme);
    int v = st.sweep;
    for (int tries = 0; tries < SW_COUNT; tries++) {
        v = (v + dir + SW_COUNT) % SW_COUNT;
        if (mask & (1 << v)) { st.sweep = (uint8_t)v; return; }
    }
}
```

In `onTap`, add before the final `return false;`:

```cpp
    if (n.screen == SCR_ASPECT) {
        for (int r = 0; r < ASP_ROWS; r++) {
            const int y0 = ASP_ROW_Y + r * ASP_ROW_H;
            if (hit(x, y, SET_MINUS_X, y0 + 1, SET_BTN_W, SET_BTN_H)) adjustAspect(st, r, -1);
            if (hit(x, y, SET_PLUS_X,  y0 + 1, SET_BTN_W, SET_BTN_H)) adjustAspect(st, r, +1);
        }
    }
```

- [ ] **Step 5: Apply the theme when it changes**

The palette must follow the setting. In `sim/main_sim.cpp` this is the existing `setTheme` call. On the device, `device/main.cpp` must call `pal::setTheme(state.set.theme)` once per frame before drawing, and force a full-frame blit when it differs from the previous frame's value.

Add to `device/main.cpp`, immediately before the screen is drawn:

```cpp
    // The palette and the dial geometry are the theme. Apply it before
    // anything draws, and repaint everything when it changes -- the dirty
    // regions of the old theme say nothing about the new one.
    static uint8_t lastTheme = 0xFF;
    if (lastTheme != state.set.theme) {
        pal::setTheme(state.set.theme);
        lastTheme = state.set.theme;
        forceFullBlit = true;
    }
```

where `forceFullBlit` is the existing mechanism used after a screen change. If `device/main.cpp` names it differently, use that name — find it by searching for where a screen change causes a full repaint.

- [ ] **Step 6: Route the screen in the simulator and the device**

In `sim/main_sim.cpp`, add `aspect` alongside the other screens: a `const bool aspect = !strcmp(which, "aspect");`, include `SCR_ASPECT` in the `s.screen` selection, and `else if (aspect) ui::drawAspect(c, s);` in the draw selection.

In `device/main.cpp`, add `SCR_ASPECT` to the switch that dispatches to the draw functions, calling `ui::drawAspect`.

In `sim/Makefile` line 7, append ` ../ui/screen_aspect.cpp` to `SRC`.

- [ ] **Step 7: Verify**

Run: `cd sim && make clean && make -s && make -s regress && make -s test`
Expected: `regress: UMBRAVOX unchanged`, all five tests pass.

The grimoire now has eight tiles, so `grimoire.png` **will** differ from the baseline. That is the one expected regression failure in this plan. Confirm it is the only one, confirm the new tile is the cause, then re-render the grimoire baseline:

```bash
./umbravox ../private/baseline/grimoire.png --screen grimoire --frame 0 --scale 4
cd ../private/baseline && md5sum *.png > MANIFEST.md5
```

Re-run `make -s regress` and expect `UMBRAVOX unchanged`.

Run: `./umbravox /tmp/aspect.png --screen aspect --theme 1 --frame 26 --scale 4`

Open it. Expected: a header reading ASPECT, a live seal in the upper half, and two rows reading `THEME  -  BAPH0MAU5  +` and `SWEEP  -  RADAR  +`.

- [ ] **Step 8: Commit**

```bash
git add -A
git commit -m "feat: add the ASPECT screen and its grimoire tile"
```

---

### Task 10: Persistence

**Files:**
- Modify: `device/prefs.cpp`

**Interfaces:**
- Consumes: `ui::settingsEqual` from Task 3.
- Produces: theme and sweep surviving a power cycle.

- [ ] **Step 1: Delegate equality**

In `device/prefs.cpp`, delete the local `same()` function entirely and replace every call to `same(a, b)` with `ui::settingsEqual(a, b)`. The file already includes `"nav.h"`.

A local copy of this comparison is what made the original bug possible: the struct grew and the comparison did not. There is now one comparator, it lives beside the struct, and Task 3's test walks every byte of `Settings` to prove it is complete.

- [ ] **Step 2: Store the two new fields**

In `write()`, after the `idle` line:

```cpp
    nvs.putUChar("theme",   s.theme);
    nvs.putUChar("sweep",   s.sweep);
```

In `load()`, after the `in.idleIdx` line:

```cpp
    in.theme   = nvs.getUChar("theme",  s.theme);
    in.sweep   = nvs.getUChar("sweep",  s.sweep);
```

`clampSettings(in)` is already called immediately below and now covers both, including the case where the stored sweep is not available on the stored theme.

- [ ] **Step 3: Bump the store version**

Change `constexpr uint8_t VER = 3;` to `constexpr uint8_t VER = 4;`.

A v3 store has no theme field, so there is nothing to migrate; the existing behaviour for an unknown version — discard and write defaults — is correct.

- [ ] **Step 4: Extend the load log**

Change the `Serial.printf` at the end of `load()` to include the two new values:

```cpp
    Serial.printf("prefs: loaded bright %u%%, dwell %ums, ble %u%%, bands %u, alert %u, theme %u, sweep %u\n",
                  in.bright, in.dwellMs, in.bleDuty, in.bands, in.alertMin,
                  in.theme, in.sweep);
```

- [ ] **Step 5: Verify the device build**

Run: `pio run -e nm-cyd-c5`
Expected: compiles with no new warnings.

- [ ] **Step 6: Verify on hardware**

Run: `pio run -e nm-cyd-c5 -t upload && pio device monitor`

1. Open GRIMOIRE, tap ASPECT, change THEME to BAPH0MAU5 and SWEEP to GAZE.
2. Wait three seconds and confirm the monitor prints `prefs: saved`.
3. Power cycle the board.
4. Confirm the monitor prints `theme 1, sweep 1` and the device comes up on the seal with the gaze running.

If step 2 never prints, the comparator is not seeing the change — but Task 3's test should have caught that, so suspect the `write()`/`load()` key names instead.

- [ ] **Step 7: Measure the frame cost**

This is the open risk in the spec and the only number nobody has. The C5 is single core, so packet capture and drawing share one CPU and every redraw costs captures.

Using the existing timing instrumentation that produced the README's `draw 6 ms / blit 7 ms` figures, record draw time for:

- UMBRAVOX, radar — the baseline, expected ~6 ms
- BAPH0MAU5, radar — the seal blit, ~2,500 pixel tests against the rune ring's 400
- BAPH0MAU5, star — five tails rather than one

Record all three in the README's measurement block. If BAPH0MAU5 is materially worse than UMBRAVOX, the fallbacks in spec order are: cache the seal's static pixels, drop the star, reduce the face to 2bpp.

- [ ] **Step 8: Commit**

```bash
git add -A
git commit -m "feat: persist theme and sweep across a power cycle"
```

---

## Self-review

**Spec coverage.** Palette seam → Task 2. Extra theme colours → Task 2. Dial geometry → Task 4. Spectrum absence, draw and hit test → Task 4. Centrepiece branch → Task 6. Asset pipeline → Task 5. Tint regions → Task 6. Five sweeps and the per-mode contact test → Task 7. Gaze excluded on UMBRAVOX → Tasks 2 (mask) and 9 (stepping). ASPECT screen and live preview → Tasks 8 and 9. Settings and clamping → Task 3. Persistence and `VER` → Task 10. Verification table → Tasks 1, 2, 10. Working without version control → Task 1.

Deliberately not covered, per the spec's "Out of scope": the boot splash. The `BIG` asset is generated in Task 5 and goes unused until that work is picked up — a known, named loose end rather than an oversight.

**Review Focus coverage.** (1) comparator completeness → Task 3 Step 1, the byte-walk. (2) sweep not available on theme → Task 3 Step 1. (3) out-of-range ids → Task 3 Step 1 and `pal::row()`'s fallback. (4) stale pixels on theme change → Task 9 Steps 2 and 5. (5) spectrum hit test on a theme with no strip → Task 4 Step 4.
