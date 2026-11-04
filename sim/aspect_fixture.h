// Caller-supplied ASPECT scene fixtures for host rendering/tests.
#pragma once
#include "../ui/aspect_render.h"

namespace aspfix {

inline aspr::AspectScene demo(uint8_t theme, bool alertLive = true, int skipped = 0, uint32_t now = 5000) {
    aspr::AspectScene s;
    s.gps = { true, 3, 12, 500 };
    s.nodes = 3; s.hasCell = true; s.battPct = 78; s.rec = aspr::Rec::Off;
    s.themeName = "UMBRAVOX"; s.sweepName = "RADAR"; s.alertName = "WARD";
    s.alertLive = alertLive; s.packsSkipped = skipped;
    s.theme = theme; s.now = now;
    return s;
}

}  // namespace aspfix
