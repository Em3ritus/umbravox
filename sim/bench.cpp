// Draw-time benchmark: the landscape screens rendered many times, timed with
// clock_gettime. Host timing is only a proxy for the C5's single RISC-V core,
// but a regression in the hot Canvas helpers shows up here as a ratio.
//
//   make -C sim bench          (not part of `make test`)
//
// Uses only the landscape entry points (drawScan, drawGrimoire...), so the
// same file builds against an older ui/ for a before/after comparison.
#include "../ui/screens.h"
#include "../ui/demo.h"
#include "../ui/palette.h"
#include "packload.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <algorithm>

namespace {

double nowUs() {
    timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1e6 + t.tv_nsec / 1e3;
}

// Median of `reps` batches of `n` frames, in microseconds per frame.
template <class F>
double timeIt(F draw, int n, int reps) {
    double best[32];
    for (int r = 0; r < reps; r++) {
        const double t0 = nowUs();
        for (int i = 0; i < n; i++) draw(i);
        best[r] = (nowUs() - t0) / n;
    }
    std::sort(best, best + reps);
    return best[reps / 2];
}

} // namespace

int main(int argc, char** argv) {
    const int n = argc > 1 ? atoi(argv[1]) : 2000;
    static ui::Canvas c;
    char why[128];
    const int pack = packload::load("packs/cosmic-clone", why, sizeof why);
    struct Case { const char* name; int theme; uint8_t screen; bool alert; };
    const Case cases[] = {
        {"scan umbravox",        0,    0, false},
        {"scan baph0mau5",       1,    0, false},
        {"scan cosmic (emblem)", 2,    0, false},
        {"scan pack emblem",     pack, 0, false},
        {"scan cosmic alert",    2,    0, true},
        {"grimoire umbravox",    0,    7, false},
        {"attune umbravox",      0,    8, false},
    };
    double total = 0;
    uint32_t sink = 0;
    for (const Case& k : cases) {
        if (k.theme < 0) continue;
        pal::setTheme((uint8_t)k.theme);
        ui::State s = ui::demoState(0);
        s.set.theme = (uint8_t)k.theme;
        s.screen = k.screen;
        if (k.alert) {
            s.alertActive = true; s.alertThreat = 1; s.alertConf = 2;
            strcpy(s.alertWhy, "DRONEID");
        }
        const double us = timeIt([&](int i) {
            s.frame = (uint32_t)i;
            s.alertAge = (uint16_t)((i * 20) % 2000);
            if (k.screen == 7) ui::drawGrimoire(c, s);
            else if (k.screen == 8) ui::drawAttune(c, s);
            else ui::drawScan(c, s);
            sink += c.px[(i * 37) % (ui::W * ui::H)];
        }, n / 10 > 0 ? n / 10 : 1, 11);
        total += us;
        printf("  %-22s %8.1f us/frame\n", k.name, us);
    }
    printf("  %-22s %8.1f us (sum)   [sink %u]\n", "TOTAL", total, sink & 1);
    return 0;
}
