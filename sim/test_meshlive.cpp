// Host tests for the node-liveness and camp decisions (device/meshlive.h).
//
// Field data: a node with nothing queued sends nothing, the Core expired it
// after 20 s, and when the last node expired the Core went back to sweeping,
// so the nodes' reports (on channel 1) stopped landing and recovery took
// 30-310 s. The timeout is now 75 s and the Core holds channel 1 for 120 s
// after the last node; no new transmissions.
#include "../device/meshlive.h"
#include <stdio.h>

static int fails = 0;
static void check(const char* what, bool ok) {
    printf(ok ? "  ok   %s\n" : "  FAIL %s\n", what);
    if (!ok) fails++;
}

int main() {
    using namespace meshlive;

    printf("constants:\n");
    check("node timeout is 75 s",          NODE_TIMEOUT_MS == 75000);
    check("camp hold is 120 s",            CAMP_HOLD_MS == 120000);
    check("enrol 'heard' window stays 20 s", BOARD_QUIET_MS == 20000);

    printf("expired:\n");
    check("fresh: not expired",                 !expired(10000, 9000, NODE_TIMEOUT_MS));
    check("exactly at the timeout: not yet",    !expired(75000, 0, NODE_TIMEOUT_MS));
    check("one past the timeout: expired",       expired(75001, 0, NODE_TIMEOUT_MS));
    // The receive callback can stamp lastMs just after the loop sampled now.
    check("lastMs a moment after now: not expired (the old compare wrapped)",
          !expired(5000, 5003, NODE_TIMEOUT_MS));
    check("across the millis wrap, 30 s old: live",
          !expired(0x00002000u, 0xFFFFF000u - 26000u, NODE_TIMEOUT_MS));
    check("across the millis wrap, 80 s old: expired",
          expired(0x00002000u, 0x00002000u - 80000u, NODE_TIMEOUT_MS));

    printf("node age:\n");
    check("10 s old reads 10000",               ageMs(20000, 10000) == 10000);
    check("stamped just after now reads 0, not ~49 days", ageMs(5000, 5003) == 0);
    check("across the millis wrap",             ageMs(0x00001000u, 0xFFFFF000u) == 0x2000u);

    printf("camp hold:\n");
    {
        CampHold h;
        check("boot, no nodes: sweep (no hold before any node)", !wantCamp(h, 0, false, false, 1000));
        check("a node: camp",                     wantCamp(h, 2, false, false, 5000));
        check("last node gone: still camped",     wantCamp(h, 0, false, false, 6000));
        check("119.999 s later: still camped",    wantCamp(h, 0, false, false, 5000 + CAMP_HOLD_MS - 1));
        check("120 s after the last node: sweep", !wantCamp(h, 0, false, false, 5000 + CAMP_HOLD_MS));
        check("and the hold is spent",            !h.held);
        check("not back on when millis comes round again",
              !wantCamp(h, 0, false, false, 5000 + 10));
        check("a node returns: camp again",       wantCamp(h, 1, false, false, 900000));
        check("hold restarts from it",            wantCamp(h, 0, false, false, 900000 + CAMP_HOLD_MS - 1));
    }
    {
        CampHold h;
        wantCamp(h, 1, false, false, 0xFFFFF000u);
        check("hold across the millis wrap",      wantCamp(h, 0, false, false, 0x00010000u));
        check("ends 120 s after, across the wrap", !wantCamp(h, 0, false, false, 0xFFFFF000u + CAMP_HOLD_MS));
    }

    printf("coordinator and enrolment (unchanged):\n");
    {
        CampHold h;
        check("coordinator live: camp",           wantCamp(h, 0, true, false, 1000));
        check("enrol window open: camp",          wantCamp(h, 0, false, true, 1000));
        check("neither, no nodes ever: sweep",    !wantCamp(h, 0, false, false, 2000));
        check("coordinator live alone sets no node hold", !h.held);
    }

    printf(fails ? "\n%d FAILED\n" : "\nall passed\n", fails);
    return fails ? 1 : 0;
}
