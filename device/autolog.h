// AUTO LOG: start a logging session by itself once the GPS has a fix.
//
// The decision only, header-only and pure, so every rule is host-tested
// (sim/test_autolog.cpp). main.cpp feeds it once per loop and, when step()
// says so, does exactly what tapping START does: logging::start, persist the
// "was logging" flag, and one /diag.log INFO line. Nothing here transmits.
//
// The rules:
//   - OFF (the default) never starts anything.
//   - Never while a session is already wanted: ACTIVE, or NO_CARD waiting for
//     a card (logging::wantsActive()).
//   - The fix must have been fresh for STABLE_MS without a gap, so a first
//     flicker of a fix does not open a file named after it.
//   - Once per arming. A Latch lives for one boot. After the owner taps STOP
//     (noteManualStop), it does not start again this boot -- unless the
//     setting is turned OFF and back ON, which is an explicit re-arm.
//   - No card, no attempt: it waits for the card rather than calling start()
//     into NO_CARD, then starts once when the card is there.
//   - Not while a rollover or RESET COUNTERS is still draining
//     (logging::transitioning()), or in the loop a RESET was handled.
//
// millis() wraps every 49.7 days; every comparison is a wrap-safe unsigned
// difference, and a fix that has proven stable stays stable however long it
// is then held.
#pragma once
#include <stdint.h>

namespace autolog {

constexpr uint32_t STABLE_MS = 5000;

struct Inputs {
    bool     enabled       = false;   // Settings.autoLog == 1
    bool     logging       = false;   // logging::wantsActive()
    bool     cardPresent   = false;   // sdlog::present()
    bool     fixFresh      = false;   // ui::Gnss::fresh(now)
    bool     transitioning = false;   // a roll / RESET in progress, or handled this loop
    uint32_t nowMs         = 0;
};

// One per boot (a static in main), so a reboot is what clears a manual stop.
struct Latch {
    bool     manualStop = false;      // the owner tapped STOP this arming
    bool     fired      = false;      // this arming already started a session
    uint8_t  prevEnabled = 2;         // the setting last step; 2 = not seen yet
    bool     fixHeld    = false;      // fresh at every step since fixSinceMs
    bool     fixStable  = false;      // ...for at least STABLE_MS
    uint32_t fixSinceMs = 0;
};

// The owner stopped a session by hand (a STOP tap that ended a wanted session).
inline void noteManualStop(Latch& l) { l.manualStop = true; }

// Call every loop. True means: start a session now, exactly like START.
inline bool step(Latch& l, const Inputs& in) {
    // The fix clock runs whatever else is true, so turning the setting ON
    // with a fix already held starts at once rather than 5 s later.
    if (!in.fixFresh) {
        l.fixHeld = l.fixStable = false;
    } else {
        if (!l.fixHeld) { l.fixHeld = true; l.fixSinceMs = in.nowMs; }
        if (!l.fixStable && (uint32_t)(in.nowMs - l.fixSinceMs) >= STABLE_MS) l.fixStable = true;
    }

    // OFF then ON re-arms. The first step only records the setting: a boot
    // is already a fresh arming, and must not read as a toggle.
    if (in.enabled && l.prevEnabled == 0) { l.manualStop = false; l.fired = false; }
    l.prevEnabled = in.enabled ? 1 : 0;

    if (!in.enabled || l.manualStop || l.fired) return false;
    if (in.logging || in.transitioning || !in.cardPresent) return false;
    if (!l.fixStable) return false;
    l.fired = true;
    return true;
}

} // namespace autolog
