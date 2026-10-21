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

// The star's limbs. Drawing, the mark band and detection all read these, so
// a change to the spin, the limb count or the tail length moves all three.
constexpr int   STAR_N    = 5;
constexpr float STAR_STEP = 360.0f / STAR_N;
constexpr int   STAR_TAIL = 14;

float starLimb(uint32_t frame, int k) {
    return (float)frame * 2.4f + (float)k * STAR_STEP;
}
// Brightness of the tail d degrees behind a limb; drawn only if >= 12.
int starTailLum(int d) { return 200 * (STAR_TAIL - d) / STAR_TAIL; }
// The farthest d the drawing actually lights. Detection uses this, not a
// restated number, so a contact is "found" exactly where a tail is visible.
int starReach() {
    int d = STAR_TAIL;
    while (d > 0 && starTailLum(d) < 12) d--;
    return d;
}

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
        case SW_PULSAR:               // a distance-finder: marks hold bright
            return (255 + floorLum) / 2;
        case SW_STARFALL:
        case SW_AURORA:
            return floorLum;          // a wash: marks hold at their floor
        case SW_STAR: {
            near = 180;
            for (int k = 0; k < STAR_N; k++) {
                const int n = nearDeg(starLimb(frame, k), markAngle);
                if (n < near) near = n;
            }
            break;
        }
        default:
            near = nearDeg(beamOf(frame), markAngle);
    }
    return near < 50 ? 255 - near * 2 : floorLum;
}

int pulseRing(uint32_t frame, int rField) { return pulseHead(frame, rField); }

int contactRadius(int dist, int rField) { return dist * (rField - 3) / 100; }

