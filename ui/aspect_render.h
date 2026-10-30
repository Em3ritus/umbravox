// aspect_render.h -- static ASPECT (display) compositor + scene.
//
// What the instrument looks like: a themed dial preview beside THEME / SWEEP /
// ALERT steppers. The preview reuses scanr::drawArtwork (the theme's emblem or
// heptagram) rather than a second copy of the dial. Regions come from
// uieng::aspect(W,H); the caller supplies each row's value pre-formatted (theme
// name, sweep name, alert name) and whether ALERT is actionable. Reuses subr::
// chrome + stepBtn. Host + device compilable (no Arduino deps).
#pragma once
#include "scan_render.h"
#include "sub_render.h"
#include "layout_engine.h"
#include "palette.h"
#include "font5x7.h"
#include "canvas.h"
#include <cstdint>
#include <cstdio>

namespace aspr {

using scanr::Sink;
using scanr::GnssFix;
using scanr::Rec;

struct AspectScene {
    GnssFix  gps;
    int      nodes = 0;
    bool     hasCell = false; int battPct = 0;
    bool pmuOk = false, charging = false;
    bool bridgeOn = false;
    Rec      rec = Rec::Off;
    const char* themeName = "";
    const char* sweepName = "";
    const char* alertName = "";
    bool     alertLive = false;     // ALERT row actionable (a theme with a choice)
    int      packsSkipped = 0;      // theme packs skipped at boot (status)
    uint8_t  theme = 0;             // drives the preview artwork
    uint32_t now = 0;
};

inline void composeStrip(scanr::S& s, int W, int H, const AspectScene& sc) {
    using scanr::box; using scanr::txt; using scanr::frame; using scanr::centered;
    const uieng::AspectLayout L = uieng::aspect(W, H);
    // shared design-1 chrome (header + nav, no tab lit) + STOP/REC record control
    scanr::drawChrome(s, W, H, L.body, {sc.gps, sc.nodes, sc.hasCell, sc.battPct, sc.rec, sc.theme, sc.bridgeOn, sc.pmuOk, sc.charging}, -1);

    // preview: the theme's own artwork, scaled into the preview box
    const uieng::Rect pv = L.preview;
    box(s, pv.x, pv.y, pv.w, pv.h, ui::Canvas::mix(pal::VOID, pal::PANEL, 1, 3));
    frame(s, pv.x, pv.y, pv.w, pv.h, pal::EDGE);
    const int pcx = pv.x + pv.w / 2, pcy = pv.y + pv.h / 2;
    int pr = (pv.w < pv.h ? pv.w : pv.h) / 2 - 8; if (pr < 0) pr = 0;
    if (pr > 6) scanr::drawArtwork(s, sc.theme, pcx, pcy, pr);
    if (sc.packsSkipped > 0) {
        char sk[16];
        if (sc.packsSkipped > 99) snprintf(sk, sizeof sk, "99+ SKIP");
        else snprintf(sk, sizeof sk, "%d SKIP", sc.packsSkipped);
        txt(s, pv.x + 4, pv.y + pv.h - 9, sk, 1, pal::AMBER);
    }

    // rows: THEME / SWEEP / ALERT
    static const char* const lab[3] = {"THEME", "SWEEP", "ALERT"};
    const char* val[3] = { sc.themeName, sc.sweepName, sc.alertName };
    for (int i = 0; i < 3; i++) {
        const uieng::Rect r = L.rows[i];
        const bool live = (i < 2) || sc.alertLive;
        if (i) box(s, r.x, r.y, r.w, 1, ui::Canvas::mix(pal::PANEL, pal::EDGE, 1, 2));
        txt(s, r.x + 2, r.y + 8, lab[i], 1, pal::DIM);
        const char* v = val[i] ? val[i] : "";
        txt(s, L.minus[i].x - 6 - Font::width(v, 1), r.y + 8, v, 1, live ? pal::BRIGHT : pal::DIM);
        subr::stepBtn(s, L.minus[i], "-", live);
        subr::stepBtn(s, L.plus[i],  "+", live);
    }

}

class Compositor {
public:
    static constexpr int MAXW = 480, STRIP_H = 16, BUF_BYTES = MAXW * STRIP_H * 2;
    void renderRows(int W, int H, const AspectScene& sc, int y0, int y1, Sink& sink) {
        if (y0 < 0) y0 = 0; if (y1 > H) y1 = H;
        for (int sy = y0; sy < y1; sy += STRIP_H) {
            const int sh = (sy + STRIP_H > y1) ? (y1 - sy) : STRIP_H;
            scanr::S s{ buf_, W, sy, sh }; composeStrip(s, W, H, sc); sink.strip(0, sy, W, sh, buf_);
        }
    }
    void renderFull(int W, int H, const AspectScene& sc, Sink& sink) { renderRows(W, H, sc, 0, H, sink); }
    void renderRegion(int W, int H, const AspectScene& sc, uieng::Rect r, Sink& sink) {
        renderRows(W, H, sc, (r.y / STRIP_H) * STRIP_H, r.y + r.h, sink);
    }
private:
    uint16_t buf_[MAXW * STRIP_H];
};

}  // namespace aspr
