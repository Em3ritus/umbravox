// fontpack.h: the .fnt pixel font a theme pack carries (one face at one pixel size).
//
// Version 1 layout:
//   12-byte header  'U','V','F','N', 1, h, ascent, first=32, count=95, 0,0,0
//   95 x {adv u8, w u8, off u16 LE}   one entry per char 32..126; off is into the bitmap area
//   bitmaps         each glyph h rows x ceil(w/8) bytes, MSB = leftmost pixel
// Proportional, fixed line height h. parse() checks every glyph's bitmap is inside the file once, so
// pixel() needs no bounds work beyond the glyph box. The built-in 5x7 (font5x7.h) is a separate thing.
#pragma once
#include <stddef.h>
#include <stdint.h>

namespace fontpack {

constexpr size_t MAX_FONT         = 12800;
constexpr int    FONT_MIN_H       = 6;
constexpr int    FONT_MAX_H       = 32;
constexpr int    FONT_MAX_GLYPH_W = 32;
constexpr int    FIRST = 32, COUNT = 95;
constexpr int    HEADER = 12, ENTRY = 4;

struct Font {
    uint8_t h = 0, ascent = 0;
    const uint8_t* meta = nullptr;   // the 95-entry glyph table
    const uint8_t* bits = nullptr;   // the bitmap area
};

// Validates d[0..n) and points `out` into it (the bytes must outlive `out`). On failure *why is a reason.
bool parse(const uint8_t* d, size_t n, Font& out, const char** why);
int  advance(const Font& f, char c);                 // 0 for chars outside 32..126
bool pixel(const Font& f, char c, int x, int y);     // false outside the glyph box
int  width(const Font& f, const char* s, int scale); // advances, minus the last glyph's trailing gap

}  // namespace fontpack
