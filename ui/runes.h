// A set of 5x5 marks for the circle.
//
// Invented, not transcribed: these are abstract geometry, not Enochian or
// Theban or anything else with a claimed meaning. They need to read as
// deliberate at 5 pixels and tile evenly around a ring; that is the only
// specification they answer to.
#pragma once
#include <stdint.h>
#include "canvas.h"

namespace ui {

constexpr int RUNE_N = 16;
constexpr int RUNE_S = 5;

inline constexpr uint8_t kRuneGlyphs[RUNE_N][RUNE_S] = {
    {0b00100, 0b01110, 0b11011, 0b01110, 0b00100}, // eye
    {0b10001, 0b01010, 0b00100, 0b01010, 0b10001}, // chi
    {0b11111, 0b00100, 0b00100, 0b00100, 0b11111}, // gate
    {0b00100, 0b01010, 0b10001, 0b01010, 0b00100}, // diamond
    {0b01110, 0b10000, 0b01110, 0b00001, 0b01110}, // serpent
    {0b10101, 0b10101, 0b11111, 0b00100, 0b00100}, // trident
    {0b00100, 0b01110, 0b00100, 0b10101, 0b01110}, // anchor
    {0b00100, 0b01110, 0b10101, 0b00100, 0b00100}, // arrow
    {0b00100, 0b11111, 0b00100, 0b11111, 0b00100}, // double bar
    {0b10001, 0b01010, 0b00100, 0b00100, 0b00100}, // fork
    {0b10001, 0b10001, 0b01010, 0b00100, 0b00100}, // vessel
    {0b10101, 0b11111, 0b01110, 0b00100, 0b01110}, // crown
    {0b01110, 0b10001, 0b01110, 0b00100, 0b01110}, // loop
    {0b10000, 0b10000, 0b10000, 0b10000, 0b11111}, // corner
    {0b11000, 0b01100, 0b00110, 0b00011, 0b00001}, // steps
    {0b00100, 0b10101, 0b01110, 0b10101, 0b00100}, // star
};
// The five row bitmaps of rune `i` (wraps); bit 4 is the leftmost column.
inline const uint8_t* runeBits(int i) { return kRuneGlyphs[((i % RUNE_N) + RUNE_N) % RUNE_N]; }

// Draw rune `i` (wraps) with its top-left at x,y.
void rune5(Canvas& c, int i, int x, int y, uint16_t col);

// Draw rune `i` centred on cx,cy.
void rune5c(Canvas& c, int i, int cx, int cy, uint16_t col);

// Scaled, centred. Used where a rune stands in for a glyph of text and has
// to match its size.
void rune5sc(Canvas& c, int i, int cx, int cy, uint16_t col, int scale);

} // namespace ui
