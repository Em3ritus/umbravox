// omens_render.h -- static OMENS compositor + scene.
//
// OMENS is the alert record, newest first: confidence, source, age, and whether
// the hit carries a position. Read-only (scroll, don't tap). Regions come from
// uieng::omens(W,H); painted as strips from scene state. Reuses the shared
// subr:: chrome. Host + device compilable (no Arduino deps).
#pragma once
#include "scan_render.h"
#include "sub_render.h"
#include "layout_engine.h"
#include "palette.h"
#include "font5x7.h"
#include "canvas.h"
#include <cstdint>
#include <cstdio>

namespace omnr {

using scanr::Sink;
using scanr::GnssFix;
using scanr::Rec;

struct OmenRow {
    const char* omen;      // threat name
    const char* source;    // device/network name ("" -> none)
    uint8_t     conf;      // 0 LOW, 1 MED, 2 HIGH
    uint32_t    agoS;
    bool        located;
};

struct OmenScene {
    GnssFix  gps;
    int      nodes = 0;
    bool     hasCell = false; int battPct = 0;
    Rec      rec = Rec::Off;
    const OmenRow* rows = nullptr; int nRows = 0;
    int      total = 0;                         // everything recorded (>= nRows)
    uint8_t  theme = 0;
    uint32_t now = 0;
};

inline uint16_t confColor(uint8_t c) { return c >= 2 ? pal::RED : c == 1 ? pal::AMBER : pal::WITCH; }
inline void agoStr(char* out, int n, uint32_t s) {
    if (s < 60)        snprintf(out, n, "%uS", (unsigned)s);
    else if (s < 3600) snprintf(out, n, "%uM", (unsigned)(s / 60));
    else               snprintf(out, n, "%uH", (unsigned)(s / 3600));
}

inline void composeStrip(scanr::S& s, int W, int H, const OmenScene& sc) {
    using scanr::box; using scanr::txt; using scanr::centered;
    const uieng::LogLayout L = uieng::omens(W, H);
    subr::panels(s, W, H);
    subr::status(s, L.statusCells, sc.gps, sc.nodes, sc.hasCell, sc.battPct, sc.rec);

    // title + total
    txt(s, L.title.x, L.title.y + 2, "OMENS", 2, pal::BRIGHT);
    if (sc.total > sc.nRows) { char tb[24]; snprintf(tb, sizeof tb, "%d", sc.total);
        txt(s, L.title.x + L.title.w - Font::width(tb,1), L.title.y + 4, tb, 1, pal::DIM); }

    // column header
    txt(s, L.header.x + 4,  L.header.y + 2, "OMEN", 1, pal::EDGE_LIT);
    txt(s, L.header.x + L.header.w/2, L.header.y + 2, "SOURCE", 1, pal::EDGE_LIT);
    txt(s, L.header.x + L.header.w - Font::width("AGE",1), L.header.y + 2, "AGE", 1, pal::EDGE_LIT);

    box(s, L.list.x, L.list.y, L.list.w, L.list.h, ui::Canvas::mix(pal::VOID, pal::PANEL, 1, 3));
    if (sc.total == 0) {
        centered(s, L.list.x + L.list.w/2, L.list.y + L.list.h/2 - 6, "NOTHING HAS FIRED", 1, pal::DIM);
        centered(s, L.list.x + L.list.w/2, L.list.y + L.list.h/2 + 4, "WHICH IS THE GOOD OUTCOME", 1, pal::EDGE_LIT);
    } else {
        const int srcX = L.list.x + L.list.w/2;
        for (int i = 0; i < sc.nRows && i < L.nRows; i++) {
            const OmenRow& o = sc.rows[i];
            const int y = L.list.y + 2 + i * L.rowH;
            const uint16_t cc = confColor(o.conf);
            box(s, L.list.x + 2, y + 1, 2, L.rowH - 4, cc);
            txt(s, L.list.x + 7, y + 1, o.omen ? o.omen : "", 1, cc);
            if (o.located) box(s, srcX - 8, y + 4, 2, 2, pal::GREEN);
            else           txt(s, srcX - 9, y + 1, "-", 1, pal::EDGE_LIT);
            txt(s, srcX, y + 1, o.source ? o.source : "", 1, pal::TEXT);
            char ab[12]; agoStr(ab, sizeof ab, o.agoS);
            txt(s, L.list.x + L.list.w - 2 - Font::width(ab,1), y + 1, ab, 1, pal::DIM);
            if (i) box(s, L.list.x + 4, y, L.list.w - 8, 1, ui::Canvas::mix(pal::PANEL, pal::EDGE, 1, 2));
        }
    }
    subr::nav(s, L.navCells);
}

class Compositor {
public:
    static constexpr int MAXW = 480, STRIP_H = 16, BUF_BYTES = MAXW * STRIP_H * 2;
    void renderRows(int W, int H, const OmenScene& sc, int y0, int y1, Sink& sink) {
        if (y0 < 0) y0 = 0; if (y1 > H) y1 = H;
        for (int sy = y0; sy < y1; sy += STRIP_H) {
            const int sh = (sy + STRIP_H > y1) ? (y1 - sy) : STRIP_H;
            scanr::S s{ buf_, W, sy, sh }; composeStrip(s, W, H, sc); sink.strip(0, sy, W, sh, buf_);
        }
    }
    void renderFull(int W, int H, const OmenScene& sc, Sink& sink) { renderRows(W, H, sc, 0, H, sink); }
    void renderRegion(int W, int H, const OmenScene& sc, uieng::Rect r, Sink& sink) {
        renderRows(W, H, sc, (r.y / STRIP_H) * STRIP_H, r.y + r.h, sink);
    }
private:
    uint16_t buf_[MAXW * STRIP_H];
};

}  // namespace omnr
