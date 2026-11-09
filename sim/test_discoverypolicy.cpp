// Host test for the recording-time discovery policy (device/discoverypolicy.h).
//
// Field report: a drive with the radio-hungry active discovery on heard ~100x
// fewer BLE adverts than before. While a session is recording the unit stays
// passive; the saved setting is untouched and applies again after STOP.
#include "../device/discoverypolicy.h"
#include <stdio.h>

static int fails = 0;
static void check(const char* what, bool ok) {
    printf(ok ? "  ok   %s\n" : "  FAIL %s\n", what);
    if (!ok) fails++;
}

int main() {
    using discoverypolicy::effective;

    printf("effective discovery:\n");
    check("off stays off, idle",                effective(0, false) == 0);
    check("off stays off, recording",           effective(0, true)  == 0);
    check("on when idle",                       effective(1, false) == 1);
    check("forced passive while recording",     effective(1, true)  == 0);
    check("back on after the session stops",    effective(1, false) == 1);
    check("a nonzero setting counts as on",     effective(2, false) == 1);

    printf(fails ? "\n%d FAILED\n" : "\nPASSED\n", fails);
    return fails ? 1 : 0;
}
