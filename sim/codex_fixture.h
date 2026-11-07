// Caller-supplied CODEX scene fixtures for host rendering/tests. The compositor
// carries no embedded data; this is where the demo rows live. Rows arrive
// already ORDERED + PAGINATED by the caller (decision #5: strongest first).
#pragma once
#include "../ui/codex_render.h"

namespace codfix {

// A full wide page of found devices, strongest signal first.
static const codr::CodexRow ROWS[3] = {
    { "w:aabbccddeeff", "NEST-CAM",   0, -48, 6,  true  },
    { "w:0011223344ff", "cafe-guest", 1, -63, 44, false },
    { "b:112233445566", "",           2, -80, 0,  false },  // hidden/BLE
};

// Up to MAX_SIGHTS keys for the hit-binding tests (row b binds key[b]).
static const char* const KEYS[3] = { "w:aabbccddeeff", "w:0011223344ff", "b:112233445566" };

inline codr::CodexScene demo(uint8_t theme, uint8_t filter = codr::F_WIFI, uint32_t now = 5000) {
    codr::CodexScene s;
    s.gps = { true, 3, 12, 500 };
    s.nodes = 3; s.hasCell = true; s.battPct = 78; s.rec = codr::Rec::Off;
    s.filter = filter;
    s.rows = ROWS; s.nRows = 3;
    s.pageLo = 1; s.pageHi = 3; s.listTotal = 14;
    s.theme = theme; s.now = now;
    return s;
}

}  // namespace codfix
