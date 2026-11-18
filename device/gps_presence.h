// Is there an LC76G on the shared I2C bus? The decision, without the bus.
//
// Header-only and pure, host-tested in sim/test_gps_presence.cpp.
// ws_c5_gps.cpp reports each poll here and acts on what comes back.
//
// Most boards have no GPS. A failed write to an address nobody holds makes
// the Arduino core print an error line, so a board without one must not find
// that out by polling. Absence is decided by the first check after boot, and
// every check made while absent (or not yet decided) is the quiet kind: an
// address-only probe, which the core does not log.
//
// A module that has answered once is treated as before: it has to miss
// absentAfter polls in a row before it is called absent.
#pragma once
#include <stdint.h>

namespace gpspresence {

enum Event : uint8_t { NONE = 0, WENT_ABSENT = 1, CAME_BACK = 2 };

struct Tracker {
    bool     absent  = false;
    bool     decided = false;   // the start-up check has run
    uint32_t failRun = 0;       // missed polls in a row since it last answered

    // Lead the next poll with the quiet probe, and stop there if it fails.
    bool quiet() const { return absent || !decided; }

    // answered: something acknowledged at the command address this poll.
    Event onPoll(bool answered, uint32_t absentAfter) {
        const bool first = !decided;
        decided = true;
        if (answered) {
            failRun = 0;
            if (!absent) return NONE;
            absent = false;
            return CAME_BACK;
        }
        if (absent) return NONE;
        if (first || ++failRun >= absentAfter) {
            absent = true;
            return WENT_ABSENT;
        }
        return NONE;
    }
};

} // namespace gpspresence
