// sub_render.h -- shared chrome drawing for the deeper MENU sub-screens.
//
// MESH / OMENS / ECHOES / ATTUNE / ASPECT all carry the same status bar (the
// single-sourced GPS/rec projections) and the same bottom nav bar with NO tab
// lit (a sub-screen is not a nav tab; tapping any tab navigates). Factored here
// so the five screens share one copy rather than each re-drawing it. Reuses the
// scanr:: strip primitives. Host + device compilable (no Arduino deps).
#pragma once
#include "scan_render.h"
#include "layout_engine.h"
#include "palette.h"
#include "font5x7.h"
#include "canvas.h"
#include <cstdio>

namespace subr {

// The two surfaces + dividers every engine screen shares.
inline void panels(scanr::S& s, int W, int H) {
    using scanr::box;
    box(s, 0, 0, W, H, pal::VOID);
    box(s, 0, 0, W, 44, pal::PANEL);      box(s, 0, 43, W, 1, pal::EDGE);
    box(s, 0, H - 44, W, 44, pal::PANEL); box(s, 0, H - 44, W, 1, pal::EDGE);
}

// Status bar: quiet label over an emphasised value, from scene chrome state.
// GPS/rec go through scanr::gpsLabel/recLabel, so freshness/age/mode and the
// recording label are identical to every other screen.
inline void status(scanr::S& s, const uieng::Rect sc[4], const scanr::GnssFix& gps,
                   int nodes, bool hasCell, int batt, scanr::Rec rec) {
    using scanr::box; using scanr::txt; using scanr::fitScale;
    static const char* const slab[4] = {"GPS", "MESH", "BATT", "LOG"};
    char gbuf[16], rbuf[16], mbuf[16], bbuf[16];
    const uint16_t gcol = scanr::gpsLabel(gps, gbuf, sizeof gbuf);
    scanr::ScanScene tmp; tmp.rec = rec;
    const uint16_t rcol = scanr::recLabel(tmp, rbuf, sizeof rbuf);
    snprintf(mbuf, sizeof mbuf, "%d NODES", nodes);
    if (hasCell) snprintf(bbuf, sizeof bbuf, "%d%%", batt); else snprintf(bbuf, sizeof bbuf, "N/A");
    const char* sval[4] = { gbuf, mbuf, bbuf, rbuf };
    const uint16_t scol[4] = { gcol, pal::TEXT, hasCell ? pal::TEXT : pal::DIM, rcol };
    for (int i = 0; i < 4; i++) { const uieng::Rect c = sc[i];
        if (i) box(s, c.x, c.y + 9, 1, 26, pal::EDGE);
        txt(s, c.x + 8, c.y + 7, slab[i], 1, pal::DIM);
        txt(s, c.x + 8, c.y + 19, sval[i], fitScale(sval[i], c.w - 12, 2), scol[i]); }
}

// A framed [-]/[+] stepper button (a >=44px touch target), live or dimmed.
inline void stepBtn(scanr::S& s, const uieng::Rect& r, const char* glyph, bool live) {
    using scanr::box; using scanr::frame; using scanr::centered;
    box(s, r.x, r.y + 1, r.w, r.h - 2, ui::Canvas::mix(pal::PANEL, pal::VOID, 1, 3));
    frame(s, r.x, r.y + 1, r.w, r.h - 2, live ? pal::EDGE_LIT : pal::EDGE);
    centered(s, r.x + r.w / 2, r.y + r.h / 2 - 5, glyph, 2, live ? pal::WITCH_HI : pal::DIM);
}

// Bottom nav bar, no tab lit (the sub-screen is reached via MENU, not a tab).
inline void nav(scanr::S& s, const uieng::Rect nv[4]) {
    using scanr::box; using scanr::centered; using scanr::fitScale;
    static const char* const lab[4] = {"SCAN", "DOMAIN", "CODEX", "MENU"};
    for (int i = 0; i < 4; i++) { const uieng::Rect c = nv[i];
        if (i) box(s, c.x, c.y + 11, 1, 22, pal::EDGE);
        centered(s, c.x + c.w / 2, c.y + 14, lab[i], fitScale(lab[i], c.w - 8, 2), pal::DIM); }
}

}  // namespace subr
