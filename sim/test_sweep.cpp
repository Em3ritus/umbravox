// Host test for the cosmic sweeps: they exist, every theme offers them, and
// each draws within the dial (never off the canvas or past the field ring).
#include "../ui/canvas.h"
#include "../ui/palette.h"
#include "../ui/sweep.h"
#include "../ui/model.h"
#include <cstdio>
#include <math.h>

static int fails = 0;
static void check(const char* w, bool ok) {
    printf(ok ? "  ok   %s\n" : "  FAIL %s\n", w);
    if (!ok) fails++;
}

static bool drawsInBounds(uint8_t mode, int cx, int cy, int r) {
    for (uint32_t f = 0; f < 40; f += 7) {
        ui::Canvas c; c.clear(0);
        ui::drawSearch(c, mode, cx, cy, r, 0, 0, 0, f);
        for (int y = 0; y < ui::H; y++)
            for (int x = 0; x < ui::W; x++)
                if ((x - cx) * (x - cx) + (y - cy) * (y - cy) > (r + 3) * (r + 3) &&
                    c.px[y * ui::W + x] != 0) return false;
    }
    return true;
}

int main() {
    using namespace ui;
    check("eleven sweeps", SW_COUNT == 11);

    const uint8_t cosmic[6] = { SW_GALAXY, SW_ORBIT, SW_STARFALL,
                                SW_PULSAR, SW_COMET, SW_AURORA };
    const uint8_t themes[3] = { pal::TH_UMBRAVOX, pal::TH_BAPH0MAU5, pal::TH_COSMICMUFFIN };

    for (uint8_t t : themes) {
        pal::setTheme(t);
        for (uint8_t m : cosmic) {
            char buf[64];
            snprintf(buf, sizeof buf, "theme %d offers sweep %d", t, m);
            check(buf, (pal::sweepMask(t) & (uint16_t)(1u << m)) != 0);
            snprintf(buf, sizeof buf, "theme %d sweep %d draws in bounds", t, m);
            check(buf, drawsInBounds(m, 80, 60, 30));
        }
    }

    // A sweep that finds by bearing must find where it is DRAWN. Orbit and
    // Comet once reused the hidden radar beam, so contacts lit "found" in empty
    // sky: every bearing reported hot must have lit pixels near it.
    printf("detection follows the drawing:\n");
    {
        pal::setTheme(pal::TH_UMBRAVOX);
        const int cx = 80, cy = 60, R = 30;
        const uint8_t byBearing[2] = { SW_ORBIT, SW_COMET };
        for (uint8_t m : byBearing) {
            int hot = 0, blind = 0;
            for (uint32_t f = 0; f < 200; f += 3) {
                Canvas c; c.clear(0);
                drawSearch(c, m, cx, cy, R, 0, 0, 0, f);
                for (int b = 0; b < 360; b += 5) {
                    if (!searchTouches(m, f, b, 60, R)) continue;
                    hot++;
                    bool lit = false;
                    for (int d = -15; d <= 15 && !lit; d += 3)
                        for (int r = 8; r <= R && !lit; r += 2) {
                            const float a = (float)(b + d) * 3.14159265f / 180.0f;
                            const int x = cx + (int)lrintf(sinf(a) * (float)r);
                            const int y = cy - (int)lrintf(cosf(a) * (float)r);
                            if (c.px[y * W + x] != 0) lit = true;
                        }
                    if (!lit) blind++;
                }
            }
            char buf[96];
            snprintf(buf, sizeof buf, "sweep %d: every hot bearing has drawing near it (%d/%d blind)",
                     m, blind, hot);
            check(buf, hot > 0 && blind == 0);
        }
    }

    printf(fails ? "\n%d FAILED\n" : "\nall passed\n", fails);
    return fails ? 1 : 0;
}
