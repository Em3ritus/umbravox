// Pure pieces of the phone bridge's pairing security (host-tested).
//
// The bridge requires a MITM-protected (passkey) bonded link before it serves any
// capture data. The handheld is the display: when a phone starts pairing, the
// stack hands us a fresh random 6-digit code, which is shown on the settings
// status row for PIN_MS and then forgotten. Nothing is shown while nobody is
// pairing, so a bystander cannot see a code to use.
#pragma once
#include <stdint.h>
#include <atomic>
namespace bridgeauth {
constexpr uint32_t PIN_MS = 60000;
constexpr uint32_t PAIR_TIMEOUT_MS = 60000;   // a connection must be authenticated within this (>= PIN_MS)

// Written from the BLE host task, read from the loop task; each field is atomic,
// and the flag is published last so a reader never sees a half-written code.
class PinWindow {
public:
    void set(uint32_t pin, uint32_t now) { at_.store(now); pin_.store(pin); live_.store(true); }
    // Start a window with `pin` unless one is already live (then keep its code and
    // its start time). True when this call started the window.
    bool offer(uint32_t pin, uint32_t now) {
        uint32_t cur;
        if (get(now, cur)) return false;
        set(pin, now);
        return true;
    }
    void clear() { live_.store(false); }
    // True with the code while it is live. Signed difference: a code stamped a few
    // ms after `now` was read is live, and the 32-bit wrap does not expire it early.
    bool get(uint32_t now, uint32_t& pin) const {
        if (!live_.load()) return false;
        if ((int32_t)(now - at_.load()) >= (int32_t)PIN_MS) return false;
        pin = pin_.load();
        return true;
    }
private:
    std::atomic<uint32_t> pin_{0}, at_{0};
    std::atomic<bool> live_{false};
};

// Data may flow only on a link that is encrypted AND authenticated (passkey). An
// encrypted-only link is "Just Works": anyone in range could have paired with it.
inline bool mayServe(bool authRequired, bool encrypted, bool authenticated) {
    return !authRequired || (encrypted && authenticated);
}

// Signed difference, like every time compare in this firmware.
inline bool pairingExpired(uint32_t now, uint32_t connectedAt) {
    return (int32_t)(now - connectedAt) >= (int32_t)PAIR_TIMEOUT_MS;
}

// Brute-force limit on pairing. A wrong code ends a pairing attempt; MAX_FAILS of
// them end the window (the caller clears the code and rotates it) and block new
// pairings for a back-off that doubles up to BACKOFF_MAX_MS. A successful pairing
// resets it. Written from the BLE host task, read from the loop task.
constexpr int MAX_FAILS = 3;
constexpr uint32_t BACKOFF_BASE_MS = 5000, BACKOFF_MAX_MS = 60000;
class PairLimiter {
public:
    // True when this failure reached the cap, so the window must end.
    bool noteFailure(uint32_t now) {
        const uint8_t f = (uint8_t)(fails_.load() + 1);
        if (f < MAX_FAILS) { fails_.store(f); return false; }
        fails_.store(0);
        uint32_t back = BACKOFF_BASE_MS << level_.load();
        if (back > BACKOFF_MAX_MS) back = BACKOFF_MAX_MS;
        if (level_.load() < 8) level_.store((uint8_t)(level_.load() + 1));
        until_.store(now + back);
        blocked_.store(true);
        return true;
    }
    void noteSuccess() { fails_.store(0); level_.store(0); blocked_.store(false); }
    bool blocked(uint32_t now) const { return blocked_.load() && (int32_t)(until_.load() - now) > 0; }
private:
    std::atomic<uint8_t> fails_{0}, level_{0};
    std::atomic<uint32_t> until_{0};
    std::atomic<bool> blocked_{false};
};
}
