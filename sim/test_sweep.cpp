// Host test for the cosmic sweeps: they exist, every theme offers them, and
// each draws within the dial (never off the canvas or past the field ring).
#include "../ui/canvas.h"
#include "../ui/palette.h"
#include "../ui/sweep.h"
#include "../ui/model.h"
#include <cstdio>

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

    printf(fails ? "\n%d FAILED\n" : "\nall passed\n", fails);
    return fails ? 1 : 0;
}
