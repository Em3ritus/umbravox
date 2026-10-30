// echoes_render.h -- static ECHOES compositor + scene.
//
// ECHOES is probe requests: who is looking for what. The sought network is the
// finding (bright ink); the asker's address is dimmed when randomised (a
// session, not a device). Read-only (scroll, don't tap). Regions come from
// uieng::echoes(W,H) (the shared LogLayout). Reuses subr:: chrome. Host +
// device compilable (no Arduino deps).
#pragma once
#include "scan_render.h"
#include "sub_render.h"
#include "layout_engine.h"
#include "palette.h"
#include "font5x7.h"
#include "canvas.h"
#include <cstdint>
#include <cstdio>

namespace echr {

using scanr::Sink;
using scanr::GnssFix;
using scanr::Rec;

struct EchoRow {
    const char* seeking;   // SSID being probed for
    const char* who;       // asker's address tail, e.g. "AB:CD"
    bool        randomised;// locally-administered address -> dim
    uint32_t    agoS;
};

struct EchoScene {
    GnssFix  gps;
    int      nodes = 0;
    bool     hasCell = false; int battPct = 0;
    Rec      rec = Rec::Off;
    const EchoRow* rows = nullptr; int nRows = 0;
    int      total = 0;        // probes recorded (>= nRows)
    int      clients = 0;      // distinct askers
    uint8_t  theme = 0;
    uint32_t now = 0;
};

inline void agoStr(char* out, int n, uint32_t s) {
    if (s < 60)        snprintf(out, n, "%uS", (unsigned)s);
    else if (s < 3600) snprintf(out, n, "%uM", (unsigned)(s / 60));
    else               snprintf(out, n, "%uH", (unsigned)(s / 3600));
}

inline void composeStrip(scanr::S& s, int W, int H, const EchoScene& sc) {
    using scanr::box; using scanr::txt; using scanr::centered;
    const uieng::LogLayout L = uieng::echoes(W, H);
    subr::panels(s, W, H);
    subr::status(s, L.statusCells, sc.gps, sc.nodes, sc.hasCell, sc.battPct, sc.rec);

    txt(s, L.title.x, L.title.y + 2, "ECHOES", 2, pal::BRIGHT);
    { char tb[20];
      if (sc.clients) snprintf(tb, sizeof tb, "%d CLIENTS", sc.clients);
      else if (sc.total > sc.nRows) snprintf(tb, sizeof tb, "%d", sc.total);
      else tb[0] = 0;
      if (tb[0]) txt(s, L.title.x + L.title.w - Font::width(tb,1), L.title.y + 4, tb, 1, pal::DIM); }

    const int whoX = L.header.x + L.header.w - 60;
    txt(s, L.header.x + 4, L.header.y + 2, "SEEKING", 1, pal::EDGE_LIT);
    txt(s, whoX, L.header.y + 2, "WHO", 1, pal::EDGE_LIT);
    txt(s, L.header.x + L.header.w - Font::width("AGE",1), L.header.y + 2, "AGE", 1, pal::EDGE_LIT);

    box(s, L.list.x, L.list.y, L.list.w, L.list.h, ui::Canvas::mix(pal::VOID, pal::PANEL, 1, 3));
    if (sc.total == 0) {
        centered(s, L.list.x + L.list.w/2, L.list.y + L.list.h/2 - 6, "NO PROBES HEARD", 1, pal::DIM);
        centered(s, L.list.x + L.list.w/2, L.list.y + L.list.h/2 + 4, "CLIENTS ARE QUIET OR ABSENT", 1, pal::EDGE_LIT);
    } else {
        for (int i = 0; i < sc.nRows && i < L.nRows; i++) {
            const EchoRow& e = sc.rows[i];
            const int y = L.list.y + 2 + i * L.rowH;
            txt(s, L.list.x + 4, y + 1, e.seeking ? e.seeking : "", 1, pal::BRIGHT);
            const char* who = e.who ? e.who : "";
            txt(s, whoX, y + 1, who, 1, e.randomised ? pal::EDGE_LIT : pal::ICE);
            char ab[12]; agoStr(ab, sizeof ab, e.agoS);
            txt(s, L.list.x + L.list.w - 2 - Font::width(ab,1), y + 1, ab, 1, pal::DIM);
            if (i) box(s, L.list.x + 4, y, L.list.w - 8, 1, ui::Canvas::mix(pal::PANEL, pal::EDGE, 1, 2));
        }
    }
    subr::nav(s, L.navCells);
}

class Compositor {
public:
    static constexpr int MAXW = 480, STRIP_H = 16, BUF_BYTES = MAXW * STRIP_H * 2;
    void renderRows(int W, int H, const EchoScene& sc, int y0, int y1, Sink& sink) {
        if (y0 < 0) y0 = 0; if (y1 > H) y1 = H;
        for (int sy = y0; sy < y1; sy += STRIP_H) {
            const int sh = (sy + STRIP_H > y1) ? (y1 - sy) : STRIP_H;
            scanr::S s{ buf_, W, sy, sh }; composeStrip(s, W, H, sc); sink.strip(0, sy, W, sh, buf_);
        }
    }
    void renderFull(int W, int H, const EchoScene& sc, Sink& sink) { renderRows(W, H, sc, 0, H, sink); }
    void renderRegion(int W, int H, const EchoScene& sc, uieng::Rect r, Sink& sink) {
        renderRows(W, H, sc, (r.y / STRIP_H) * STRIP_H, r.y + r.h, sink);
    }
private:
    uint16_t buf_[MAXW * STRIP_H];
};

}  // namespace echr
