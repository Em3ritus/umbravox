// Caller-supplied OMENS scene fixtures for host rendering/tests.
#pragma once
#include "../ui/omens_render.h"

namespace omnfix {

static const omnr::OmenRow ROWS[3] = {
    { "FLOCK-CAM",  "camera-7",  2,   42, true  },
    { "AIRTAG",     "",          1,  300, false },
    { "IMSI-WATCH", "unknown",   0, 7200, false },
};

inline omnr::OmenScene demo(uint8_t theme, bool empty = false, uint32_t now = 5000) {
    omnr::OmenScene s;
    s.gps = { true, 3, 12, 500 };
    s.nodes = 3; s.hasCell = true; s.battPct = 78; s.rec = omnr::Rec::Off;
    if (!empty) { s.rows = ROWS; s.nRows = 3; s.total = 9; }
    s.theme = theme; s.now = now;
    return s;
}

}  // namespace omnfix
