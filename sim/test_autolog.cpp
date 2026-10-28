// Host tests for the AUTO LOG decision (device/autolog.h): when the unit may
// start a logging session by itself on a GPS fix. Each rule from the design
// has its own case; the latch cases are the ones that matter, because a unit
// that restarts logging after the owner pressed STOP is worse than none.
#include "../device/autolog.h"
#include <stdio.h>

static int fails = 0;
static void check(const char* what, bool ok) {
    printf(ok ? "  ok   %s\n" : "  FAIL %s\n", what);
    if (!ok) fails++;
}

using autolog::Inputs;
using autolog::Latch;
using autolog::STABLE_MS;

// Everything in favour of starting, at `now`.
static Inputs go(uint32_t now) {
    Inputs in;
    in.enabled = true; in.logging = false; in.cardPresent = true;
    in.fixFresh = true; in.transitioning = false; in.nowMs = now;
    return in;
}

// Step every `dt` ms from `t0` to `t1` inclusive with `in` (nowMs replaced);
// returns how many steps said start, and stops counting state at t1.
static int run(Latch& l, Inputs in, uint32_t t0, uint32_t t1, uint32_t dt = 100) {
    int starts = 0;
    for (uint32_t t = t0;; t += dt) {
        in.nowMs = t;
        if (autolog::step(l, in)) starts++;
        if (t == t1 || (uint32_t)(t1 - t) < dt) break;
    }
    return starts;
}

int main() {
    printf("the default is OFF and OFF never starts:\n");
    {
        Latch l;
        Inputs in = go(0); in.enabled = false;
        check("OFF: no start in a minute of perfect fix", run(l, in, 0, 60000) == 0);
    }

    printf("a fix must hold for STABLE_MS:\n");
    {
        Latch l;
        check("STABLE_MS is 5 s", STABLE_MS == 5000);
        check("no start before 5 s of fix", run(l, go(0), 1000, 1000 + STABLE_MS - 100) == 0);
        check("starts at 5 s", autolog::step(l, go(1000 + STABLE_MS)));
        check("...exactly once", run(l, go(0), 1000 + STABLE_MS + 100, 120000) == 0);
    }
    {
        Latch l;
        run(l, go(0), 0, 4000);
        Inputs lost = go(4100); lost.fixFresh = false;
        autolog::step(l, lost);                       // fix dropped at 4.1 s
        check("a dropped fix restarts the clock", run(l, go(0), 4200, 4200 + STABLE_MS - 100) == 0);
        check("...and 5 s of the new fix starts it", autolog::step(l, go(4200 + STABLE_MS)));
    }

    printf("already logging (ACTIVE, or NO_CARD waiting for a card):\n");
    {
        Latch l;
        Inputs in = go(0); in.logging = true;
        check("no start while a session is wanted", run(l, in, 0, 30000) == 0);
    }

    printf("manual STOP latches until the next boot:\n");
    {
        Latch l;
        check("auto-start fires", run(l, go(0), 0, 6000) == 1);
        // ...the session runs, then the owner taps STOP.
        autolog::noteManualStop(l);
        check("never again this boot", run(l, go(0), 6100, 600000) == 0);
        Latch boot;                                    // a reboot: a new latch
        check("the next boot arms again", run(boot, go(0), 0, 6000) == 1);
    }
    {
        Latch l;
        Inputs in = go(0); in.logging = true;          // started by hand
        run(l, in, 0, 10000);
        autolog::noteManualStop(l);                    // and stopped by hand
        check("a hand START+STOP also latches it", run(l, go(0), 10100, 60000) == 0);
    }

    printf("toggling the setting OFF then ON re-arms:\n");
    {
        Latch l;
        run(l, go(0), 0, 6000);
        autolog::noteManualStop(l);
        Inputs off = go(6100); off.enabled = false;
        autolog::step(l, off);
        check("still latched while OFF", run(l, off, 6200, 20000) == 0);
        // ON again: the fix has been fresh all along, so it is already stable.
        check("ON again starts", run(l, go(0), 20100, 20100) == 1);
        check("...once", run(l, go(0), 20200, 60000) == 0);
    }
    {
        Latch l;
        autolog::noteManualStop(l);
        Inputs in = go(0);
        check("staying ON does not re-arm", run(l, in, 0, 60000) == 0);
    }

    printf("no card: wait, do not retry, start once when it appears:\n");
    {
        Latch l;
        Inputs in = go(0); in.cardPresent = false;
        check("no start without a card, for ten minutes", run(l, in, 0, 600000) == 0);
        check("card appears: one start", run(l, go(0), 600100, 600100) == 1);
        check("...and no more", run(l, go(0), 600200, 700000) == 0);
    }

    printf("not during a rollover / RESET:\n");
    {
        Latch l;
        Inputs in = go(0); in.transitioning = true;
        check("no start while transitioning", run(l, in, 0, 20000) == 0);
        check("starts once it completes", run(l, go(0), 20100, 20100) == 1);
    }

    printf("millis() wraps:\n");
    {
        Latch l;
        const uint32_t t0 = 0xFFFFFFFFu - 2000;        // fix 2 s before the wrap
        check("no start across the wrap before 5 s",
              run(l, go(0), t0, t0 + STABLE_MS - 100) == 0);   // t0 + 4.9 s wraps
        check("starts 5 s after, past the wrap", autolog::step(l, go(t0 + STABLE_MS)));
    }
    {
        // A fix held stable for longer than millis() takes to wrap must not
        // read as "only just fixed" again.
        Latch l;
        Inputs in = go(0); in.logging = true;          // logging, so nothing fires
        run(l, in, 0, 10000);
        in.nowMs = 0x80000000u; autolog::step(l, in);
        in.nowMs = 0xFFFFFF00u; autolog::step(l, in);
        in.nowMs = 3000;        autolog::step(l, in);  // wrapped, still the same fix
        autolog::noteManualStop(l);
        Inputs off = go(3100); off.enabled = false; autolog::step(l, off);
        check("a fix held past a wrap is still stable", autolog::step(l, go(3200)));
    }

    printf(fails ? "\n%d FAILED\n" : "\nall passed\n", fails);
    return fails ? 1 : 0;
}
