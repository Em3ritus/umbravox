// Caller-supplied ECHOES scene fixtures for host rendering/tests.
#pragma once
#include "../ui/echoes_render.h"

namespace echfix {

static const echr::EchoRow ROWS[3] = {
    { "HOTEL-WIFI",   "AB:CD", false,   12 },
    { "home-network", "11:22", true,   240 },
    { "CoffeeHouse",  "9F:0A", false, 3600 },
};

inline echr::EchoScene demo(uint8_t theme, bool empty = false, uint32_t now = 5000) {
    echr::EchoScene s;
    s.gps = { true, 3, 12, 500 };
    s.nodes = 3; s.hasCell = true; s.battPct = 78; s.rec = echr::Rec::Off;
    if (!empty) { s.rows = ROWS; s.nRows = 3; s.total = 11; s.clients = 4; }
    s.theme = theme; s.now = now;
    return s;
}

}  // namespace echfix
