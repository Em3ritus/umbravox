// Technomancer palette. RGB565 throughout -- what the panel takes is what the
// canvas stores, and nothing converts in between.
//
// These are variables, not constants, so the theme can change at runtime
// (pal::setTheme, driven by the table in palette.cpp). Every call site keeps
// the name it always used: pal::WITCH is still pal::WITCH, it just reads the
// current theme's value.
//
// The organising rule: chrome is WITCH (arcane violet), ceremony is BONE, and
// the cold instrument cyan survives only as ICE, and only ever as *data*.
#pragma once
#include <stdint.h>

namespace pal {

constexpr uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) {
    return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

// --- structure -----------------------------------------------------------
extern uint16_t VOID;       // page
extern uint16_t PANEL;      // card fill
extern uint16_t GRID;       // lattice inside the circle
extern uint16_t EDGE;       // card border, ring work
extern uint16_t EDGE_LIT;   // border, emphasised

// --- type ----------------------------------------------------------------
extern uint16_t DIM;        // labels
extern uint16_t TEXT;       // body
extern uint16_t BRIGHT;     // emphasis

// --- chrome: ours ---------------------------------------------------------
extern uint16_t WITCH;      // live marks
extern uint16_t WITCH_HI;   // the hot edge
extern uint16_t BONE;       // runes, inscription, ceremony

// --- the search -----------------------------------------------------------
extern uint16_t SWEEP;      // the beam body
extern uint16_t SWEEP_HI;   // its leading edge

// --- the centrepiece, when a theme has a bitmap one ------------------------
extern uint16_t LIVE;       // parts of the seal that are alight
extern uint16_t LIVE_HI;
extern uint16_t RAMP[4];    // the bitmap's four anchor levels, dark to light

// --- data: theirs ---------------------------------------------------------
// These names are historical and mean the BAND, not the hue: MAGENTA is
// 2.4 GHz, ICE is 5 GHz, GREEN is BLE. BAPH0MAU5 paints them green, white
// and violet. Renaming them would mean editing all nine screen files, which
// is the exact cost this seam exists to avoid.
extern uint16_t MAGENTA;    // 2.4 GHz
extern uint16_t ICE;        // 5 GHz
extern uint16_t GREEN;      // BLE, and "good"

// --- status: never used for data -----------------------------------------
extern uint16_t AMBER;
extern uint16_t RED;

enum ThemeId : uint8_t { TH_UMBRAVOX = 0, TH_BAPH0MAU5, TH_COUNT };

// Point the palette at a theme. An out-of-range id falls back to UMBRAVOX
// rather than reading off the end of the table -- this is reached from NVS,
// which can hold anything.
void setTheme(uint8_t id);
const char* themeName(uint8_t id);

// Which sweeps this theme offers, as a bitmask over the Sweep enum. The gaze
// needs eyes, and UMBRAVOX has a heptagram rather than a face.
uint8_t sweepMask(uint8_t id);

} // namespace pal
