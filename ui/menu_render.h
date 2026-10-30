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
    box(s, 0, 0, W, 44, pal::PANEL);   box(s, 0, 43, W, 1, pal::EDGE);
    box(s, 0, H - 44, W, 44, pal::PANEL); box(s, 0, H - 44, W, 1, pal::EDGE);

    // status bar (same look + projections as the other screens)
    static const char* const slab[4] = {"GPS", "MESH", "BATT", "LOG"};
    char gbuf[16], rbuf[16], mbuf[16], bbuf[16];
    const uint16_t gcol = scanr::gpsLabel(sc.gps, gbuf, sizeof gbuf);
    scanr::ScanScene tmp; tmp.rec = sc.rec;
    const uint16_t rcol = scanr::recLabel(tmp, rbuf, sizeof rbuf);
    snprintf(mbuf, sizeof mbuf, "%d NODES", sc.nodes);
    if (sc.hasCell) snprintf(bbuf, sizeof bbuf, "%d%%", sc.battPct); else snprintf(bbuf, sizeof bbuf, "N/A");
    const char* sval[4] = { gbuf, mbuf, bbuf, rbuf };
    const uint16_t scol[4] = { gcol, pal::TEXT, sc.hasCell ? pal::TEXT : pal::DIM, rcol };
    for (int i = 0; i < 4; i++) { const uieng::Rect c = L.statusCells[i];
        if (i) box(s, c.x, c.y + 9, 1, 26, pal::EDGE);
        txt(s, c.x + 8, c.y + 7, slab[i], 1, pal::DIM);
        txt(s, c.x + 8, c.y + 19, sval[i], fitScale(sval[i], c.w - 12, 2), scol[i]); }

    // the grid: eight launcher tiles + a LABELS toggle
    for (int i = 0; i < 8; i++) { const uieng::Rect t = L.cells[i];
        box(s, t.x, t.y, t.w, t.h, ui::Canvas::mix(pal::VOID, pal::PANEL, 2, 3));
        frame(s, t.x, t.y, t.w, t.h, pal::EDGE);
        box(s, t.x, t.y, t.w, 3, pal::WITCH);                 // top accent
        const char* lbl = sc.plain ? kMenuEntries[i].plain : kMenuEntries[i].brand;
        centered(s, t.x + t.w / 2, t.y + t.h / 2 - 3, lbl, fitScale(lbl, t.w - 10, 2), pal::TEXT); }

    // LABELS control tile (ninth cell): a live edge marks it as a control
    { const uieng::Rect t = L.cells[8];
      box(s, t.x, t.y, t.w, t.h, ui::Canvas::mix(pal::VOID, pal::PANEL, 2, 3));
      frame(s, t.x, t.y, t.w, t.h, pal::WITCH);
      centered(s, t.x + t.w / 2, t.y + 6, "LABELS", fitScale("LABELS", t.w - 10, 2), pal::WITCH_HI);
      const char* mode = sc.plain ? "PLAIN" : "BRAND";
      centered(s, t.x + t.w / 2, t.y + t.h - 14, mode, fitScale(mode, t.w - 10, 2), sc.plain ? pal::GREEN : pal::DIM); }

    // nav bar (MENU is index 3)
    static const char* const nav[4] = {"SCAN", "DOMAIN", "CODEX", "MENU"};
    for (int i = 0; i < 4; i++) { const uieng::Rect c = L.navCells[i];
        if (i) box(s, c.x, c.y + 11, 1, 22, pal::EDGE);
        const int ns = fitScale(nav[i], c.w - 8, 2);
        centered(s, c.x + c.w / 2, c.y + 14, nav[i], ns, i == 3 ? pal::BRIGHT : pal::DIM);
        if (i == 3) { const int lw = Font::width(nav[i], ns); box(s, c.x + c.w / 2 - lw / 2, H - 8, lw, 2, pal::WITCH_HI); } }
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
