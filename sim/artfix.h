// artfix.h: valid .art files for the host tests.
// Palette: index 0 black, 1 red, 2 green, 3 blue (RGB565). Pixel (x,y) of frame f has index ((x + y + f) % 3) + 1,
// except that x == 0 is index 0 (so a transparent art leaves its left column untouched). Delays 100, 200, 300, ...
#pragma once
#include <stdint.h>
#include <vector>

namespace artfix {

static const uint16_t PAL[4] = {0x0000, 0xF800, 0x07E0, 0x001F};

inline uint8_t indexAt(int x, int y, int f) { return x == 0 ? 0 : (uint8_t)(((x + y + f) % 3) + 1); }

inline std::vector<uint8_t> make(int w, int h, int frames, bool transparent) {
    std::vector<uint8_t> d = {'U', 'V', 'A', 'R', 1, (uint8_t)(transparent ? 1 : 0),
                              (uint8_t)(w & 0xFF), (uint8_t)(w >> 8), (uint8_t)(h & 0xFF), (uint8_t)(h >> 8),
                              (uint8_t)(frames & 0xFF), (uint8_t)(frames >> 8), 4, 0, 0, 0};
    for (uint16_t c : PAL) { d.push_back(c & 0xFF); d.push_back(c >> 8); }
    for (int f = 0; f < frames; ++f) { const int ms = 100 * (f + 1); d.push_back(ms & 0xFF); d.push_back(ms >> 8); }
    const int bytes = (w * h + 1) / 2;
    for (int f = 0; f < frames; ++f) {
        std::vector<uint8_t> px((size_t)bytes, 0);
        for (int i = 0; i < w * h; ++i) {
            const uint8_t v = indexAt(i % w, i / w, f);
            px[(size_t)i / 2] |= (i & 1) ? v : (uint8_t)(v << 4);
        }
        d.insert(d.end(), px.begin(), px.end());
    }
    return d;
}

}  // namespace artfix
