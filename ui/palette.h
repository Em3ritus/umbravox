// Technomancer palette, occult register. RGB565 throughout -- what the panel
// takes is what the canvas stores, and nothing converts in between.
//
// The organising rule: chrome is WITCH (arcane violet), ceremony is BONE, and
// the cold instrument cyan survives only as ICE, and only ever as *data*. That
// one move is most of what separates a summoning circle from a dashboard.
#pragma once
#include <stdint.h>

namespace pal {

constexpr uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) {
    return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

// --- structure -----------------------------------------------------------
constexpr uint16_t VOID     = rgb(0x07, 0x04, 0x0E); // page: near-black aubergine
constexpr uint16_t PANEL    = rgb(0x13, 0x0B, 0x22); // card fill
constexpr uint16_t GRID     = rgb(0x18, 0x0F, 0x2C); // lattice inside the circle
constexpr uint16_t EDGE     = rgb(0x2E, 0x1F, 0x52); // card border, ring work
constexpr uint16_t EDGE_LIT = rgb(0x56, 0x36, 0x97); // border, emphasised

// --- type ----------------------------------------------------------------
constexpr uint16_t DIM      = rgb(0x6B, 0x4F, 0x9E); // labels
constexpr uint16_t TEXT     = rgb(0xC2, 0xAB, 0xE4); // body
constexpr uint16_t BRIGHT   = rgb(0xF2, 0xE8, 0xFF); // emphasis

// --- chrome: ours ---------------------------------------------------------
constexpr uint16_t WITCH    = rgb(0x9D, 0x7B, 0xFF); // the sweep, live marks
constexpr uint16_t WITCH_HI = rgb(0xD6, 0xC2, 0xFF); // witchlight, the hot edge
constexpr uint16_t BONE     = rgb(0xC9, 0xB8, 0x97); // runes, inscription, ceremony

// --- data: theirs ---------------------------------------------------------
constexpr uint16_t MAGENTA  = rgb(0xD0, 0x2B, 0xB4); // 2.4 GHz
constexpr uint16_t ICE      = rgb(0x6F, 0xD3, 0xF5); // 5 GHz
constexpr uint16_t GREEN    = rgb(0x4A, 0xDE, 0x80); // BLE, and "good"

// --- status: never used for data -----------------------------------------
constexpr uint16_t AMBER    = rgb(0xF5, 0x9E, 0x0B);
constexpr uint16_t RED      = rgb(0xEF, 0x44, 0x44);

} // namespace pal
