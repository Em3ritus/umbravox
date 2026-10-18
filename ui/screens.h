#pragma once
#include "canvas.h"
#include "model.h"

namespace ui {

struct Rect { int16_t x, y, w, h; };

// What changed this frame, in canvas coordinates.
//
// This exists because of the blit, not the draw. Drawing the whole canvas
// costs ~6ms; pushing it over SPI costs ~35ms, and the bus is pinned at 40MHz
// by the GPIO matrix (SCK 6 / MOSI 7 are not IOMUX pins on this board), so the
// transfer cannot be made faster -- only smaller. On a single-core C5 that
// difference is CPU the radio does not get.
struct Dirty {
    // Scan's worst frame is dial + channel + header + two tile groups +
    // spectrum + footer + two reticle rects, which is already 9.
    static constexpr int MAX = 12;
    Rect r[MAX];
    int  n = 0;
    void clear() { n = 0; }
    void add(int16_t x, int16_t y, int16_t w, int16_t h) {
        if (n < MAX) r[n++] = Rect{x, y, w, h};
    }
};

// AETHERSCAN -- the running-capture screen.
//
// Always draws the whole canvas; `d` (when given) reports which parts of it
// actually changed so the caller can blit just those. Pass nullptr to force a
// full-frame blit, which is what the first frame after a mode change wants.
void drawScan(Canvas& c, const State& s, Dirty* d = nullptr);

// The dial, drawn anywhere at any size.
//
// ASPECT previews the dial while you choose a theme and a sweep for it, and
// the preview has to BE the dial rather than a picture of one -- the same
// reason ui/layout.h exists. drawScan() calls this with the active theme's
// geometry; ASPECT calls it with the preview's.
//
// What it does NOT do, so a second caller must: it draws no panel behind the
// dial, it does not clip to one, and it marks no dirty region -- drawScan's
// dirty rect is still derived from the DIAL_*/R_BOUND globals, not from these
// arguments.
void drawDialAt(Canvas& c, const State& s, int cx, int cy,
                int rField, int rRunes, int rStar, int rBound);

// ASPECT -- what the instrument looks like: the theme, and how the dial
// searches the field.
void drawAspect(Canvas& c, const State& s, Dirty* d = nullptr);

// Display name for a Sweep id.
const char* sweepName(uint8_t id);

// CODEX -- the register of what has been found: one row per network, with
// band, name, signal and channel. Scrolls.
void drawCodex(Canvas& c, const State& s, Dirty* d = nullptr);

// SIGHTING -- one device in full, with its signal history.
void drawDetail(Canvas& c, const State& s, Dirty* d = nullptr);

// ECHOES -- probe requests: clients naming networks they have joined.
void drawEchoes(Canvas& c, const State& s, Dirty* d = nullptr);

// GRIMOIRE -- the tile grid of every screen.
struct GrimoireEntry { uint8_t screen; uint8_t rune; const char* label; };
constexpr int GRIMOIRE_N = 8;
extern const GrimoireEntry GRIMOIRE[GRIMOIRE_N];
void grimoireCell(int i, int& x, int& y, int& w, int& h);
void drawGrimoire(Canvas& c, const State& s, Dirty* d = nullptr);

// OMENS -- the record of what has fired.
void drawOmens(Canvas& c, const State& s, Dirty* d = nullptr);

// DOMAIN -- the territory map: located devices and the track behind you.
void drawDomain(Canvas& c, const State& s, Dirty* d = nullptr);

// MESH -- the Core and its nodes: coverage, contribution, liveness.
void drawMesh(Canvas& c, const State& s, Dirty* d = nullptr);

// ATTUNE -- the settings knobs.
void drawAttune(Canvas& c, const State& s, Dirty* d = nullptr);

// Shared chrome, so the two screens cannot drift apart. Declared here because
// each screen draws its own -- there is no frame around them.
void drawHeader(Canvas& c, const State& s, const char* title);
void drawFooter(Canvas& c, const State& s);
void drawReticle(Canvas& c, const State& s);

// Dirty regions for everything the screens share: header, footer, reticle.
//
// This is shared rather than repeated because repeating it has now gone wrong
// three times -- static chrome never blitted, the reticle's old position left
// behind, and the header's band badge going stale while the radio hopped. The
// failure is always the same shape: something whose *content* changes without
// its *screen* changing, so nobody marks it. One copy, and a screen only has
// to describe its own body.
void dirtyChrome(Dirty& d, const State& s, const State& prev);

} // namespace ui