bool searchTouches(uint8_t mode, uint32_t frame, int angle, int dist, int rField) {
    switch (mode) {
        case SW_PULSE: {
            const int dp = contactRadius(dist, rField);
            const int d  = dp - pulseHead(frame, rField);
            return d <= 0 && d > -5;
        }
        case SW_STAR: {
            const int reach = starReach();
            for (int k = 0; k < STAR_N; k++) {
                const int d = (((int)starLimb(frame, k) - angle) % 360 + 360) % 360;
                if (d <= reach) return true;
            }
            return false;
        }
        case SW_PULSAR: {   // finds by distance, on the same ring the pulse uses
            const int dp = contactRadius(dist, rField);
            const int d  = dp - pulseHead(frame, rField);
            return d <= 0 && d > -5;
        }
        case SW_CANDLE:
        case SW_STARFALL:
        case SW_AURORA:
            return false;   // nothing sweeps, so nothing is ever just found
        // SW_GALAXY / SW_ORBIT / SW_COMET find by bearing, like the radar.
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
        for (int k = 0; k < STAR_N; k++)
            for (int d = STAR_TAIL; d >= 0; d--) {
                const int lum = starTailLum(d);
                if (lum < 12) continue;
                int x, y;
                polar(cx, cy, (float)(rField - 1), starLimb(frame, k) - (float)d, x, y);
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

    // --- cosmic sweeps: every colour from pal::, so each theme recolours them,
    //     and every pixel kept inside rField - 1. ---

    case SW_GALAXY: {
        // Two spiral arms turning about the core, drawn as continuous strokes.
        const float rot = (float)frame * 0.06f;
        const int R = rField - 1;
        for (int arm = 0; arm < 2; arm++) {
            int px = -1, py = -1;
            for (float t = 0.2f; t < 3.0f; t += 0.12f) {
                const float f = t / 3.0f;
                const int r = (int)((0.40f + 0.60f * f) * (float)R);
                const float a = t * 2.1f + (float)arm * 3.14159265f + rot;
                const int x = cx + (int)lrintf(cosf(a) * (float)r);
                const int y = cy + (int)lrintf(sinf(a) * (float)r);
                if ((x - cx) * (x - cx) + (y - cy) * (y - cy) > R * R) { px = -1; continue; }
                const uint16_t col = Canvas::mix(pal::VOID, arm ? pal::SWEEP : pal::SWEEP_HI,
                                                 255 - (int)(f * 70.0f), 255);
                if (px >= 0) c.line(px, py, x, y, col); else c.pixel(x, y, col);
                px = x; py = y;
            }
        }
        break;
    }

    case SW_ORBIT: {
        // A planet riding the rim, a comet tail behind it, a moon inside.
        const int rr = (rField - 1) * 9 / 10;
        const float a = (float)frame * 0.11f;
        int px = -1, py = -1;
        for (int k = 14; k >= 0; k--) {
            const float aa = a - (float)k * 0.12f;
            const int x = cx + (int)lrintf(cosf(aa) * (float)rr);
            const int y = cy + (int)lrintf(sinf(aa) * (float)rr);
            const uint16_t col = Canvas::mix(pal::VOID, k < 7 ? pal::SWEEP : pal::WITCH, 255 - k * 13, 255);
            if (px >= 0) c.line(px, py, x, y, col);
            px = x; py = y;
        }
        c.disc(px, py, 2, pal::BONE);
        const float m = -a * 0.6f;
        const int mr = (rField - 1) * 6 / 10;
        c.disc(cx + (int)lrintf(cosf(m) * (float)mr), cy + (int)lrintf(sinf(m) * (float)mr), 1, pal::SWEEP_HI);
        break;
    }

    case SW_STARFALL: {
        // A field that twinkles in place, and a shooting star crossing it.
        const int R = rField - 2;
        for (int i = 0; i < 18; i++) {
            const uint32_t h = (uint32_t)i * 2654435761u;
            const int ang = (int)(h % 360u);
            const int rad = (int)((h >> 9) % (uint32_t)(R > 1 ? R : 1));
            int x, y;
            polar(cx, cy, (float)rad, (float)ang, x, y);
            int tw = 150 + (int)(105.0f * sinf((float)frame * 0.3f + (float)i));
            tw = tw < 40 ? 40 : tw > 255 ? 255 : tw;
            const uint16_t col = Canvas::mix(pal::VOID, (i & 1) ? pal::SWEEP : pal::BONE, tw, 255);
            c.pixel(x, y, col);
            if (tw > 200) {                          // the bright ones flare into a cross
                c.pixel(x - 1, y, col); c.pixel(x + 1, y, col);
                c.pixel(x, y - 1, col); c.pixel(x, y + 1, col);
            }
        }
        const float p = (float)(frame % 40u) / 40.0f;
        const int hx = cx - R + (int)(p * 2.0f * (float)R);
        const int hy = cy - R / 2 + (int)(p * (float)R);
        for (int k = 9; k >= 0; k--) {
            const int tx = hx - k * 2, ty = hy - k;
            if ((tx - cx) * (tx - cx) + (ty - cy) * (ty - cy) > R * R) continue;
            c.pixel(tx, ty, Canvas::mix(pal::VOID, k ? pal::SWEEP : pal::BONE, 255 - k * 22, 255));
            if (k == 0) c.disc(tx, ty, 1, pal::BONE);
        }
        break;
    }

    case SW_PULSAR: {
        // Three rings leaving the core on the pulse's own clock.
        const int span = pulseSpan(rField);
        for (int k = 0; k < 3; k++) {
            const int r = (pulseHead(frame, rField) - 2 + k * span / 3) % span + 2;
            if (r < 2 || r > rField - 1) continue;
            const int lum = 255 - r * 120 / span;
            const uint16_t col = Canvas::mix(pal::VOID, (k & 1) ? pal::SWEEP : pal::SWEEP_HI, lum, 255);
            c.circle(cx, cy, r, col);
            if (r > 2) c.circle(cx, cy, r - 1, col);
        }
        c.disc(cx, cy, 1, pal::BONE);
        break;
    }

    case SW_COMET: {
        // One comet whipping round the rim, a long tail behind it.
        const int rr = (rField - 1) * 9 / 10;
        const float a = (float)frame * 0.12f;
        int px = -1, py = -1;
        for (int k = 30; k >= 0; k--) {
            const float aa = a - (float)k * 0.10f;
            const int x = cx + (int)lrintf(cosf(aa) * (float)rr);
            const int y = cy + (int)lrintf(sinf(aa) * (float)rr);
            const uint16_t col = Canvas::mix(pal::VOID, k < 12 ? pal::SWEEP_HI : pal::SWEEP, 255 - k * 7, 255);
            if (px >= 0) c.line(px, py, x, y, col);
            px = x; py = y;
        }
        c.disc(px, py, 2, pal::BONE);
        break;
    }

    case SW_AURORA: {
        // Three bands that undulate around the dial like curtains of light.
        for (int band = 0; band < 3; band++) {
            const float baseR = (0.55f + 0.14f * (float)band) * (float)(rField - 1);
            const uint16_t col = band == 0 ? pal::SWEEP : band == 1 ? pal::SWEEP_HI : pal::WITCH;
            int px = -1, py = -1;
            for (int s = 0; s <= 48; s++) {
                const float a = 6.2831853f * (float)(s % 48) / 48.0f;
                const float wob = sinf(a * 3.0f + (float)frame * 0.1f + (float)band * 1.3f)
                                * (float)rField * 0.05f;
                int r = (int)(baseR + wob);
                if (r > rField - 1) r = rField - 1;
                const int x = cx + (int)lrintf(cosf(a) * (float)r);
                const int y = cy + (int)lrintf(sinf(a) * (float)r);
                if (px >= 0) c.line(px, py, x, y, col);
                px = x; py = y;
            }
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
