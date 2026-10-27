// Host tests for the pure parts of elog: the line formatter and the rotation
// decision. The SD-touching begin()/event()/dump() live in elog.cpp and are
// device-only, so they are not built or exercised here.
#include "../device/elog.h"
#include <stdio.h>
#include <string.h>

static int fails = 0;
static void check(const char* what, bool ok) {
    printf(ok ? "  ok   %s\n" : "  FAIL %s\n", what);
    if (!ok) fails++;
}

int main() {
    using namespace elog;
    char b[256];

    printf("format:\n");
    int n = format(b, sizeof b, 120, INFO, "boot", "hello");
    check("INFO line",     !strcmp(b, "0000000120 INFO boot: hello\n"));
    check("length == strlen", n == (int)strlen(b));
    format(b, sizeof b, 0, ERR, "sd", "write failed");
    check("ERR padded",    !strcmp(b, "0000000000 ERR  sd: write failed\n"));
    format(b, sizeof b, 61200, WARN, "heap", "low, 38KB free");
    check("WARN line",     !strcmp(b, "0000061200 WARN heap: low, 38KB free\n"));
    format(b, sizeof b, 4294967295u, INFO, "t", "x");
    check("max uptime, 10 digits", !strncmp(b, "4294967295 ", 11));

    printf("defensive:\n");
    format(b, sizeof b, 1, INFO, nullptr, nullptr);
    check("null tag/msg safe", !strcmp(b, "0000000001 INFO ?: \n"));
    n = format(b, 8, 1, INFO, "boot", "truncate me");
    check("tiny buffer stays in bounds", n == 7 && b[7] == '\0' && strlen(b) == 7);
    check("zero-size buffer -> 0", format(b, 0, 1, INFO, "x", "y") == 0);

    printf("levels + rotation:\n");
    check("levelName INFO", !strcmp(levelName(INFO), "INFO"));
    check("levelName WARN", !strcmp(levelName(WARN), "WARN"));
    check("levelName ERR ", !strcmp(levelName(ERR),  "ERR "));
    check("under cap: keep",        !shouldRotate(100, 50, 1000));
    check("exactly at cap: keep",   !shouldRotate(900, 100, 1000));
    check("one past cap: rotate",    shouldRotate(901, 100, 1000));
    // 64-bit math: a near-full counter must not wrap and skip a rotation.
    check("near-full no wrap",       shouldRotate(0xFFFFFF00u, 0x200, CAP));

    printf("rate-limit gate:\n");
    {
        Gate g;
        check("the first event always passes",    g.allow(1000, 30000));
        check("inside the gap: held",             !g.allow(20000, 30000));
        check("held again just before the gap",   !g.allow(30999, 30000));
        check("at the gap: passes",               g.allow(31000, 30000));
        check("the gap restarts from that one",   !g.allow(40000, 30000));
        Gate z;
        check("first event at t=0 passes",        z.allow(0, 30000));
        check("and holds the next at t=10",       !z.allow(10, 30000));
        Gate w; w.allow(0xFFFFF000u, 30000);
        check("held across the millis wrap",      !w.allow(0x00001000u, 30000));
        check("passes 30 s after, across the wrap", w.allow(0xFFFFF000u + 30000u, 30000));
    }

    printf(fails ? "\n%d FAILED\n" : "\nall passed\n", fails);
    return fails ? 1 : 0;
}
