// Caller-supplied FILES scene fixtures for host rendering/tests.
#pragma once
#include "../ui/files_render.h"
#include "../ui/palette.h"

namespace flsfix {

static const flsr::FileRowV ROWS[3] = {
    { "WD-20260127-153012", "1.2M",  true  },   // the active logging session
    { "WD-20260127-144500", "820K",  false },
    { "WD-20260126-2201",   "3.4M",  false },
};

// A normal list with row 1 selected (a deletable, non-active log).
inline flsr::FilesScene demo(uint8_t theme, uint32_t now = 5000) {
    flsr::FilesScene s;
    s.gps = { true, 3, 12, 500 };
    s.nodes = 3; s.hasCell = true; s.battPct = 78; s.rec = flsr::Rec::On;
    s.card = true; s.rows = ROWS; s.nRows = 3; s.total = 9; s.firstIndex = 0;
    s.sel = 1; s.deletable = true;
    s.status = "820K"; s.statusInk = pal::BRIGHT;
    s.theme = theme; s.now = now;
    return s;
}

// The confirm modal up for the selected log.
inline flsr::FilesScene confirm(uint8_t theme, uint32_t now = 5000) {
    flsr::FilesScene s = demo(theme, now);
    s.confirm = true; s.confirmName = "WD-20260127-144500"; s.confirmSize = "820K";
    return s;
}

}  // namespace flsfix
