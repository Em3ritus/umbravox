// menu_render.h -- static MENU (hub) compositor + scene.
//
// MENU is the hub (the old GRIMOIRE): a 3x3 grid of launcher tiles over the
// shared chrome. Eight tiles open screens; the ninth toggles brand/plain
// labels. Regions come from uieng::menu(W,H); the screen is painted as strips
// composed completely from scene state, same strategy as the other engine
// screens. Reuses scanr:: primitives + gpsLabel/recLabel. 5x7 plumbing (runes
// and final typography are the separate visual-fidelity milestone). Host +
// device compilable (no Arduino deps).
#pragma once
#include "scan_render.h"      // scanr:: primitives + GnssFix/gpsLabel/recLabel/Sink/S
#include "layout_engine.h"
#include "palette.h"
#include "font5x7.h"
#include "canvas.h"
#include <cstdint>
#include <cstdio>

namespace menr {

using scanr::Sink;
using scanr::GnssFix;
using scanr::Rec;

// The hub's launcher tiles, in grid order (matches uieng::menuHits). Each
// carries both label modes; the LABELS toggle is tile 8, handled separately.
struct MenuEntry { const char* brand; const char* plain; };
static const MenuEntry kMenuEntries[8] = {
    {"SCAN",   "SCAN"},     {"CODEX",  "FOUND"},
    {"DOMAIN", "MAP"},      {"ECHOES", "PROBES"},
    {"MESH",   "MESH"},     {"OMENS",  "ALERTS"},
    {"ATTUNE", "SETTINGS"}, {"ASPECT", "DISPLAY"},
};

struct MenuScene {
    // status chrome (projected by scanr::gpsLabel / recLabel)
    GnssFix  gps;
    int      nodes = 0;
    bool     hasCell = false; int battPct = 0;
    Rec      rec = Rec::Off;
    // hub state
    bool     plain = false;      // label mode: false = on-brand, true = plain
    uint8_t  theme = 0;
    uint32_t now = 0;
};

// --- compose one strip completely from the scene -----------------------------
inline void composeStrip(scanr::S& s, int W, int H, const MenuScene& sc) {
    using scanr::box; using scanr::txt; using scanr::frame; using scanr::centered; using scanr::fitScale;
    const uieng::MenuLayout L = uieng::menu(W, H);

    box(s, 0, 0, W, H, pal::VOID);
    // shared design-1 chrome: header + nav (MENU lit, index 3) + STOP/REC control
    scanr::drawChrome(s, W, H, L.body, {sc.gps, sc.nodes, sc.hasCell, sc.battPct, sc.rec}, 3);

    // the grid: eight launcher tiles + a LABELS toggle. Design #1 is muted
    // chrome -- the old WITCH (bright violet) tile accents clashed on hardware,
    // so the per-tile top bar is EDGE (a quiet member of the chrome family).
    for (int i = 0; i < 8; i++) { const uieng::Rect t = L.cells[i];
        box(s, t.x, t.y, t.w, t.h, ui::Canvas::mix(pal::VOID, pal::PANEL, 2, 3));
        frame(s, t.x, t.y, t.w, t.h, pal::EDGE);
        box(s, t.x, t.y, t.w, 3, pal::EDGE_LIT);             // top accent (design-1 seal tone)
        const char* lbl = sc.plain ? kMenuEntries[i].plain : kMenuEntries[i].brand;
        const int scale = fitScale(lbl, t.w - 10, 2);
        centered(s, t.x + t.w / 2, t.y + (t.h - 7*scale)/2, lbl, scale, pal::TEXT); }

    // LABELS control tile (ninth cell): the lit EDGE_LIT edge (design-1's live-
    // control edge, per subr::stepBtn) marks it as a control, not a launcher --
    // no WITCH purple on the hub.
    { const uieng::Rect t = L.cells[8];
      box(s, t.x, t.y, t.w, t.h, ui::Canvas::mix(pal::VOID, pal::PANEL, 2, 3));
      frame(s, t.x, t.y, t.w, t.h, pal::EDGE_LIT);
      const char* mode = sc.plain ? "PLAIN" : "BRAND";
      const int scale = fitScale("LABELS", t.w - 10, 2), gap = 5;
      const int y = t.y + (t.h - 14*scale - gap)/2;
      centered(s, t.x + t.w / 2, y, "LABELS", scale, pal::BRIGHT);
      centered(s, t.x + t.w / 2, y + 7*scale + gap, mode, scale, sc.plain ? pal::GREEN : pal::DIM); }

    // nav bar + STOP/REC drawn by scanr::drawChrome above (shared design-1 chrome).
}

// --- strip compositor: small buffer, compose-then-emit (mirrors the others) ---
class Compositor {
public:
    static constexpr int MAXW    = 480;
    static constexpr int STRIP_H = 16;
    static constexpr int BUF_BYTES = MAXW * STRIP_H * 2;

    void renderRows(int W, int H, const MenuScene& sc, int y0, int y1, Sink& sink) {
        if (y0 < 0) y0 = 0; if (y1 > H) y1 = H;
        for (int sy = y0; sy < y1; sy += STRIP_H) {
            const int sh = (sy + STRIP_H > y1) ? (y1 - sy) : STRIP_H;
            scanr::S s{ buf_, W, sy, sh };
            composeStrip(s, W, H, sc);
            sink.strip(0, sy, W, sh, buf_);
        }
    }
    void renderFull(int W, int H, const MenuScene& sc, Sink& sink) { renderRows(W, H, sc, 0, H, sink); }
    void renderRegion(int W, int H, const MenuScene& sc, uieng::Rect r, Sink& sink) {
        renderRows(W, H, sc, (r.y / STRIP_H) * STRIP_H, r.y + r.h, sink);
    }
private:
    uint16_t buf_[MAXW * STRIP_H];
};

}  // namespace menr
