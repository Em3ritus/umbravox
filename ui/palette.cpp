#include "palette.h"

namespace pal {

uint16_t VOID, PANEL, GRID, EDGE, EDGE_LIT;
uint16_t DIM, TEXT, BRIGHT;
uint16_t WITCH, WITCH_HI, BONE;
uint16_t SWEEP, SWEEP_HI;
uint16_t LIVE, LIVE_HI;
uint16_t RAMP[4];
uint16_t MAGENTA, ICE, GREEN;
uint16_t AMBER, RED;

namespace {

// One row per theme. A table rather than a switch: adding a theme is adding
// a row, and a row cannot silently omit a colour the way a hand-written
// block of assignments can.
struct Row {
    const char* name;
    uint8_t  sweeps;                 // bitmask over Sweep
    uint16_t void_, panel, grid, edge, edgeLit;
    uint16_t dim, text, bright;
    uint16_t witch, witchHi, bone;
    uint16_t sweep, sweepHi;
    uint16_t live, liveHi;
    uint16_t ramp[4];
    uint16_t magenta, ice, green;
    uint16_t amber, red;
};

constexpr Row THEMES[TH_COUNT] = {
    {   "AETHERSCAN",
        0b11101,                     // every sweep but the gaze
        rgb(0x07,0x04,0x0E), rgb(0x13,0x0B,0x22), rgb(0x18,0x0F,0x2C),
        rgb(0x2E,0x1F,0x52), rgb(0x56,0x36,0x97),
        rgb(0x6B,0x4F,0x9E), rgb(0xC2,0xAB,0xE4), rgb(0xF2,0xE8,0xFF),
        rgb(0x9D,0x7B,0xFF), rgb(0xD6,0xC2,0xFF), rgb(0xC9,0xB8,0x97),
        rgb(0x9D,0x7B,0xFF), rgb(0xD6,0xC2,0xFF),
        rgb(0xD6,0xC2,0xFF), rgb(0xF2,0xE8,0xFF),
        // No bitmap centrepiece here, but a working grey ramp rather than
        // four copies of VOID: a placeholder that renders nothing is the
        // kind that survives to a release.
        { rgb(0x07,0x04,0x0E), rgb(0x2E,0x1F,0x52),
          rgb(0x6B,0x4F,0x9E), rgb(0xF2,0xE8,0xFF) },
        rgb(0xD0,0x2B,0xB4), rgb(0x6F,0xD3,0xF5), rgb(0x4A,0xDE,0x80),
        rgb(0xF5,0x9E,0x0B), rgb(0xEF,0x44,0x44),
    },
    {   "BAPH0MAU5",
        0b11111,                     // all five
        rgb(0x06,0x03,0x0C), rgb(0x13,0x09,0x21), rgb(0x1D,0x11,0x33),
        rgb(0x3C,0x20,0x62), rgb(0x74,0x3C,0xB4),
        rgb(0x7E,0x56,0xBC), rgb(0xCA,0xB0,0xEA), rgb(0xFF,0xFA,0xFF),
        rgb(0x9B,0x62,0xF0), rgb(0xD6,0xBC,0xFF), rgb(0xE4,0xDA,0xF6),
        rgb(0x3B,0xFF,0x6E), rgb(0xC4,0xFF,0xD4),
        rgb(0x24,0xC4,0x50), rgb(0x3B,0xFF,0x6E),
        { rgb(0x06,0x03,0x0C), rgb(0x3E,0x30,0x55),
          rgb(0xB6,0xAE,0xCC), rgb(0xFF,0xFC,0xFF) },
        rgb(0x3B,0xFF,0x6E), rgb(0xF2,0xEE,0xFF), rgb(0x9B,0x62,0xF0),
        rgb(0xF5,0x9E,0x0B), rgb(0xD4,0x24,0x20),
    },
};

inline const Row& row(uint8_t id) {
    return THEMES[id < TH_COUNT ? id : (uint8_t)TH_AETHERSCAN];
}

} // namespace

void setTheme(uint8_t id) {
    const Row& t = row(id);
    VOID = t.void_;  PANEL = t.panel;  GRID = t.grid;
    EDGE = t.edge;   EDGE_LIT = t.edgeLit;
    DIM = t.dim;     TEXT = t.text;    BRIGHT = t.bright;
    WITCH = t.witch; WITCH_HI = t.witchHi; BONE = t.bone;
    SWEEP = t.sweep; SWEEP_HI = t.sweepHi;
    LIVE = t.live;   LIVE_HI = t.liveHi;
    for (int i = 0; i < 4; i++) RAMP[i] = t.ramp[i];
    MAGENTA = t.magenta; ICE = t.ice; GREEN = t.green;
    AMBER = t.amber; RED = t.red;
}

const char* themeName(uint8_t id) { return row(id).name; }
uint8_t     sweepMask(uint8_t id) { return row(id).sweeps; }

} // namespace pal
