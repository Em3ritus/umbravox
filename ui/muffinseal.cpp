#include "muffinseal.h"
#include "muffin.h"
#include "palette.h"

namespace ui {
namespace {

// liftOverride is the same contract the seal uses: 128 = as drawn, below sinks
// toward VOID (a dim), above brightens toward white (a lift).
inline uint16_t applyLift(uint16_t col, int l) {
    if (l < 0 || l == 128) return col;
    if (l < 128) return Canvas::mix(col, pal::VOID, 128 - l, 128);
    return Canvas::mix(col, 0xFFFF, l - 128, 127);
}

} // namespace

void drawMuffin(Canvas& c, int cx, int cy, int r, uint32_t frame, int liftOverride) {
    (void)frame;
    if (r <= 0) return;
    const int d = 2 * r;
    for (int y = cy - r; y <= cy + r; y++) {
        if (y < 0 || y >= H) continue;
        const int dy = y - cy;
        for (int x = cx - r; x <= cx + r; x++) {
            if (x < 0 || x >= W) continue;
            const int dx = x - cx;
            if (dx * dx + dy * dy > r * r) continue;        // mask to the circle
            int sx = (x - (cx - r)) * MUFFIN_W / d;
            int sy = (y - (cy - r)) * MUFFIN_H / d;
            if (sx < 0) sx = 0; else if (sx >= MUFFIN_W) sx = MUFFIN_W - 1;
            if (sy < 0) sy = 0; else if (sy >= MUFFIN_H) sy = MUFFIN_H - 1;
            const uint16_t col = MUFFIN_PAL[MUFFIN_PX[sy * MUFFIN_W + sx]];
            c.pixel(x, y, applyLift(col, liftOverride));
        }
    }
}

static_assert(MUFFIN_W <= themepack::MAX_EMBLEM_DIM && MUFFIN_H <= themepack::MAX_EMBLEM_DIM,
              "the baked muffin must fit an Emblem");
static_assert(MUFFIN_NCOL <= themepack::MAX_EMBLEM_COLORS, "the baked muffin must fit an Emblem");

const themepack::Emblem& muffinEmblem() {
    // Built on first use (a function-local static), so there is no static-
    // initialisation order to get wrong.
    static const themepack::Emblem e = [] {
        themepack::Emblem m{};
        m.w = (uint8_t)MUFFIN_W;
        m.h = (uint8_t)MUFFIN_H;
        m.ncol = (uint8_t)MUFFIN_NCOL;
        for (int i = 0; i < MUFFIN_NCOL; i++) m.pal[i] = MUFFIN_PAL[i];
        m.px = MUFFIN_PX;
        return m;
    }();
    return e;
}

} // namespace ui
