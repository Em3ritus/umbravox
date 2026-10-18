// Host tests for tap routing on the scan screen under each theme.
//
// The dial and spectrum geometry is per-theme and runtime, and nav.cpp reads
// it by the same names the drawing code does. A theme with no spectrum
// (SPEC_H == 0) must not route a tap in the old strip to MESH.
#include "../ui/nav.h"
#include "../ui/layout.h"
#include "../ui/palette.h"
#include <stdio.h>

static int fails = 0;

static void check(const char* what, bool ok) {
    printf(ok ? "  ok   %s\n" : "  FAIL %s\n", what);
    if (!ok) fails++;
}

using namespace ui;

// A tap is a press then a release; the release carries the last position seen.
static Nav tapAt(int x, int y) {
    Nav n;
    Settings s;
    navTouch(n, s, true,  (int16_t)x, (int16_t)y);
    navTouch(n, s, false, (int16_t)x, (int16_t)y);
    return n;
}

int main() {
    const int specX = COL_R_X + COL_R_W / 2;

    pal::setTheme(pal::TH_AETHERSCAN);
    check("theme 0 has a spectrum strip", SPEC_H > 0);
    {
        Nav n = tapAt(specX, SPEC_Y + SPEC_H / 2);
        check("theme 0: spectrum tap opens MESH", n.screen == SCR_MESH);
    }
    {
        Nav n = tapAt(DIAL_CX, DIAL_CY);
        check("theme 0: dial tap opens DOMAIN", n.screen == SCR_DOMAIN);
    }
    const int oldSpecY = SPEC_Y + SPEC_H / 2;   // theme 0's strip, for theme 1

    pal::setTheme(pal::TH_BAPH0MAU5);
    check("theme 1 has no spectrum strip", SPEC_H == 0);
    {
        Nav n = tapAt(specX, oldSpecY);
        check("theme 1: old spectrum coordinates do not open MESH", n.screen != SCR_MESH);
    }
    {
        Nav n = tapAt(DIAL_CX, DIAL_CY);
        check("theme 1: dial tap opens DOMAIN", n.screen == SCR_DOMAIN);
    }

    const uint8_t want[3] = {F_WIFI, F_5G, F_BLE};
    for (int t = 0; t < 2; t++) {
        pal::setTheme((uint8_t)t);
        for (int i = 0; i < 3; i++) {
            Nav n = tapAt(COL_L_X + COL_L_W / 2, TILE_Y[i] + TILE_H / 2);
            char buf[64];
            snprintf(buf, sizeof buf, "theme %d: tile %d opens CODEX with its filter", t, i);
            check(buf, n.screen == SCR_CODEX && n.filter == want[i]);
        }
    }

    printf(fails ? "\n%d FAILED\n" : "\nall passed\n", fails);
    return fails ? 1 : 0;
}
