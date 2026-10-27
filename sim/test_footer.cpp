// Host test for the scan footer: GPS reads FIXED / NO GPS, and the record
// control reads START / STOP / NO CARD with no elapsed timer. Each label is
// checked by rendering the same string into a scratch canvas and comparing the
// footer's text rows pixel for pixel, so extra or stale glyphs also fail.
#include "../ui/canvas.h"
#include "../ui/layout.h"
#include "../ui/model.h"
#include "../ui/palette.h"
#include "../ui/screens.h"
#include <cstdio>

using namespace ui;

static int fails = 0;
static void check(const char* what, bool ok) {
    printf(ok ? "  ok   %s\n" : "  FAIL %s\n", what);
    if (!ok) fails++;
}

static Canvas fc, ref;

// True when footer rows [ty, ty+8) over [x0, x1) equal the label drawn alone
// on the panel colour at (tx, ty).
static bool region(const char* label, int tx, uint16_t col, int x0, int x1) {
    const int ty = FOOT_Y + 5;
    ref.clear(pal::PANEL);
    ref.text(tx, ty, label, col);
    for (int y = ty; y < ty + 8; y++)
        for (int x = x0; x < x1; x++)
            if (fc.px[y * W + x] != ref.px[y * W + x]) return false;
    return true;
}

int main() {
    pal::setTheme(pal::TH_UMBRAVOX);
    const int rx = LOG_X, tx = rx + 12, x1 = rx + LOG_W - 2;

    State s;
    s.gpsFix = true; s.sats = 9; s.sdOk = true; s.logState = 0;
    drawFooter(fc, s);
    check("fix reads FIXED", region("FIXED", 11, pal::GREEN, 11, 80));
    check("idle reads START", region("START", tx, pal::WITCH_HI, tx, x1));

    s.gpsFix = false; s.logState = 1;
    drawFooter(fc, s);
    check("no fix reads NO GPS", region("NO GPS", 11, pal::AMBER, 11, 80));
    check("recording reads STOP (no timer)", region("STOP", tx, pal::BRIGHT, tx, x1));

    s.logState = 0; s.sdOk = false;
    drawFooter(fc, s);
    check("no card reads NO CARD", region("NO CARD", tx, pal::RED, tx, x1));

    printf(fails ? "\n%d FAILED\n" : "\nall passed\n", fails);
    return fails ? 1 : 0;
}
