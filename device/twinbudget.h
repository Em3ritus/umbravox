// A budget for capture's evil-twin pass, pure so it can be host-tested
// (sim/test_twinbudget.cpp).
//
// The pass compares each new named, encrypted AP with every AP in the table,
// with interrupts off on the Wi-Fi task: ~1 us per entry in PSRAM, so 20-40 ms
// at 20,000 entries. Normal driving finds a few new APs a second; a beacon
// spammer can announce hundreds of fake SSIDs a second, and with no budget
// each one would buy a full-table pass and starve the loop. So the pass draws
// from a token bucket of entry visits: RATE_PER_MS refill (40,000 visits/s,
// ~4% of the CPU), capped at BURST. A pass that would overdraw it is skipped
// and counted (the caller logs it). The residual risk is the obvious one:
// under such a flood, evil-twin detection for the skipped APs does not run.
//
// The index rework that would make the pass cheap is out of scope.
#pragma once
#include <stdint.h>

namespace twinbudget {

constexpr uint32_t RATE_PER_MS = 40;       // entry visits earned per millisecond
constexpr uint32_t BURST       = 65536;    // ...banked at most (two full tables)
constexpr uint32_t CHUNK       = 1024;     // visits per stretch with interrupts off

struct Bucket {
    uint32_t tokens  = BURST;
    uint32_t lastMs  = 0;
    bool     started = false;

    // May a pass of `cost` entry visits run now? Spends them if so. Elapsed
    // time is unsigned (holds across the millis() wrap) and the refill is
    // 64-bit, so a long quiet spell just fills the bucket.
    bool take(uint32_t nowMs, uint32_t cost) {
        if (!started) { started = true; lastMs = nowMs; }
        const uint64_t add = (uint64_t)(nowMs - lastMs) * RATE_PER_MS;
        lastMs = nowMs;
        const uint64_t t = (uint64_t)tokens + add;
        tokens = t > BURST ? BURST : (uint32_t)t;
        if (cost > tokens) return false;
        tokens -= cost;
        return true;
    }
};

// Release and retake the lock after visit `q` (0-based): every CHUNK visits,
// so interrupts are never off for more than ~1 ms of the pass.
inline bool chunkDue(uint32_t q) { return (q + 1) % CHUNK == 0; }

} // namespace twinbudget
