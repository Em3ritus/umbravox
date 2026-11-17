// fontfix.h: a small valid .fnt (h=8, ascent 7) for the host tests.
// 'A' is 5 wide with a fixed bitmap, 'a' 4 wide, space advances 3 and draws nothing, every other glyph is w=0 adv 4.
#pragma once
#include <stdint.h>
#include <vector>

namespace fontfix {

// Rows of 'A' and 'a', MSB = leftmost pixel.
static const uint8_t ROWS_A[8]  = {0x70, 0x88, 0x88, 0xF8, 0x88, 0x88, 0x88, 0x00};
static const uint8_t ROWS_a[8]  = {0x00, 0x00, 0x60, 0x10, 0x70, 0x90, 0x70, 0x00};

inline std::vector<uint8_t> make() {
    const int H = 8, COUNT = 95;
    std::vector<uint8_t> f = {'U', 'V', 'F', 'N', 1, (uint8_t)H, 7, 32, (uint8_t)COUNT, 0, 0, 0};
    std::vector<uint8_t> bits;
    for (int i = 0; i < COUNT; ++i) {
        const char c = (char)(32 + i);
        uint8_t adv = 4, w = 0;
        const uint8_t* rows = nullptr;
        if (c == ' ') adv = 3;
        if (c == 'A') { adv = 6; w = 5; rows = ROWS_A; }
        if (c == 'a') { adv = 5; w = 4; rows = ROWS_a; }
        const uint16_t off = (uint16_t)bits.size();
        f.push_back(adv); f.push_back(w); f.push_back(off & 0xFF); f.push_back(off >> 8);
        if (rows) for (int r = 0; r < H; ++r) bits.push_back(rows[r]);
    }
    f.insert(f.end(), bits.begin(), bits.end());
    return f;
}

}  // namespace fontfix
