// attune_render.h -- static ATTUNE (settings) compositor + scene.
//
// The knobs, paginated so every [-][+] stepper is a >=44px touch target.
// Regions come from uieng::attune(W,H); the caller supplies each visible knob
// already formatted to a label + value string (the Settings->text projection is
// UI logic the caller owns, exactly as CODEX carried pre-formatted rows), so
// the compositor stays arithmetic. Reuses subr:: chrome. Host + device
// compilable (no Arduino deps).
#pragma once
#include "scan_render.h"
#include "sub_render.h"
#include "layout_engine.h"
#include "palette.h"
#include "font5x7.h"
#include "canvas.h"
#include <cstdint>
#include <cstdio>

namespace atnr {

using scanr::Sink;
using scanr::GnssFix;
using scanr::Rec;

struct AttuneRow { const char* label; const char* value; };   // one visible knob

struct AttuneScene {
    GnssFix  gps;
    int      nodes = 0;
    bool     hasCell = false; int battPct = 0;
    Rec      rec = Rec::Off;
    int      page = 0, nPages = 1;
    const AttuneRow* rows = nullptr; int nRows = 0;            // this page's knobs
    uint8_t  theme = 0;
    uint32_t now = 0;
};

inline void stepBtn(scanr::S& s, const uieng::Rect& r, const char* g) {
    using scanr::box; using scanr::frame; using scanr::centered;
    box(s, r.x, r.y + 1, r.w, r.h - 2, ui::Canvas::mix(pal::PANEL, pal::VOID, 1, 3));
    frame(s, r.x, r.y + 1, r.w, r.h - 2, pal::EDGE_LIT);
    centered(s, r.x + r.w / 2, r.y + r.h / 2 - 5, g, 2, pal::WITCH_HI);
}

inline void composeStrip(scanr::S& s, int W, int H, const AttuneScene& sc) {
    using scanr::box; using scanr::txt; using scanr::frame; using scanr::centered; using scanr::fitScale;
    const uieng::AttuneLayout L = uieng::attune(W, H);
    subr::panels(s, W, H);
    subr::status(s, L.statusCells, sc.gps, sc.nodes, sc.hasCell, sc.battPct, sc.rec);

    // knob rows: label + value + [-][+]
    for (int i = 0; i < sc.nRows && i < L.nRows; i++) {
        const uieng::Rect r = L.rows[i];
        if (i) box(s, r.x + 4, r.y, r.w - 8, 1, ui::Canvas::mix(pal::PANEL, pal::EDGE, 1, 2));
        txt(s, r.x + 6, r.y + 8, sc.rows[i].label ? sc.rows[i].label : "", 1, pal::DIM);
        const char* v = sc.rows[i].value ? sc.rows[i].value : "";
        const int vmax = L.minus[i].x - (r.x + 6) - 6;
        txt(s, L.minus[i].x - 6 - Font::width(v, 1), r.y + 8, v, 1, pal::BRIGHT);
        (void)vmax;
        stepBtn(s, L.minus[i], "-");
        stepBtn(s, L.plus[i],  "+");
    }

    // footer: PAGE n/m + ATTUNE + CLEAR (CLEAR is the only red: it is destructive)
    char pg[32]; snprintf(pg, sizeof pg, "PG %d/%d", sc.page + 1, sc.nPages);
    box(s, L.pager.x, L.pager.y + 1, L.pager.w, 42, ui::Canvas::mix(pal::PANEL, pal::VOID, 1, 3));
    frame(s, L.pager.x, L.pager.y + 1, L.pager.w, 42, pal::EDGE_LIT);
    centered(s, L.pager.x + L.pager.w / 2, L.pager.y + 15, pg, fitScale(pg, L.pager.w - 6, 2), pal::WITCH_HI);
    centered(s, L.footer.x + L.footer.w / 2, L.footer.y + 16, "ATTUNE", 1, pal::DIM);
    box(s, L.clear.x, L.clear.y + 1, L.clear.w, 42, ui::Canvas::mix(pal::PANEL, pal::RED, 1, 6));
    frame(s, L.clear.x, L.clear.y + 1, L.clear.w, 42, pal::RED);
    centered(s, L.clear.x + L.clear.w / 2, L.clear.y + 15, "CLEAR", fitScale("CLEAR", L.clear.w - 6, 2), pal::RED);

    subr::nav(s, L.navCells);
}

class Compositor {
public:
    static constexpr int MAXW = 480, STRIP_H = 16, BUF_BYTES = MAXW * STRIP_H * 2;
    void renderRows(int W, int H, const AttuneScene& sc, int y0, int y1, Sink& sink) {
        if (y0 < 0) y0 = 0; if (y1 > H) y1 = H;
        for (int sy = y0; sy < y1; sy += STRIP_H) {
            const int sh = (sy + STRIP_H > y1) ? (y1 - sy) : STRIP_H;
            scanr::S s{ buf_, W, sy, sh }; composeStrip(s, W, H, sc); sink.strip(0, sy, W, sh, buf_);
        }
    }
    void renderFull(int W, int H, const AttuneScene& sc, Sink& sink) { renderRows(W, H, sc, 0, H, sink); }
    void renderRegion(int W, int H, const AttuneScene& sc, uieng::Rect r, Sink& sink) {
        renderRows(W, H, sc, (r.y / STRIP_H) * STRIP_H, r.y + r.h, sink);
    }
private:
    uint16_t buf_[MAXW * STRIP_H];
};

}  // namespace atnr
