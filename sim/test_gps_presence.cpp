// Host tests for the LC76G presence decision (device/gps_presence.h): a board
// with no GPS is called absent by its first check and only ever probed
// quietly; a GPS that has answered keeps the 3 s grace it always had.
#include "../device/gps_presence.h"
#include <stdio.h>

static int fails = 0;

static void eq(const char* what, long got, long want) {
    if (got != want) { printf("  FAIL %-40s got %ld want %ld\n", what, got, want); fails++; }
    else             { printf("  ok   %-40s %ld\n", what, got); }
}

int main() {
    using namespace gpspresence;
    const uint32_t AFTER = 24;   // engine build: 3000 ms / 125 ms

    printf("no GPS on the board:\n");
    {
        Tracker t;
        eq("first check is quiet", t.quiet(), 1);
        eq("first miss: absent at once", t.onPoll(false, AFTER), WENT_ABSENT);
        eq("absent", t.absent, 1);
        int said = 0, loud = 0;
        for (int i = 0; i < 1000; i++) {
            if (!t.quiet()) loud++;
            if (t.onPoll(false, AFTER) != NONE) said++;
        }
        eq("never says it again", said, 0);
        eq("never a logged write", loud, 0);
    }

    printf("GPS there from boot:\n");
    {
        Tracker t;
        eq("first answer: nothing to say", t.onPoll(true, AFTER), NONE);
        eq("present", t.absent, 0);
        eq("polls are the normal kind", t.quiet(), 0);
        int said = 0;
        for (int i = 0; i < 1000; i++) if (t.onPoll(true, AFTER) != NONE) said++;
        eq("stays silent", said, 0);
    }

    printf("GPS misses polls after answering:\n");
    {
        Tracker t;
        t.onPoll(true, AFTER);
        int said = 0;
        for (uint32_t i = 0; i < AFTER - 1; i++) if (t.onPoll(false, AFTER) != NONE) said++;
        eq("23 misses: still present", said, 0);
        eq("not quiet yet", t.quiet(), 0);
        eq("an answer clears the run", t.onPoll(true, AFTER), NONE);
        for (uint32_t i = 0; i < AFTER - 1; i++) if (t.onPoll(false, AFTER) != NONE) said++;
        eq("23 more: still present", said, 0);
        eq("24th in a row: absent", t.onPoll(false, AFTER), WENT_ABSENT);
        eq("now quiet", t.quiet(), 1);
    }

    printf("GPS plugged in after boot:\n");
    {
        Tracker t;
        t.onPoll(false, AFTER);
        t.onPoll(false, AFTER);
        eq("answers: came back", t.onPoll(true, AFTER), CAME_BACK);
        eq("present", t.absent, 0);
        eq("normal polls again", t.quiet(), 0);
        eq("one miss does not drop it", t.onPoll(false, AFTER), NONE);
        eq("still present", t.absent, 0);
    }

    printf("%s\n", fails ? "FAILED" : "all ok");
    return fails ? 1 : 0;
}
