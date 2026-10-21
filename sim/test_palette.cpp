// Host tests for the theme table: a theme is a row, and adding one must not
// break the fallback or the sweep offering.
#include "../ui/palette.h"
#include <cstring>
#include <cstdio>

static int fails = 0;
static void check(const char* what, bool ok) {
    printf(ok ? "  ok   %s\n" : "  FAIL %s\n", what);
    if (!ok) fails++;
}

int main() {
    check("three themes", pal::TH_COUNT == 3);
    check("cosmic name", !strcmp(pal::themeName(pal::TH_COSMICMUFFIN), "COSMIC MUFFIN"));

    pal::setTheme(pal::TH_COSMICMUFFIN);
    check("neon void applied", pal::VOID == pal::rgb(0x09, 0x07, 0x1C));
    check("neon magenta applied", pal::MAGENTA == pal::rgb(0xFF, 0x1F, 0xCC));
    // Every sweep but the gaze (0x7FD): the muffin has no eyes. The mask must
    // be wide enough to hold bit 10 -- an 8-bit mask would drop the top sweeps.
    check("cosmic offers every sweep but gaze", pal::sweepMask(pal::TH_COSMICMUFFIN) == 0x07FD);

    pal::setTheme(99);   // out of range, from a corrupt/stale NVS value
    check("bad theme id falls back to UMBRAVOX", pal::VOID == pal::rgb(0x07, 0x04, 0x0E));

    printf(fails ? "\n%d FAILED\n" : "\nall passed\n", fails);
    return fails ? 1 : 0;
}
