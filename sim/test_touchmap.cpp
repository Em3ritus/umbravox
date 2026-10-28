// Host tests for device/touchmap.h: the XPT2046 point, measured in the
// landscape frame it was calibrated in, carried through the panel's native
// frame to whichever rotation the display is in now.
#include "../device/touchmap.h"
#include <stdio.h>

static int fails = 0;
static void check(const char* what, bool ok) {
    printf(ok ? "  ok   %s\n" : "  FAIL %s\n", what);
    if (!ok) fails++;
}

using namespace touchmap;

int main() {
    printf("native round trips:\n");
    {
        bool ok = true;
        for (int r = 0; r < 4; r++) {
            const int w = (r & 1) ? NH : NW, h = (r & 1) ? NW : NH;
            for (int y = 0; y < h; y += 7)
                for (int x = 0; x < w; x += 5) {
                    int nx, ny, bx, by;
                    toNative(r, x, y, nx, ny);
                    if (nx < 0 || ny < 0 || nx >= NW || ny >= NH) ok = false;
                    fromNative(r, nx, ny, bx, by);
                    if (bx != x || by != y) ok = false;
                }
        }
        check("every rotation maps onto the native panel and back", ok);
    }
    printf("canvas mapping from the landscape calibration (rotation 3):\n");
    {
        bool same = true;
        for (int y = 0; y < 240; y++)
            for (int x = 0; x < 320; x++) {
                int cx, cy;
                toCanvas(3, 3, x, y, cx, cy);
                if (cx != x / 2 || cy != y / 2) same = false;
            }
        check("landscape is unchanged: canvas = panel / 2", same);
        int cx, cy;
        // The landscape panel's corners, as the portrait (rotation 0) canvas sees them.
        toCanvas(3, 0, 0, 0, cx, cy);
        check("landscape top-left is portrait bottom-left", cx == 0 && cy == 159);
        toCanvas(3, 0, 319, 0, cx, cy);
        check("landscape top-right is portrait top-left", cx == 0 && cy == 0);
        toCanvas(3, 0, 319, 239, cx, cy);
        check("landscape bottom-right is portrait top-right", cx == 119 && cy == 0);
        toCanvas(3, 0, 0, 239, cx, cy);
        check("landscape bottom-left is portrait bottom-right", cx == 119 && cy == 159);
        toCanvas(3, 2, 0, 0, cx, cy);
        check("rotation 2 is rotation 0 turned over", cx == 119 && cy == 0);
        bool in = true;
        for (int r = 0; r < 4; r++)
            for (int y = 0; y < 240; y += 3)
                for (int x = 0; x < 320; x += 3) {
                    toCanvas(3, r, x, y, cx, cy);
                    const int w = (r & 1) ? 160 : 120, h = (r & 1) ? 120 : 160;
                    if (cx < 0 || cy < 0 || cx >= w || cy >= h) in = false;
                }
        check("every panel point lands on the canvas in every rotation", in);
    }
    printf(fails ? "\n%d failed\n" : "\nall passed\n", fails);
    return fails ? 1 : 0;
}
