// A 5x7 all-caps bitmap face, covering ASCII 32..95.
//
// All-caps is the whole character set on purpose: a technomancer console has
// no lowercase, and dropping it halves the glyph table. Font::row() folds
// a..z onto A..Z so callers never have to care.
//
// One byte per row, low 5 bits used, bit 4 is the leftmost pixel.
#pragma once
#include <stdint.h>

namespace Font {

constexpr int W = 5;   // glyph width
constexpr int H = 7;   // glyph height
constexpr int ADV = 6; // pen advance: glyph + 1px gap

// Row `r` (0..6) of `ch`, as a 5-bit mask. Unknown glyphs come back blank.
uint8_t row(char ch, int r);

// Width in pixels a string occupies at `scale`, trailing gap excluded.
int width(const char* s, int scale = 1);

} // namespace Font
