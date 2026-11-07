// Caller-supplied MENU (hub) scene fixtures for host rendering/tests. The hub's
// tile set is fixed (menr::kMenuEntries); the scene is just chrome + label mode.
#pragma once
#include "../ui/menu_render.h"

namespace menfix {

inline menr::MenuScene demo(uint8_t theme, bool plain = false, uint32_t now = 5000) {
    menr::MenuScene s;
    s.gps = { true, 3, 12, 500 };
    s.nodes = 3; s.hasCell = true; s.battPct = 78; s.rec = menr::Rec::Off;
    s.plain = plain;
    s.theme = theme; s.now = now;
    return s;
}

}  // namespace menfix
