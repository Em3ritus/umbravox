// Caller-supplied MESH scene fixtures for host rendering/tests.
#pragma once
#include "../ui/mesh_render.h"

namespace mshfix {

static const mshr::MeshNode NODES[4] = {
    { {0x10,0xAA,0x01},  3,  0, true  },
    { {0x10,0xBB,0x02}, 14,  7, true  },
    { {0x10,0xCC,0x03}, 40, 33, false },
    { {0x10,0xDD,0x04},  8,  2, true  },
};

inline mshr::MeshScene demo(uint8_t theme, bool empty = false, uint32_t now = 5000) {
    mshr::MeshScene s;
    s.gps = { true, 3, 12, 500 };
    s.nodes = 3; s.hasCell = true; s.battPct = 78; s.rec = mshr::Rec::Off;
    s.meshCoords = 0; s.coreCamping = true; s.coreChan = 6; s.meshLossPct = 7;
    s.netId = 2; s.enrollSecs = 0;
    if (!empty) { s.rows = NODES; s.nRows = 4; }
    s.theme = theme; s.now = now;
    return s;
}

}  // namespace mshfix
