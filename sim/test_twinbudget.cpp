// Host tests for the evil-twin pass budget (device/twinbudget.h).
//
// The pass compares each new named, encrypted AP with every AP in the table,
// with interrupts off: ~20-40 ms at 20,000 entries. A beacon-spam flood of
// hundreds of fake SSIDs a second would saturate the Wi-Fi task. A token
// bucket of entry visits skips the pass under such a flood (counted) and
// leaves normal driving alone.
#include "../device/twinbudget.h"
#include <stdio.h>

static int fails = 0;
static void check(const char* what, bool ok) {
    printf(ok ? "  ok   %s\n" : "  FAIL %s\n", what);
    if (!ok) fails++;
}

int main() {
    using namespace twinbudget;

    printf("normal driving:\n");
    {
        // 2 new named APs a second against a 20,000-entry table, 10 minutes.
        Bucket b;
        int skipped = 0;
        for (uint32_t t = 1000; t < 1000 + 600000; t += 500)
            if (!b.take(t, 20000)) skipped++;
        check("2/s at 20,000 entries for 10 min: never skipped", skipped == 0);
        Bucket s;
        int sk = 0;
        for (uint32_t t = 0; t < 600000; t += 100)                 // 10/s, small table
            if (!s.take(t, 3000)) sk++;
        check("10/s at 3,000 entries: never skipped", sk == 0);
        Bucket full;
        check("a pass over a full 32,768-entry table fits the burst", full.take(0, 32768));
    }

    printf("flood:\n");
    {
        // 300 fake SSIDs a second at 20,000 entries for 10 s.
        Bucket b;
        int ran = 0, skipped = 0;
        for (uint32_t i = 0; i < 3000; i++) {
            const uint32_t t = 5000 + i * 10 / 3;
            if (b.take(t, 20000)) ran++; else skipped++;
        }
        printf("       ran %d, skipped %d\n", ran, skipped);
        check("most passes are skipped",                 skipped > 2800);
        check("work stays within burst + rate x time",
              (uint64_t)ran * 20000 <= BURST + (uint64_t)RATE_PER_MS * 10000);
    }

    printf("refill:\n");
    {
        Bucket b;
        for (int g = 0; g < 100 && b.take(0, 20000); g++) {}
        check("drained: the next pass is skipped",      !b.take(0, 20000));
        check("300 ms later: still short",                !b.take(300, 20000));
        check("after enough quiet: runs again",          b.take(600, 20000));
        check("an empty table costs nothing",            b.take(600, 0));
    }

    printf("millis wrap:\n");
    {
        Bucket b;
        b.take(0xFFFFFF00u, 0);
        for (int g = 0; g < 100 && b.take(0xFFFFFF00u, 20000); g++) {}
        check("drained just before the wrap",            !b.take(0xFFFFFF00u, 20000));
        check("refills across the wrap (1 s later)",      b.take(0xFFFFFF00u + 1000u, 20000));
        Bucket c;
        c.take(0, 0);
        for (int g = 0; g < 100 && c.take(0, 20000); g++) {}
        check("a very long quiet caps at the burst, no overflow",
              c.take(0xF0000000u, BURST) && !c.take(0xF0000000u, 1));
    }

    printf("chunking:\n");
    check("lock released every CHUNK visits",      chunkDue(CHUNK - 1) && !chunkDue(0) && !chunkDue(CHUNK - 2));
    check("and again a CHUNK later",               chunkDue(2 * CHUNK - 1));

    printf(fails ? "\n%d FAILED\n" : "\nall passed\n", fails);
    return fails ? 1 : 0;
}
