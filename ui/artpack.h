// artpack.h: the .art picture a theme pack carries (decor, backgrounds, panel frames, icons).
//
// Version 1 layout:
//   16-byte header  'U','V','A','R', 1, flags, w u16, h u16, frames u16, ncol u8, 0,0,0   (LE; flags bit 0 =
//                   index 0 is transparent)
//   ncol (2..16) x RGB565 LE
//   frames x delay u16 LE ms (present even for one frame; each >= 1)
//   frames x ceil(w*h/2) bytes, 4-bit indices, high nibble first, row-major
// w 1..480, h 1..320, frames 1..64, exact length, every index < ncol. The device never scales art: the designer
// converts each picture at the size it is placed.
#pragma once
#include <stddef.h>
#include <stdint.h>

namespace artpack {

constexpr size_t MAX_ART        = 196608;
constexpr int    MAX_ART_FRAMES = 64;
constexpr int    MAX_ART_W      = 480;
constexpr int    MAX_ART_H      = 320;
constexpr int    HEADER         = 16;

struct Art {
    uint16_t w = 0, h = 0, frames = 0;
    uint8_t  ncol = 0;
    bool     transparent = false;
    uint16_t pal[16] = {};
    const uint8_t* delays = nullptr;   // frames x u16 LE
    const uint8_t* px = nullptr;       // frames x frameBytes
    uint32_t frameBytes = 0, cycleMs = 0;
};

// Validates d[0..n) and points `out` into it (the bytes must outlive `out`). On failure *why is a reason.
bool parse(const uint8_t* d, size_t n, Art& out, const char** why);
uint16_t frameAt(const Art& a, uint32_t nowMs);          // by cumulative delays, mod cycleMs; 0 for one frame
inline bool animated(const Art& a) { return a.frames > 1; }
inline uint8_t index(const Art& a, uint16_t frame, int x, int y) {   // caller keeps x,y inside the art
    const uint32_t p = (uint32_t)y * a.w + (uint32_t)x;
    const uint8_t b = a.px[(size_t)frame * a.frameBytes + p / 2];
    return (p & 1) ? (b & 0x0F) : (b >> 4);
}

}  // namespace artpack
