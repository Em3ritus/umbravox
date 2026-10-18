#include "seal.h"
#include "palette.h"
#include "sigil.h"
#include <math.h>

namespace ui {
namespace {

void polar(int cx, int cy, float r, float deg, int& x, int& y) {
    const float a = deg * 3.14159265f / 180.0f;
    x = cx + (int)lrintf(sinf(a) * r);
    y = cy - (int)lrintf(cosf(a) * r);
}

// A level of the bitmap, read in chalk.
uint16_t ramp16(int lv) {
    const int p = lv * 3, i = p / 15, f = p % 15;
    return i >= 3 ? pal::RAMP[3] : Canvas::mix(pal::RAMP[i], pal::RAMP[i + 1], f, 15);
}

// ...and the same level read in witchfire. Used to set parts of the seal
// alight by recolouring the strokes that are already there. Drawing a green
// mark ON TOP of the artwork read as a sticker; letting its own line work
// catch is what makes it look like the thing is doing it.
uint16_t ramp16live(int lv) {
    return lv < 8 ? Canvas::mix(pal::VOID, pal::LIVE, lv, 8)
                  : Canvas::mix(pal::LIVE, pal::LIVE_HI, lv - 8, 7);
}

// A circular patch of the seal. `dark` puts it out instead of lighting it,
// which is how the blink works: the eyes are not covered, they stop being
// there.
struct Tint { int x, y, r; bool dark; };

void blit(Canvas& c, const uint8_t* bm, int bw, int bh, int cx, int cy,
          int lift, bool bidir, int clipR, const Tint* tint, int nTint) {
    const int x0 = cx - bw / 2, y0 = cy - bh / 2;
    for (int j = 0; j < bh; j++)
        for (int i = 0; i < bw; i++) {
            const int k = j * bw + i;
            const int lv = (bm[k >> 1] >> (4 - 4 * (k & 1))) & 15;
            if (!lv) continue;
            const int px = x0 + i, py = y0 + j;
            const int dx = px - cx, dy = py - cy;
            if (dx * dx + dy * dy > clipR * clipR) continue;

            bool hot = false, dark = false;
            for (int t = 0; t < nTint && !hot; t++) {
                const int tx = px - (cx + tint[t].x), ty = py - (cy + tint[t].y);
                if (tx * tx + ty * ty <= tint[t].r * tint[t].r) {
                    hot = true; dark = tint[t].dark;
                }
            }
            if (hot) {
                if (!dark) c.pixel(px, py, ramp16live(lv));
                continue;
            }
            const uint16_t a = ramp16(lv);
            const uint16_t up = ramp16(lv < 13 ? lv + 2 : 15);
            if (!bidir) {
                c.pixel(px, py, lift ? Canvas::mix(a, up, lift, 255) : a);
            } else if (lift < 128) {
                // Overridden light (the candle): below the midpoint the seal
                // sinks toward the void, 25% of its colour at the darkest.
                c.pixel(px, py, Canvas::mix(pal::VOID, a, 64 + lift * 191 / 128, 255));
            } else {
                c.pixel(px, py, Canvas::mix(a, up, (lift - 128) * 2, 255));
            }
        }
}

// The inscribed figure: a diamond and two triangles, on the same circle.
void figure(Canvas& c, int cx, int cy, int r, uint16_t col, uint16_t lit) {
    int x[4], y[4];
    for (int k = 0; k < 4; k++) polar(cx, cy, (float)r, (float)k * 90.0f, x[k], y[k]);
    for (int k = 0; k < 4; k++) c.line(x[k], y[k], x[(k + 1) % 4], y[(k + 1) % 4], col);
    int t[3], u[3], d[3], e[3];
    for (int k = 0; k < 3; k++) {
        polar(cx, cy, (float)r, (float)k * 120.0f,         t[k], u[k]);
        polar(cx, cy, (float)r, (float)k * 120.0f + 60.0f, d[k], e[k]);
    }
    for (int k = 0; k < 3; k++) {
        c.line(t[k], u[k], t[(k + 1) % 3], u[(k + 1) % 3], lit);
        c.line(d[k], e[k], d[(k + 1) % 3], e[(k + 1) % 3], col);
    }
}

// Eight marks from the seal's own vocabulary, at 5px so the band can walk.
// The bitmap holds the face; the ring is procedural, which is what makes it
// affordable to turn every frame.
constexpr int MARK_N = 8;
const uint8_t MARK[MARK_N][5] = {
    {0b01110, 0b11000, 0b10000, 0b11000, 0b01110}, // crescent
    {0b01010, 0b00100, 0b01110, 0b00100, 0b01110}, // mercury
    {0b00100, 0b01110, 0b00100, 0b01110, 0b00100}, // brimstone cross
    {0b01110, 0b00100, 0b01110, 0b00100, 0b01010}, // sulfur
    {0b01110, 0b00100, 0b11111, 0b00100, 0b01110}, // leviathan
    {0b00100, 0b11111, 0b01110, 0b01010, 0b10001}, // inverted pentacle
    {0b01010, 0b00000, 0b00100, 0b00000, 0b01010}, // the dots
    {0b11000, 0b00100, 0b01110, 0b00100, 0b00011}, // the ninth sign
};

void mark5(Canvas& c, int i, int cx, int cy, uint16_t col) {
    const uint8_t* m = MARK[((i % MARK_N) + MARK_N) % MARK_N];
    for (int j = 0; j < 5; j++)
        for (int k = 0; k < 5; k++)
            if ((m[j] >> (4 - k)) & 1) c.pixel(cx - 2 + k, cy - 2 + j, col);
}

// Rare, and the whole point is that it is rare: caught out of the corner of
// the eye, and not quite believed.
constexpr int BLINK_EVERY = 200;

} // namespace

void drawSeal(Canvas& c, int cx, int cy, int rField, int rRunes, int rStar,
              int rBound, uint32_t frame, bool found, int foundAngle,
              uint8_t heat, int liftOverride) {
    // A double-thump, not a sine. A sine is what every interface does when it
    // wants to look alive and it reads as a bulb; two quick beats and a
    // longer rest is the shape of something with a heart in it.
    const int p = (int)(frame % 30);
    const int d = p < 5 ? 5 - p : (p >= 8 && p < 12) ? 12 - p : 0;
    const int lift = liftOverride >= 0 ? liftOverride : 118 + d * 26;

    figure(c, cx, cy, rStar,
           Canvas::mix(pal::VOID, pal::EDGE_LIT, 160, 255),
           Canvas::mix(pal::VOID, pal::WITCH,    130, 255));

    // Every push below is guarded by remaining capacity, so the array size and
    // the guards cannot disagree.
    constexpr int MAX_TINT = 6;
    Tint tint[MAX_TINT];
    int n = 0;
    auto push = [&](const Tint& v) { if (n < MAX_TINT) tint[n++] = v; };

    if (heat) {
        // POSSESSION. The fire spreads as the evidence mounts, so a glance at
        // the dial says how bad it is without reading a number.
        if (heat >= 40)  { push(Tint{EYE_L_X, EYE_L_Y, 4, false});
                           push(Tint{EYE_R_X, EYE_R_Y, 4, false}); }
        if (heat >= 110) { push(Tint{-15, -8, 8, false});
                           push(Tint{ 15, -8, 8, false}); }
        if (heat >= 180) { push(Tint{PENTA_X, PENTA_Y, 6, false}); }
        if (heat >= 235) { n = 0; push(Tint{0, 0, rField, false}); }
    } else {
        // The eyes answer a FINDING, not the clock. Tying the flare to the
        // search's position instead made the face strobe on a timer.
        if (found) {
            const int lean = foundAngle > 180 ? -1 : 1;
            push(Tint{EYE_L_X + lean, EYE_L_Y, 4, false});
            push(Tint{EYE_R_X + lean, EYE_R_Y, 4, false});
        }
        // The pentacle beats once per channel dwell, which makes the seal the
        // clock as well as the field.
        if ((frame % 22) < 4) push(Tint{PENTA_X, PENTA_Y, 5, false});
    }

    // The flame above the horns licks: the patch that catches grows and
    // shrinks. At this size a flame is a handful of strokes disagreeing.
    const int lick = 3 + (int)(2.4f * sinf((float)frame * 0.37f));
    push(Tint{FLAME_X, FLAME_Y, lick, false});

    // The blink is deliberately the lowest-priority tint: it happens only when
    // the seal is idle, so a face that is possessed or has just found something
    // is never caught napping. That rule is stated here as a condition rather
    // than left to push order (first matching tint wins) or to whether the
    // array happens to have room. Idle never fills the array, so the capacity
    // test is a guard, not a rule.
    const bool idle = !heat && !found;
    if (idle && (frame % BLINK_EVERY) < 2 && n + 2 <= MAX_TINT) {
        push(Tint{EYE_L_X, EYE_L_Y, 4, true});
        push(Tint{EYE_R_X, EYE_R_Y, 4, true});
    }

    blit(c, FACE, FACE_W, FACE_H, cx, cy, lift, liftOverride >= 0, rField - 1, tint, n);

    c.circle(cx, cy, rField, pal::EDGE_LIT);
    c.circle(cx, cy, rBound, pal::EDGE_LIT);

    // The band walks against the search. Marks near the beam take its light;
    // the rest sit back, so the ring never reads as a plain border.
    const float beam = (float)((frame * 5) % 360);
    const float spin = -(float)frame * 0.7f;
    for (int k = 0; k < MARK_N * 2; k++) {
        const float a = spin + (float)k * (360.0f / (MARK_N * 2));
        int rx, ry;
        polar(cx, cy, (float)rRunes, a, rx, ry);
        const int da = ((int)(beam - a) % 360 + 360) % 360;
        const int near = da < 180 ? da : 360 - da;
        const int lum = near < 50 ? 255 - near * 2 : 90;
        mark5(c, k, rx, ry, Canvas::mix(pal::VOID, pal::BONE, lum, 255));
    }
}

void sealEyes(int& lx, int& rx, int& y) {
    lx = EYE_L_X; rx = EYE_R_X; y = EYE_L_Y;
}

} // namespace ui
