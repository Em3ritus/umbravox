// Tap-to-dismiss for the alert takeover.
//
// The device keeps an alert "active" while the same threat keeps being re-detected (a hit under 6 s old), so clearing
// the flag once would bring it straight back. A dismissed alert therefore stays hidden until a DIFFERENT alert shows
// (another threat id or name) or until the alert has ended, after which the same one may show again.
#pragma once
#include <cstdint>
#include <cstring>

namespace alertgate {
struct Gate {
    bool    dismissed = false;
    uint8_t threat = 0;
    char    name[24] = {0};

    // The tap names the alert being cleared, so the gate knows exactly which one to keep hidden.
    void dismiss(uint8_t t, const char* n) { dismissed = true; threat = t; std::strncpy(name, n ? n : "", sizeof name - 1); name[sizeof name - 1] = 0; }
    // Feed the device's current alert; returns whether the takeover should be on screen.
    bool shows(bool active, uint8_t t, const char* n) {
        if (!active) { dismissed = false; return false; }
        if (!dismissed) return true;
        if (t != threat || std::strncmp(name, n ? n : "", sizeof name - 1) != 0) { dismissed = false; return true; }
        return false;
    }
};
}  // namespace alertgate
