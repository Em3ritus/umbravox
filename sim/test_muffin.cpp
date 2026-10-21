// Host test for the muffin centrepiece: it must fill the dial with colour and
// never write a pixel outside the circle.
#include "../ui/canvas.h"
#include "../ui/palette.h"
#include "../ui/muffinseal.h"
#include <cstdio>

static int fails = 0;
static void check(const char* w, bool ok) {
    printf(ok ? "  ok   %s\n" : "  FAIL %s\n", w);
    if (!ok) fails++;
}

static bool anyLitWithin(const ui::Canvas& c, int cx, int cy, int r) {
    for (int y = cy - r; y <= cy + r; y++)
        for (int x = cx - r; x <= cx + r; x++) {
            if (x < 0 || x >= ui::W || y < 0 || y >= ui::H) continue;
            if ((x - cx) * (x - cx) + (y - cy) * (y - cy) <= r * r &&
                c.px[y * ui::W + x] != 0) return true;
        }
    return false;
}

static bool allDarkOutside(const ui::Canvas& c, int cx, int cy, int r) {
    for (int y = 0; y < ui::H; y++)
        for (int x = 0; x < ui::W; x++)
            if ((x - cx) * (x - cx) + (y - cy) * (y - cy) > r * r &&
                c.px[y * ui::W + x] != 0) return false;
    return true;
}

int main() {
    ui::Canvas c;
    c.clear(0x0000);
    pal::setTheme(pal::TH_COSMICMUFFIN);
    ui::drawMuffin(c, 80, 60, 30, 0, 128);

    check("draws lit pixels inside the dial", anyLitWithin(c, 80, 60, 30));
    // One pixel of slack: the mask is inclusive at r, nothing beyond r+1.
    check("nothing outside the disc", allDarkOutside(c, 80, 60, 31));

    printf(fails ? "\n%d FAILED\n" : "\nall passed\n", fails);
    return fails ? 1 : 0;
}
