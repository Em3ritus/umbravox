// Host test for the enrol / un-enrol window: when the CYD transmits, how long
// it stays on air, and -- the point -- that a RESET never abandons a net while
// boards are still answering on it.
#include "../device/enrollwin.h"
#include <cstdio>

static int fails = 0;
static void check(const char* w, bool ok) {
    printf(ok ? "  ok   %s\n" : "  FAIL %s\n", w);
    if (!ok) fails++;
}

// Step the clock by `dt` ms until `t` reaches `to`, counting transmissions.
static int run(enrollwin::State& s, uint32_t& t, uint32_t to, bool heard, bool* done = nullptr,
               bool* gaveUp = nullptr) {
    int sends = 0;
    while (t < to) {
        t += 5;
        const enrollwin::Tick k = enrollwin::tick(s, t, heard);
        if (k.send) sends++;
        if (k.done && done) *done = true;
        if (k.gaveUp && gaveUp) *gaveUp = true;
        if (k.done) break;
    }
    return sends;
}

int main() {
    using namespace enrollwin;

    printf("adopt window:\n");
    {
        State s; uint32_t t = 1000;
        check("adopt starts", adopt(s, t));
        bool done = false;
        const int sends = run(s, t, 1000 + ADOPT_MS + 100, false, &done);
        check("adopt ends after its window", done && s.mode == IDLE);
        // Boards listen for tens of ms per cycle, so it must transmit far
        // faster than once a second: >= 20 frames/s over 60 s.
        check("adopt transmits fast (>=1200 frames in 60 s)", sends >= 1200);
    }

    printf("un-enrol runs until the fleet is quiet:\n");
    {
        State s; uint32_t t = 0;
        check("unenrol starts", unenrol(s, t));
        bool done = false;
        run(s, t, UNENROL_MIN_MS - 100, false, &done);
        check("never ends before the minimum on-air time", !done);
        run(s, t, UNENROL_MIN_MS + 200, false, &done);
        check("ends at the minimum when nothing ever answered", done);
    }
    {
        State s; uint32_t t = 0;
        unenrol(s, t);
        bool done = false;
        run(s, t, 30000, true, &done);                 // boards answering for 30 s
        check("still on air while boards answer", !done);
        run(s, t, 30000 + UNENROL_QUIET_MS - 100, false, &done);
        check("stays through the quiet period after the last answer", !done);
        run(s, t, 30000 + UNENROL_QUIET_MS + 200, false, &done);
        check("ends once the fleet has been quiet long enough", done);
    }
    {
        State s; uint32_t t = 0;
        unenrol(s, t);
        bool done = false, gaveUp = false;
        run(s, t, UNENROL_MAX_MS + 500, true, &done, &gaveUp);   // boards never leave
        check("gives up at the hard cap", done);
        check("...and reports that boards were still answering", gaveUp);
    }

    printf("a running reset cannot be disturbed:\n");
    {
        State s; uint32_t t = 0;
        unenrol(s, t);
        check("a second RESET tap is ignored", !unenrol(s, t + 500));
        check("an ENROLL tap during a reset is ignored", !adopt(s, t + 600));
        check("still un-enrolling", s.mode == UNENROL);
    }
    {
        State s; uint32_t t = 0;
        adopt(s, t);
        check("RESET may interrupt an adopt window", unenrol(s, t + 500) && s.mode == UNENROL);
    }

    printf("countdown:\n");
    {
        State s; uint32_t t = 0;
        adopt(s, t);
        check("secsLeft counts a fresh adopt window", secsLeft(s, 0) == (int)(ADOPT_MS / 1000));
        check("secsLeft is 0 when idle", secsLeft(State(), 0) == 0);
    }

    printf(fails ? "\n%d FAILED\n" : "\nall passed\n", fails);
    return fails ? 1 : 0;
}
