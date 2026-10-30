// Caller-supplied ATTUNE scene fixtures for host rendering/tests. The eight
// knobs come pre-formatted (label + value), as the caller projects them from
// Settings; demo() slices the current page for the given profile.
#pragma once
#include "../ui/attune_render.h"
#include "../ui/layout_engine.h"

namespace atnfix {

static const atnr::AttuneRow KNOBS[uieng::kAttuneKnobs] = {
    {"ALERT",  "MED"},     {"DWELL", "220MS (13S)"}, {"BLE",   "30%"},
    {"BANDS",  "BOTH"},    {"BRIGHT","80%"},         {"IDLE",  "30S DIM"},
    {"AUTOLOG","OFF"},     {"ORIENT","LANDSCAPE"},
};

inline atnr::AttuneScene demo(uint8_t theme, int W, int H, int page = 0, uint32_t now = 5000) {
    (void)W;   // rows-per-page depends only on body height
    atnr::AttuneScene s;
    s.gps = { true, 3, 12, 500 };
    s.nodes = 3; s.hasCell = true; s.battPct = 78; s.rec = atnr::Rec::Off;
    const int per = uieng::attuneRowsPerPage(H), pages = uieng::attunePages(H);
    if (page < 0) page = 0; else if (page >= pages) page = pages - 1;
    s.page = page; s.nPages = pages;
    static atnr::AttuneRow buf[4];
    int n = 0;
    for (int i = 0; i < per; i++) { const int k = page * per + i; if (k >= uieng::kAttuneKnobs) break; buf[n++] = KNOBS[k]; }
    s.rows = buf; s.nRows = n;
    s.theme = theme; s.now = now;
    return s;
}

}  // namespace atnfix
