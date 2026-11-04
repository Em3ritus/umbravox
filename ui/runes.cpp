#include "runes.h"

namespace ui {

static const uint8_t (&R)[RUNE_N][RUNE_S] = kRuneGlyphs;

void rune5(Canvas& c, int i, int x, int y, uint16_t col) {
    const uint8_t* g = R[((i % RUNE_N) + RUNE_N) % RUNE_N];
    for (int j = 0; j < RUNE_S; j++)
        for (int k = 0; k < RUNE_S; k++)
            if ((g[j] >> (RUNE_S - 1 - k)) & 1) c.pixel(x + k, y + j, col);
}

void rune5c(Canvas& c, int i, int cx, int cy, uint16_t col) {
    rune5(c, i, cx - RUNE_S / 2, cy - RUNE_S / 2, col);
}

void rune5sc(Canvas& c, int i, int cx, int cy, uint16_t col, int scale) {
    if (scale < 1) scale = 1;
    const uint8_t* g = R[((i % RUNE_N) + RUNE_N) % RUNE_N];
    const int x0 = cx - (RUNE_S * scale) / 2;
    const int y0 = cy - (RUNE_S * scale) / 2;
    for (int j = 0; j < RUNE_S; j++)
        for (int k = 0; k < RUNE_S; k++)
            if ((g[j] >> (RUNE_S - 1 - k)) & 1)
                c.fill(x0 + k * scale, y0 + j * scale, scale, scale, col);
}

} // namespace ui
