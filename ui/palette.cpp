#include "palette.h"
#include "layout.h"

namespace ui {
int DIAL_Y, DIAL_H, DIAL_CX, DIAL_CY;
int R_FIELD, R_RUNES, R_BOUND, R_STAR;
int SPEC_Y, SPEC_H;
} // namespace ui

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
    uint16_t sweeps;                 // bitmask over Sweep (>8 sweeps, so 16-bit)
    int16_t dialY, dialH, dialCx, dialCy;
    int16_t rField, rRunes, rBound, rStar;
    int16_t specY, specH;
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
    {   "UMBRAVOX",
        0b11111111101,               // every sweep but the gaze (incl. cosmic)
        13, 70, 109, 48,   22, 26, 31, 20,   85, 16,
        rgb(0x07,0x04,0x0E), rgb(0x13,0x0B,0x22), rgb(0x18,0x0F,0x2C),
        rgb(0x2E,0x1F,0x52), rgb(0x56,0x36,0x97),
        rgb(0x6B,0x4F,0x9E), rgb(0xC2,0xAB,0xE4), rgb(0xF2,0xE8,0xFF),
        rgb(0x9D,0x7B,0xFF), rgb(0xD6,0xC2,0xFF), rgb(0xC9,0xB8,0x97),
        rgb(0x9D,0x7B,0xFF), rgb(0xD6,0xC2,0xFF),
        rgb(0xD6,0xC2,0xFF), rgb(0xF2,0xE8,0xFF),
        // No bitmap centrepiece here, but a working ramp from the theme's own
        // darkest to brightest (VOID, EDGE, DIM, BRIGHT) rather than a
        // placeholder that renders nothing -- that kind survives to a release.
        { rgb(0x07,0x04,0x0E), rgb(0x2E,0x1F,0x52),
          rgb(0x6B,0x4F,0x9E), rgb(0xF2,0xE8,0xFF) },
        rgb(0xD0,0x2B,0xB4), rgb(0x6F,0xD3,0xF5), rgb(0x4A,0xDE,0x80),
        rgb(0xF5,0x9E,0x0B), rgb(0xEF,0x44,0x44),
    },
    {   "BAPH0MAU5",
        0b11111111111,               // all sweeps, cosmic + gaze
        13, 90, 109, 57,   33, 37, 42, 31,    0,  0,
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
    {   "COSMIC MUFFIN",
        0b11111111101,               // every sweep but the gaze -- no eyes (0x7FD)
        13, 90, 109, 57,   33, 37, 42, 31,    0,  0,
        rgb(0x09,0x07,0x1C), rgb(0x16,0x10,0x3A), rgb(0x1E,0x16,0x46),
        rgb(0x3A,0x2A,0x6E), rgb(0xA8,0x4D,0xFF),
        rgb(0x8A,0x7B,0xD0), rgb(0xE8,0xDE,0xFF), rgb(0xFF,0xFF,0xFF),
        rgb(0xA8,0x4D,0xFF), rgb(0xFF,0x1F,0xCC), rgb(0xFF,0xD0,0x4A),
        rgb(0x1E,0xF5,0xEE), rgb(0xFF,0x1F,0xCC),
        rgb(0xFF,0x58,0xB0), rgb(0xFF,0xF0,0xD0),
        { rgb(0x09,0x07,0x1C), rgb(0x3A,0x2A,0x6E),
          rgb(0xA8,0x4D,0xFF), rgb(0xFF,0xF0,0xE8) },
        rgb(0xFF,0x1F,0xCC), rgb(0x1E,0xF5,0xEE), rgb(0x3B,0xFF,0x9E),
        rgb(0xFF,0xC2,0x4A), rgb(0xFF,0x4D,0x6A),
    },
};

inline const Row& row(uint8_t id) {
    return THEMES[id < TH_COUNT ? id : (uint8_t)TH_UMBRAVOX];
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

    ui::DIAL_Y  = t.dialY;  ui::DIAL_H  = t.dialH;
    ui::DIAL_CX = t.dialCx; ui::DIAL_CY = t.dialCy;
    ui::R_FIELD = t.rField; ui::R_RUNES = t.rRunes;
    ui::R_BOUND = t.rBound; ui::R_STAR  = t.rStar;
    ui::SPEC_Y  = t.specY;  ui::SPEC_H  = t.specH;
}

// The globals are zero-initialised, which is black on black. Nothing should
// ever read a colour before setTheme() runs -- but "should" is doing a lot of
// work there, and the failure mode is a dead screen with no other symptom.
// Pointing the palette at UMBRAVOX during static init makes the question
// moot rather than leaving it to call order.
//
// Safe against static-initialisation order: THEMES is constexpr and so is
// constant-initialised before any dynamic initialisation runs, and nothing
// anywhere reads a pal:: value during static init -- every use is a local
// inside a function.
namespace {
struct DefaultTheme {
    DefaultTheme() { setTheme(TH_UMBRAVOX); }
};
const DefaultTheme _defaultTheme;
} // namespace

const char* themeName(uint8_t id) { return row(id).name; }
uint16_t    sweepMask(uint8_t id) { return row(id).sweeps; }

} // namespace pal
