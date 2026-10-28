// Host tests for the GPS bring-up hints (device/gpshint.h): one line when the
// UART has heard nothing 10 s after boot, one when bytes arrive but none of
// them parse (the baud is wrong). Each at most once per boot.
#include "../device/gpshint.h"
#include <stdio.h>

static int fails = 0;

static void eq(const char* what, long got, long want) {
    if (got != want) { printf("  FAIL %-34s got %ld want %ld\n", what, got, want); fails++; }
    else             { printf("  ok   %-34s %ld\n", what, got); }
}

int main() {
    using namespace gpshint;

    printf("no data:\n");
    {
        Watch w;
        eq("quiet before 10 s", w.check(9999, 0, 0), NONE);
        eq("at 10 s: no data", w.check(10000, 0, 0), NO_DATA);
        eq("said once", w.check(11000, 0, 0), NONE);
        eq("still once an hour on", w.check(3600000, 0, 0), NONE);
    }

    printf("healthy receiver:\n");
    {
        Watch w;
        eq("bytes and good: nothing", w.check(10000, 5000, 40), NONE);
        eq("later: nothing", w.check(60000, 50000, 400), NONE);
    }

    printf("baud mismatch:\n");
    {
        Watch w;
        eq("a few bytes, none good: wait", w.check(2000, NO_PARSE_BYTES - 1, 0), NONE);
        eq("enough bytes, none good: hint", w.check(3000, NO_PARSE_BYTES, 0), NO_PARSE);
        eq("said once", w.check(4000, 5000, 0), NONE);
        eq("not also 'no data'", w.check(10000, 5000, 0), NONE);
    }

    printf("wired late:\n");
    {
        Watch w;
        eq("no data at 10 s", w.check(10000, 0, 0), NO_DATA);
        eq("then garbage: baud hint", w.check(20000, NO_PARSE_BYTES, 0), NO_PARSE);
        eq("then nothing more", w.check(30000, 9000, 0), NONE);
    }

    printf("one good line is enough:\n");
    {
        Watch w;
        eq("lots of bytes, one good", w.check(10000, 20000, 1), NONE);
    }

    printf("first check long after boot (late start, no wrap logic):\n");
    {
        Watch w;
        eq("first check at ~49.7 days: no data", w.check(0xFFFFFF00u, 0, 0), NO_DATA);
        eq("after millis wraps: not said again", w.check(5000, 0, 0), NONE);
    }

    printf(fails ? "\n%d FAILED\n" : "\nall passed\n", fails);
    return fails ? 1 : 0;
}
