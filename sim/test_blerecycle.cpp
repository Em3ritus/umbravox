// Host tests for the BLE scan recycle decision (device/blerecycle.h).
//
// The field failure: the Arduino BLEScan keeps every distinct advertiser in a
// map for as long as the scan runs, and blescan's scan never ends, so internal
// heap fell from ~64 KB to ~16 KB and BLE counting stopped for the rest of the
// boot. Recycling the scan (stop + start, which clears the map) bounds it, and
// a scan found stopped is started again.
#include "../device/blerecycle.h"
#include <stdio.h>

static int fails = 0;
static void check(const char* what, bool ok) {
    printf(ok ? "  ok   %s\n" : "  FAIL %s\n", what);
    if (!ok) fails++;
}

int main() {
    using namespace blerecycle;

    printf("recycle on time:\n");
    check("fresh scan, few results: nothing",       shouldRecycle(3, 1000, 0, true, 30) == NONE);
    check("just before RECYCLE_MS: nothing",        shouldRecycle(3, RECYCLE_MS - 1, 0, true, 30) == NONE);
    check("at RECYCLE_MS: recycle",                 shouldRecycle(3, RECYCLE_MS, 0, true, 30) == RECYCLE);
    check("long after: recycle",                    shouldRecycle(0, 100000, 0, true, 30) == RECYCLE);

    printf("recycle on count:\n");
    check("MAX_RESULTS results: not yet",           shouldRecycle(MAX_RESULTS, 5000, 0, true, 30) == NONE);
    check("one more than MAX_RESULTS: recycle",     shouldRecycle(MAX_RESULTS + 1, 5000, 0, true, 30) == RECYCLE);
    check("but not twice inside MIN_GAP_MS",        shouldRecycle(500, 5000 + MIN_GAP_MS - 1, 5000, true, 30) == NONE);
    check("again once MIN_GAP_MS has passed",       shouldRecycle(500, 5000 + MIN_GAP_MS, 5000, true, 30) == RECYCLE);

    printf("restart a stopped scan:\n");
    check("stopped, inside RESTART_MS: wait",       shouldRecycle(0, 4999, 0, false, 30) == NONE);
    check("stopped, at RESTART_MS: restart",        shouldRecycle(0, RESTART_MS, 0, false, 30) == RESTART);
    check("stopped with a full map: still RESTART", shouldRecycle(900, 20000, 0, false, 30) == RESTART);

    printf("duty 0 (BLE handed to Wi-Fi):\n");
    check("never recycles",                         shouldRecycle(900, 100000, 0, true, 0) == NONE);
    check("never restarts a scan it stopped",       shouldRecycle(0, 100000, 0, false, 0) == NONE);

    printf("millis wrap:\n");
    check("recycle 12 s after, across the wrap",
          shouldRecycle(0, 0xFFFFF000u + RECYCLE_MS, 0xFFFFF000u, true, 30) == RECYCLE);
    check("nothing 1 s after, across the wrap",
          shouldRecycle(0, 0xFFFFF000u + 1000u + 0x2000u, 0xFFFFF000u + 0x2000u, true, 30) == NONE);

    printf(fails ? "\n%d FAILED\n" : "\nall passed\n", fails);
    return fails ? 1 : 0;
}
