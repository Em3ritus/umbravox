#include "sweep.h"
#include "palette.h"
#include "model.h"
#include "layout.h"
#include <math.h>

namespace ui {
namespace {

void polar(int cx, int cy, float r, float deg, int& x, int& y) {
    const float a = deg * 3.14159265f / 180.0f;
    x = cx + (int)lrintf(sinf(a) * r);
    y = cy - (int)lrintf(cosf(a) * r);
}

// A ray from an arbitrary origin, clipped to the field. The gaze starts at
// the eyes rather than the centre, so it cannot just be drawn to a radius --
// it has to be walked and stopped at the bound.
void ray(Canvas& c, int ox, int oy, float deg, int cx, int cy, int R, uint16_t col) {
    const float a = deg * 3.14159265f / 180.0f;
    const float sx = sinf(a), sy = -cosf(a);
    for (float t = 0; t < 2.0f * (float)R; t += 0.7f) {
        const int x = ox + (int)lrintf(sx * t), y = oy + (int)lrintf(sy * t);
        const int dx = x - cx, dy = y - cy;
        if (dx * dx + dy * dy > R * R) break;
        c.pixel(x, y, col);
    }
}

float beamOf(uint32_t frame) { return (float)((frame * 5) % 360); }

// The candle's gutter, about -1.9..1.9. One place, so the rings and the
// seal's brightness flicker together and cannot drift apart.
float gutter(uint32_t frame) {
    return sinf((float)frame * 0.41f)
         + 0.6f * sinf((float)frame * 1.07f)
         + 0.3f * sinf((float)frame * 2.3f);
}

// The pulse's leading ring, in pixels. Drawing and detection both read this,
// so they cannot disagree. Guarded so a tiny field cannot divide by zero.
int pulseSpan(int rField) { return rField - 2 > 1 ? rField - 2 : 1; }
int pulseHead(uint32_t frame, int rField) {
    return 2 + (int)(frame * 1.15f) % pulseSpan(rField);
}

int nearDeg(float beam, float a) {
    const int da = ((int)(beam - a) % 360 + 360) % 360;
    return da < 180 ? da : 360 - da;
}

} // namespace

int searchMarkLum(uint8_t mode, uint32_t frame, float markAngle, int floorLum) {
    int near;
    switch (mode) {
        case SW_PULSE:
        case SW_CANDLE:
            return (255 + floorLum) / 2;
        case SW_STAR: {
            const float spin = (float)frame * 2.4f;
            near = 180;
            for (int k = 0; k < 5; k++) {
                const int n = nearDeg(spin + (float)k * 72.0f, markAngle);
                if (n < near) near = n;
            }
            break;
        }
        default:
            near = nearDeg(beamOf(frame), markAngle);
    }
    return near < 50 ? 255 - near * 2 : floorLum;
}

int contactRadius(int dist, int rField) { return dist * (rField - 3) / 100; }

bool searchTouches(uint8_t mode, uint32_t frame, int angle, int dist) {
    switch (mode) {
        case SW_PULSE: {
            const int dp = contactRadius(dist, R_FIELD);
            const int d  = dp - pulseHead(frame, R_FIELD);
            return d <= 0 && d > -5;
        }
        case SW_STAR: {
            const float spin = (float)frame * 2.4f;
            for (int k = 0; k < 5; k++) {
                const int d = (((int)(spin + (float)k * 72.0f) - angle) % 360 + 360) % 360;
                if (d <= 20) return true;
            }
            return false;
        }
        case SW_CANDLE:
            return false;   // nothing sweeps, so nothing is ever just found
        default:
            return (((int)beamOf(frame) - angle + 360) % 360) <= 26;
    }
}

int searchLift(uint8_t mode, uint32_t frame) {
    if (mode != SW_CANDLE) return -1;
    // 128 is the seal as drawn; the gutter swings it into the dark and back.
    int v = 128 + (int)lrintf(gutter(frame) * 67.0f);
    return v < 0 ? 0 : v > 255 ? 255 : v;
}

void drawSearch(Canvas& c, uint8_t mode, int cx, int cy, int rField,
                int eyeLx, int eyeRx, int eyeY, uint32_t frame) {
    const float beam = beamOf(frame);

    switch (mode) {

    case SW_GAZE: {
        // Two beams, one from each eye, pointed the same way. The light comes
        // out of the face rather than out of the middle of the circle, so the
        // seal is doing the looking instead of sitting behind an instrument
        // that looks on its behalf.
        const int ox[2] = {cx + eyeLx, cx + eyeRx};
        const int oy    = cy + eyeY;
        const int TAIL  = 26;
        for (int s = 0; s < 2; s++)
            for (int d = TAIL; d >= 0; d--) {
                const int fade = TAIL - d;
                const int lum  = 220 * fade * fade / (TAIL * TAIL);
                if (lum < 10) continue;
                ray(c, ox[s], oy, beam - (float)d * 0.8f, cx, cy, rField - 1,
                    Canvas::mix(pal::VOID, d == 0 ? pal::SWEEP_HI : pal::SWEEP,
                                d == 0 ? 170 : lum / 4, 255));
            }
        break;
    }

    case SW_PULSE: {
        // A ring leaves the centre and dies at the bound. It finds by
        // DISTANCE rather than bearing -- near things answer first -- which
        // reads as divination rather than a dish going round.
        const int span = pulseSpan(rField);
        const int r0 = pulseHead(frame, rField);
        for (int t = 0; t < 6; t++) {
            const int r = r0 - t * 2;
            if (r < 2 || r > rField - 1) continue;
            const int out = 255 - r * 150 / span;
            const int lum = (t ? 165 - t * 26 : 255) * (160 + out * 95 / 255) / 255;
            if (lum < 10) continue;
            if (t == 0) {
                // Two radii, not one: a single-pixel ring crossing the chalk
                // simply disappears into it.
                const uint16_t hi = Canvas::mix(pal::VOID, pal::SWEEP_HI, lum, 255);
                c.circle(cx, cy, r, hi);
                if (r > 2) c.circle(cx, cy, r - 1, hi);
            } else {
                c.dottedCircle(cx, cy, r, Canvas::mix(pal::VOID, pal::SWEEP, lum, 255), 2);
            }
        }
        break;
    }

    case SW_STAR: {
        // The inscribed figure is the search. Five limbs means the dial
        // refreshes five times a revolution, so nothing waits a whole turn.
        const float spin = (float)frame * 2.4f;
        for (int k = 0; k < 5; k++)
            for (int d = 14; d >= 0; d--) {
                const int lum = 200 * (14 - d) / 14;
                if (lum < 12) continue;
                int x, y;
                polar(cx, cy, (float)(rField - 1), spin + (float)k * 72.0f - (float)d, x, y);
                c.line(cx, cy, x, y,
                       Canvas::mix(pal::VOID, d ? pal::SWEEP : pal::SWEEP_HI,
                                   d ? lum / 4 : 140, 255));
            }
        break;
    }

    case SW_CANDLE: {
        // No beam. The circle is lit from the middle by something guttering,
        // and what is in it is simply there to be seen.
        const float g = gutter(frame);
        const int jx = (int)lrintf(1.6f * sinf((float)frame * 0.23f));
        const int jy = (int)lrintf(1.2f * sinf((float)frame * 0.31f));
        for (int t = 0; t < 4; t++) {
            const int r = rField - 3 - t * 5;
            if (r < 3) break;
            const int lum = (int)((42 - t * 7) * (1.0f + 0.4f * g));
            if (lum < 6) continue;
            c.dottedCircle(cx + jx, cy + jy, r,
                           Canvas::mix(pal::VOID, pal::SWEEP, lum, 255), 4);
        }
        break;
    }

    default: {
        // The radar. One hot edge with a cubic afterglow.
        const int TAIL = 40;   // must match the pre-theme radar exactly (regress)
        for (int d = TAIL; d >= 1; d--) {
            const int fade = TAIL - d;
            const int lum = 255 * fade * fade * fade / (TAIL * TAIL * TAIL);
            if (lum < 8) continue;
            int x, y;
            polar(cx, cy, (float)(rField - 1), beam - (float)d, x, y);
            c.line(cx, cy, x, y, Canvas::mix(pal::VOID, pal::SWEEP, lum, 255));
        }
        int bx, by;
        polar(cx, cy, (float)(rField - 1), beam, bx, by);
        c.line(cx, cy, bx, by, pal::SWEEP_HI);
        break;
    }
    }
}

} // namespace ui
