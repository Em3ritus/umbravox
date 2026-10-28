#include "motif.h"
#include "palette.h"
#include "trigwrap.h"
#include <math.h>

namespace ui {
namespace {

// liftOverride is the same contract the seal uses: 128 = as drawn, below sinks
// toward VOID (a dim), above brightens toward white (a lift).
inline uint16_t applyLift(uint16_t col, int l) {
    if (l < 0 || l == 128) return col;
    if (l < 128) return Canvas::mix(col, pal::VOID, 128 - l, 128);
    return Canvas::mix(col, 0xFFFF, l - 128, 127);
}

// Radar convention: 0 degrees is up, angle increases clockwise.
void polar(int cx, int cy, int r, float deg, int& x, int& y) {
    const float a = wrapDeg(deg) * 3.14159265f / 180.0f;   // see trigwrap.h
    x = cx + (int)lrintf(sinf(a) * (float)r);
    y = cy - (int)lrintf(cosf(a) * (float)r);
}

} // namespace

// The muffin blitter this was moved from, parameterised on the art.
void drawEmblem(Canvas& c, const themepack::Emblem& e, int cx, int cy, int r,
                uint32_t frame, int liftOverride) {
    (void)frame;
    if (r <= 0 || e.w == 0 || e.h == 0 || !e.px) return;
    const int d = 2 * r;
    const int ew = e.w, eh = e.h;
    for (int y = cy - r; y <= cy + r; y++) {
        if (y < 0 || y >= c.h) continue;
        const int dy = y - cy;
        for (int x = cx - r; x <= cx + r; x++) {
            if (x < 0 || x >= c.w) continue;
            const int dx = x - cx;
            if (dx * dx + dy * dy > r * r) continue;        // mask to the circle
            int sx = (x - (cx - r)) * ew / d;
            int sy = (y - (cy - r)) * eh / d;
            if (sx < 0) sx = 0; else if (sx >= ew) sx = ew - 1;
            if (sy < 0) sy = 0; else if (sy >= eh) sy = eh - 1;
            const uint16_t col = e.pal[e.px[sy * ew + sx]];
            c.pixel(x, y, applyLift(col, liftOverride));
        }
    }
}

// The {7/3} heptagram: seven points, each joined to the one three along.
// UMBRAVOX's figure, and the fallback picture of any theme without an emblem.
void heptagram(Canvas& c, int cx, int cy, int r, uint16_t col) {
    int sx[7], sy[7];
    for (int k = 0; k < 7; k++)
        polar(cx, cy, r, (float)k * 360.0f / 7.0f, sx[k], sy[k]);
    for (int k = 0; k < 7; k++)
        c.line(sx[k], sy[k], sx[(k + 3) % 7], sy[(k + 3) % 7], col);
}

void drawMotif(Canvas& c, uint8_t theme, int cx, int cy, int r, uint32_t frame,
               int lift, uint16_t col) {
    const themepack::Emblem* e = pal::emblem(theme);
    if (e) drawEmblem(c, *e, cx, cy, r, frame, lift);
    else   heptagram(c, cx, cy, r, col);
}

} // namespace ui
