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

namespace themepack { struct Pack; struct Emblem; }

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

// The BUILT-IN themes. TH_COUNT is the number of built-ins, not of themes:
// theme packs off the SD card follow them at ids TH_COUNT + slot, and
// themeCount() is the total.
enum ThemeId : uint8_t { TH_UMBRAVOX = 0, TH_BAPH0MAU5, TH_COSMICMUFFIN, TH_COUNT };

// Point the palette at a theme. An out-of-range id (>= themeCount()) falls
// back to UMBRAVOX rather than reading off the end of the table -- this is
// reached from NVS, which can hold anything.
void setTheme(uint8_t id);
const char* themeName(uint8_t id);

// Which sweeps this theme offers, as a bitmask over the Sweep enum. The gaze
// needs eyes, and UMBRAVOX has a heptagram rather than a face. 16-bit because
// there are more than eight sweeps once the cosmic ones land.
uint16_t sweepMask(uint8_t id);

// --- theme packs ----------------------------------------------------------
// Built-ins plus loaded packs.
int themeCount();

// Append a pack; returns its theme id (TH_COUNT + slot), or -1 when
// themepack::MAX_PACKS are already held. The pack is COPIED (the caller's may
// go away) and its status colours sanitised on the copy. The emblem is stored
// by POINTER, not copied: the caller keeps its buffer alive (null = none).
// Not thread-safe: call before anything draws (boot, or the sim's setup).
int addPack(const themepack::Pack& p, const themepack::Emblem* e);

// The theme id of the pack with exactly this name, or -1. Packs only: a
// built-in's name never matches.
int findPack(const char* name);

// The theme to select at boot from what NVS held: a saved pack NAME wins
// (its id now, or UMBRAVOX if that pack is gone); with no name, the saved id
// if it is a built-in, else UMBRAVOX -- a bare pack id from NVS is never
// trusted, because the slots may have shifted.
uint8_t resolveSaved(const char* name, uint8_t savedId);

enum Centrepiece : uint8_t { CP_HEPTAGRAM, CP_SEAL, CP_EMBLEM };
Centrepiece centrepiece(uint8_t id);

// The emblem art for this theme: the baked muffin for COSMIC MUFFIN, the
// pack's for a pack (may be null), null for the heptagram / seal built-ins.
const themepack::Emblem* emblem(uint8_t id);

// The masthead text on the scan screen.
const char* header(uint8_t id);

// The sweep a theme falls back to when the chosen one is not offered:
// RADAR for the built-ins, the pack's own `sweep` for a pack.
uint8_t startSweep(uint8_t id);

// The pack's alert rite (a themepack::Rite). Only meaningful for packs; the
// built-ins keep their own alert logic and get RITE_WARD here.
uint8_t packRite(uint8_t id);

} // namespace pal
