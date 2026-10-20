// The two decisions behind the collector-mode node-migration fix, host-tested
// without a radio. The bug was: a node latched its Core on the first Assign and
// never re-homed, so the collector's redirect to a coordinator was ignored and
// the node stayed stuck on the CYD. These pins the corrected logic.
#include "../device/rehome.h"
#include <stdio.h>

static int fails = 0;
static void check(const char* what, bool ok) {
    printf(ok ? "  ok   %s\n" : "  FAIL %s\n", what);
    if (!ok) fails++;
}

int main() {
    const uint8_t cyd[6]   = {0xA0, 0xB1, 0xC2, 0xD3, 0xE4, 0xF5};
    const uint8_t coord[6] = {0x10, 0xBD, 0xA3, 0xF3, 0xA4, 0x38};
    const uint8_t cydAgain[6] = {0xA0, 0xB1, 0xC2, 0xD3, 0xE4, 0xF5};

    printf("re-home decision:\n");
    check("no core yet -> BIND",
          rehome::decide(false, cyd, coord) == rehome::BIND);
    check("same core -> UPDATE (refreshed assignment)",
          rehome::decide(true, cyd, cydAgain) == rehome::UPDATE);
    check("different core -> REHOME (handed off CYD->coordinator)",
          rehome::decide(true, cyd, coord) == rehome::REHOME);
    // The exact bug: a node already bound to the CYD, told to go to a
    // coordinator, must re-home -- not silently keep the CYD.
    check("stuck-on-CYD node migrates when redirected",
          rehome::decide(true, cyd, coord) != rehome::UPDATE);

    printf("straggler redirect gate:\n");
    check("inactive collector never redirects",
          !straggler::shouldRedirect(false, 10000, 0, 400));
    check("active + past cooldown redirects",
          straggler::shouldRedirect(true, 10000, 9000, 400));
    check("active + within cooldown holds off",
          !straggler::shouldRedirect(true, 10000, 9800, 400));
    check("exactly at the cooldown boundary fires",
          straggler::shouldRedirect(true, 10400, 10000, 400));

    printf(fails ? "\n%d FAILED\n" : "\nall passed\n", fails);
    return fails ? 1 : 0;
}
