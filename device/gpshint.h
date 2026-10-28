// GPS bring-up hints: the two wiring faults worth one console/diag line.
//
// Header-only and pure, host-tested in sim/test_gpshint.cpp. main.cpp calls
// check() once per loop with the parser's byte and good-sentence counters and
// prints whatever it returns. Each hint fires at most once per boot, so this
// costs a compare per loop and never logs per byte. Receive-only: nothing
// here writes to the GPS.
#pragma once
#include <stdint.h>

namespace gpshint {

enum Hint : uint8_t { NONE = 0, NO_DATA = 1, NO_PARSE = 2 };

// A NEO-6M at its defaults sends ~450 bytes a second and a TXT banner at
// boot, so 10 s of silence is a wiring fault, not a slow receiver.
constexpr uint32_t NO_DATA_MS = 10000;
// About two seconds of output: enough that a few boot-glitch bytes do not
// trip it, short enough to say so before the owner gives up.
constexpr uint32_t NO_PARSE_BYTES = 1000;

struct Watch {
    bool saidNoData  = false;
    bool saidNoParse = false;

    // nowMs: millis() since boot. bytes/good: ui::Gnss counters.
    Hint check(uint32_t nowMs, uint32_t bytes, uint32_t good) {
        if (good) return NONE;   // one good sentence and the link is fine
        if (bytes == 0) {
            if (saidNoData || nowMs < NO_DATA_MS) return NONE;
            saidNoData = true;
            return NO_DATA;
        }
        if (saidNoParse || bytes < NO_PARSE_BYTES) return NONE;
        saidNoParse = true;
        return NO_PARSE;
    }
};

} // namespace gpshint
