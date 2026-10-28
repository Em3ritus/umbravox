// Host tests for the canvas's runtime geometry (portrait mode).
//
// The buffer is the same 19,200 pixels in both orientations; only the stride
// and the clip change. A helper that strides by the landscape W while the
// canvas is 120 wide would wrap a line off the right edge onto the next row,
// which is exactly the bug these look for.
#include "../ui/canvas.h"
#include "../ui/font5x7.h"
#include <stdio.h>
#include <string.h>

static int fails = 0;

static void check(const char* what, bool ok) {
    printf(ok ? "  ok   %s\n" : "  FAIL %s\n", what);
    if (!ok) fails++;
}

using namespace ui;

// The canvas with a guard band either side, so a write outside the buffer
// (not just outside w x h) is seen too.
struct Guarded {
    uint16_t pre[256];
    Canvas c;
    uint16_t post[256];
};
static constexpr uint16_t SENT = 0xA5A5;

static bool guardsIntact(const Guarded& g) {
    for (int i = 0; i < 256; i++)
        if (g.pre[i] != SENT || g.post[i] != SENT) return false;
    return true;
}

int main() {
    printf("geometry:\n");
    {
        static Canvas c;
        check("a new canvas is the landscape 160x120", c.w == W && c.h == H && W == 160 && H == 120);
        check("portrait fits the same buffer", c.setSize(120, 160) && c.w == 120 && c.h == 160);
        check("a size larger than the buffer is refused", !c.setSize(160, 160) && c.w == 120);
        check("a non-positive size is refused", !c.setSize(0, 10) && !c.setSize(10, -1));
        check("back to landscape", c.setSize(W, H) && c.w == W && c.h == H);
    }

    printf("portrait stride and clip:\n");
    {
        static Canvas c;
        c.setSize(120, 160);
        c.clear(0);
        c.pixel(119, 159, 7);
        check("last pixel is at index 159*120+119", c.px[159 * 120 + 119] == 7);
        c.pixel(5, 1, 9);
        check("(5,1) is at index 125 (stride 120)", c.px[125] == 9);
        c.clear(0);
        c.hline(100, 10, 50, 1);        // runs 30 px off the right edge
        bool wrapped = false;
        for (int x = 0; x < 30; x++) if (c.px[11 * 120 + x]) wrapped = true;
        check("a line off the right edge does not wrap onto the next row", !wrapped);
        check("...and is drawn up to the edge", c.px[10 * 120 + 119] == 1 && c.px[10 * 120 + 100] == 1);
        c.clear(0);
        c.pixel(120, 0, 1);
        c.pixel(0, 160, 1);
        c.pixel(-1, 5, 1);
        int n = 0;
        for (int i = 0; i < W * H; i++) n += c.px[i] != 0;
        check("pixels just outside 120x160 are dropped", n == 0);
        c.clear(0);
        c.text(110, 0, "WWWW", 1);      // runs off the right edge
        wrapped = false;
        for (int y = 1; y < 9; y++) for (int x = 0; x < 20; x++) if (c.px[y * 120 + x]) wrapped = true;
        check("text off the right edge does not wrap", !wrapped);
        c.clear(0);
        c.fill(-10, 150, 200, 30, 2);   // a fill hanging off three edges
        bool ok = true;
        for (int y = 0; y < 160; y++)
            for (int x = 0; x < 120; x++)
                if ((c.px[y * 120 + x] == 2) != (y >= 150)) ok = false;
        check("a fill off three edges covers exactly the visible part", ok);
    }

    printf("whole-frame passes cover exactly w x h:\n");
    {
        static Canvas c;
        c.setSize(120, 160);
        c.clear(0xFFFF);
        c.scanlines();
        bool ok = true;
        for (int y = 0; y < 160; y++)
            for (int x = 0; x < 120; x++)
                if ((c.px[y * 120 + x] == 0xFFFF) != (y % 2 == 0)) ok = false;
        check("scanlines darken every odd row of 160", ok);
        c.clear(0xFFFF);
        c.vignette();
        check("vignette keeps the centre", c.px[80 * 120 + 60] == 0xFFFF);
        check("vignette darkens all four corners",
              c.px[0] != 0xFFFF && c.px[119] != 0xFFFF &&
              c.px[159 * 120] != 0xFFFF && c.px[159 * 120 + 119] != 0xFFFF);
        check("vignette is symmetric about x = w/2",
              c.px[10 * 120 + 4] == c.px[10 * 120 + 116]);
    }

    printf("guard bands survive shapes off every edge:\n");
    for (int o = 0; o < 2; o++) {
        static Guarded g;
        for (int i = 0; i < 256; i++) g.pre[i] = g.post[i] = SENT;
        if (o) g.c.setSize(120, 160); else g.c.setSize(W, H);
        g.c.clear(0);
        g.c.circle(0, 0, 300, 1);
        g.c.disc(g.c.w, g.c.h, 40, 1);
        g.c.line(-500, -500, 500, 500, 1);
        g.c.rect(-5, -5, 400, 400, 1);
        g.c.dottedRect(-3, -3, 300, 300, 1);
        g.c.dottedCircle(g.c.w / 2, g.c.h / 2, 200, 1);
        g.c.text(-30, g.c.h - 3, "OUT OF BOUNDS", 1, 3);
        g.c.textRight(g.c.w + 40, -4, "EDGE", 1, 2);
        g.c.vignette();
        g.c.scanlines();
        check(o ? "portrait: guards intact" : "landscape: guards intact", guardsIntact(g));
    }

    printf(fails ? "\n%d failed\n" : "\nall passed\n", fails);
    return fails ? 1 : 0;
}
