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

// Draw rune `i` (wraps) with its top-left at x,y.
void rune5(Canvas& c, int i, int x, int y, uint16_t col);

// Draw rune `i` centred on cx,cy.
void rune5c(Canvas& c, int i, int cx, int cy, uint16_t col);

// Scaled, centred. Used where a rune stands in for a glyph of text and has
// to match its size.
void rune5sc(Canvas& c, int i, int cx, int cy, uint16_t col, int scale);

} // namespace ui
