// Whether active discovery (Wi-Fi probe requests, BLE scan requests) may run
// right now, as a pure decision so it can be host-tested
// (sim/test_discoverypolicy.cpp).
//
// Active discovery takes radio time from passive capture and restarts the BLE
// scan every 2 s; on a drive that cost far more BLE sightings than it gained.
// So while a session is recording, the unit is passive. The saved setting is
// not changed: it applies again as soon as the session stops.
#pragma once
#include <stdint.h>

namespace discoverypolicy {

// `userSetting` is the saved DISCOVERY TX value (nonzero = on), `recording`
// is true from START to STOP of a session. Returns 0 or 1.
inline uint8_t effective(uint8_t userSetting, bool recording) {
    return (userSetting && !recording) ? 1 : 0;
}

} // namespace discoverypolicy
