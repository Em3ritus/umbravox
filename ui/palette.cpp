#include "palette.h"
#include "layout.h"
#include "model.h"
#include "muffinseal.h"
#include "themepack.h"
#include <string.h>

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
    const char* header;              // the scan masthead
    Centrepiece centre;
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
    {   "UMBRAVOX", "UMBRAVOX", CP_HEPTAGRAM,
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
    {   "BAPH0MAU5", "CVLT OF BAPH0MAU5", CP_SEAL,
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
    {   "COSMIC MUFFIN", "COSMIC MVFFIN", CP_EMBLEM,
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

// Theme packs, appended after the built-ins at ids TH_COUNT + slot. Static
// storage: a pack Row's name and header point into PACKS, so they live as long
// as the program. Zero-initialised and filled only by addPack, at boot.
themepack::Pack           PACKS[themepack::MAX_PACKS];
Row                       PACK_ROWS[themepack::MAX_PACKS];
const themepack::Emblem*  PACK_EMBLEM[themepack::MAX_PACKS];
int                       nPacks = 0;

// The slot for a pack theme id, or -1 for a built-in or an id past the packs.
inline int slotOf(uint8_t id) {
    const int s = (int)id - (int)TH_COUNT;
    return (s >= 0 && s < nPacks) ? s : -1;
}

inline const Row& row(uint8_t id) {
    if (id < TH_COUNT) return THEMES[id];
    const int s = slotOf(id);
    return s >= 0 ? PACK_ROWS[s] : THEMES[TH_UMBRAVOX];
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

// --- theme packs ----------------------------------------------------------

int themeCount() { return TH_COUNT + nPacks; }

int addPack(const themepack::Pack& in, const themepack::Emblem* e) {
    if (nPacks >= themepack::MAX_PACKS) return -1;
    const int slot = nPacks;
    themepack::Pack& p = PACKS[slot];
    p = in;

    // The parser already guarantees all of this; the table does not rely on
    // it, because a caller that hand-builds a Pack (the sim, a test) skips it.
    p.name[themepack::MAX_NAME] = 0;
    p.author[themepack::MAX_AUTHOR] = 0;
    p.header[themepack::MAX_HEADER] = 0;
    if (!p.header[0]) {                     // default the masthead to the name
        memcpy(p.header, p.name, themepack::MAX_HEADER);
        p.header[themepack::MAX_HEADER] = 0;
    }
    if (p.dial != themepack::DIAL_LARGE) p.dial = themepack::DIAL_COMPACT;
    if (p.centre != themepack::CENTRE_EMBLEM) p.centre = themepack::CENTRE_HEPTAGRAM;
    if ((unsigned)p.rite > (unsigned)themepack::RITE_LOCKON) p.rite = themepack::RITE_WARD;
    // The starting sweep must be one the pack offers, or clampSettings could
    // fall back to a sweep that is itself withdrawn.
    p.sweeps &= (uint16_t)((1u << ui::SW_COUNT) - 1);
    if (!p.sweeps) p.sweeps = (uint16_t)(1u << ui::SW_RADAR);
    if (p.sweep >= ui::SW_COUNT || !(p.sweeps & (1u << p.sweep))) {
        p.sweep = 0;
        while (!(p.sweeps & (1u << p.sweep))) p.sweep++;
    }
    // amber/red are status-only: a pack cannot make them vanish.
    themepack::sanitizeStatus(p);

    using namespace themepack;
    Row r = THEMES[p.dial == DIAL_LARGE ? TH_COSMICMUFFIN : TH_UMBRAVOX];   // geometry
    r.name = p.name;
    r.header = p.header;
    r.centre = p.centre == CENTRE_EMBLEM ? CP_EMBLEM : CP_HEPTAGRAM;
    r.sweeps = p.sweeps;
    const uint16_t* c = p.color;
    r.void_ = c[R_VOID];   r.panel = c[R_PANEL];     r.grid = c[R_GRID];
    r.edge = c[R_EDGE];    r.edgeLit = c[R_EDGELIT];
    r.dim = c[R_DIM];      r.text = c[R_TEXT];       r.bright = c[R_BRIGHT];
    r.witch = c[R_WITCH];  r.witchHi = c[R_WITCHHI]; r.bone = c[R_BONE];
    r.sweep = c[R_SWEEP];  r.sweepHi = c[R_SWEEPHI];
    r.live = c[R_LIVE];    r.liveHi = c[R_LIVEHI];
    // The same ramp UMBRAVOX uses: the theme's own darkest to brightest.
    r.ramp[0] = c[R_VOID]; r.ramp[1] = c[R_EDGE]; r.ramp[2] = c[R_DIM]; r.ramp[3] = c[R_BRIGHT];
    r.magenta = c[R_MAGENTA]; r.ice = c[R_ICE];      r.green = c[R_GREEN];
    r.amber = c[R_AMBER];  r.red = c[R_RED];
    PACK_ROWS[slot] = r;
    PACK_EMBLEM[slot] = e;
    nPacks++;
    return TH_COUNT + slot;
}

int findPack(const char* name) {
    if (!name || !name[0]) return -1;
    for (int s = 0; s < nPacks; s++)
        if (!strcmp(PACKS[s].name, name)) return TH_COUNT + s;
    return -1;
}

uint8_t resolveSaved(const char* name, uint8_t savedId) {
    if (name && name[0]) {
        const int id = findPack(name);
        return id >= 0 ? (uint8_t)id : (uint8_t)TH_UMBRAVOX;
    }
    return savedId < TH_COUNT ? savedId : (uint8_t)TH_UMBRAVOX;
}

Centrepiece centrepiece(uint8_t id) { return row(id).centre; }
const char* header(uint8_t id)      { return row(id).header; }

const themepack::Emblem* emblem(uint8_t id) {
    if (id == TH_COSMICMUFFIN) return &ui::muffinEmblem();
    const int s = slotOf(id);
    return s >= 0 ? PACK_EMBLEM[s] : nullptr;
}

uint8_t startSweep(uint8_t id) {
    const int s = slotOf(id);
    return s >= 0 ? PACKS[s].sweep : (uint8_t)ui::SW_RADAR;
}

uint8_t packRite(uint8_t id) {
    const int s = slotOf(id);
    return (uint8_t)(s >= 0 ? PACKS[s].rite : themepack::RITE_WARD);
}

} // namespace pal
