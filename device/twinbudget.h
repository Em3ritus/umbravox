// A budget for capture's evil-twin pass, pure so it can be host-tested
// (sim/test_twinbudget.cpp).
//
// The pass compares each new named, encrypted AP with every AP in the table,
// with interrupts off on the Wi-Fi task: ~1 us per entry in PSRAM, so 20-40 ms
// at 20,000 entries. Normal driving finds a few new APs a second; a beacon
// spammer can announce hundreds of fake SSIDs a second, and with no budget
// each one would buy a full-table pass and starve the loop. So the pass draws
// from a token bucket of entry visits: RATE_PER_MS refill (40,000 visits/s,
// ~4% of the CPU), capped at BURST.
//
// A spent budget must not switch detection off (security review: flood fresh
// BSSIDs to drain it, then beacon the rogue twin). So:
//  - SsidFilter: a 4 KB bitset of SSIDs already in the table (with security),
//    keyed with the per-boot seed. A new AP whose SSID is definitely not there
//    cannot be anyone's twin: no walk, no budget. That takes the cost out of a
//    flood of unique fake SSIDs at once. False positives (~25% at 10,000
//    distinct names) just cost a walk.
//  - DeferRing: a check the budget cannot pay for now is queued (slot plus
//    table generation, newest first, 64 deep; overflow drops the oldest and
//    is counted). The loop task works through it with step(), at most CHUNK
//    visits per call, as the budget refills, and a match raises the same flag
//    the inline pass would. A rollover (new generation) invalidates the queue.
// Residual risk: a flood of more than 64 SSIDs that collide with the filter
// can still push a queued rogue out of the ring before it is checked (counted
// as lost), and a deferred alert comes late -- seconds, not frames.
//
// The index rework that would make the pass cheap is out of scope.
#pragma once
#include <stdint.h>

namespace twinbudget {

constexpr uint32_t RATE_PER_MS = 40;       // entry visits earned per millisecond
constexpr uint32_t BURST       = 65536;    // ...banked at most (two full tables)
constexpr uint32_t CHUNK       = 1024;     // visits per stretch with interrupts off
constexpr int32_t  SKEW_MAX_MS = 1000;     // backward jitter between the two tasks' clocks

struct Bucket {
    uint32_t tokens  = BURST;
    uint32_t lastMs  = 0;
    bool     started = false;

    // May a pass of `cost` entry visits run now? Spends them if so. Elapsed
    // time is signed: callers on two tasks read millis() a moment apart, so a
    // time just before lastMs arrives now and then, and as an unsigned
    // difference it wrapped to ~49 days and refilled the bucket every time.
    // Up to SKEW_MAX_MS back is that jitter: no time, and lastMs is not moved
    // back. Further back cannot be skew (millis() is monotonic and the two
    // tasks are milliseconds apart): it is a forward gap of more than ~24.8
    // days read as signed, so the bucket resyncs to now and fills, as any
    // long quiet does -- it must never wait for the clock to catch up. That
    // holds across the millis() wrap. The refill is 64-bit, capped at BURST.
    bool take(uint32_t nowMs, uint32_t cost) {
        if (!started) { started = true; lastMs = nowMs; }
        const int32_t dt = (int32_t)(nowMs - lastMs);
        uint64_t add = 0;
        if (dt > 0)                 { add = (uint64_t)dt * RATE_PER_MS; lastMs = nowMs; }
        else if (dt < -SKEW_MAX_MS) { add = BURST; lastMs = nowMs; }
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

// --- SSID filter -----------------------------------------------------------
struct SsidFilter {
    static constexpr uint32_t BITS = 32768;          // 4 KB
    static constexpr int      MAXC = 19;             // the table stores 19 chars
    uint32_t w[BITS / 32];
    SsidFilter() { clear(); }
    void clear() { for (uint32_t i = 0; i < BITS / 32; i++) w[i] = 0; }
    static uint32_t bitOf(const char* s, uint32_t seed) {
        uint32_t h = 2166136261u ^ seed;
        for (int k = 0; k < MAXC && s[k]; k++) { h ^= (uint8_t)s[k]; h *= 16777619u; }
        h ^= h >> 16; h *= 0x85ebca6bu; h ^= h >> 13; h *= 0xc2b2ae35u; h ^= h >> 16;
        return h % BITS;
    }
    void add(const char* s, uint32_t seed) {
        if (!s || !s[0]) return;
        const uint32_t b = bitOf(s, seed); w[b >> 5] |= 1u << (b & 31);
    }
    bool mayHave(const char* s, uint32_t seed) const {
        if (!s || !s[0]) return false;
        const uint32_t b = bitOf(s, seed); return (w[b >> 5] >> (b & 31)) & 1u;
    }
};

// --- deferred checks -------------------------------------------------------
struct DeferRing {
    static constexpr uint16_t N = 64;
    struct Item { uint16_t slot; uint32_t gen; };
    Item     it[N];
    uint16_t n = 0, head = 0;     // head: where the next push goes
    uint32_t lost = 0, stale = 0; // overwritten when full; invalidated by a roll
    void push(uint16_t slot, uint32_t gen) {
        if (n == N) lost++; else n++;
        it[head] = Item{slot, gen};
        head = (uint16_t)((head + 1) % N);
    }
    // The newest queued check of generation `gen`; older generations dropped.
    bool popNewest(Item& out, uint32_t gen) {
        while (n) {
            head = (uint16_t)((head + N - 1) % N);
            n--;
            if (it[head].gen == gen) { out = it[head]; return true; }
            stale++;
        }
        return false;
    }
};

// What the inline (Wi-Fi task) path does with a newly inserted AP.
enum Inline : uint8_t { NO_PASS, FILTERED, RUN, DEFERRED };
inline Inline gate(bool wantPass, const char* ssid, uint32_t seed, const SsidFilter& f,
                   Bucket& b, uint32_t nowMs, uint32_t cost, DeferRing& r,
                   uint16_t slot, uint32_t gen) {
    if (!wantPass) return NO_PASS;
    if (!f.mayHave(ssid, seed)) return FILTERED;     // nobody to be a twin of
    if (b.take(nowMs, cost)) return RUN;
    r.push(slot, gen);
    return DEFERRED;
}

// The loop task's side: one bounded chunk of one deferred check per call.
struct Job { bool active = false; uint16_t slot = 0; uint32_t gen = 0; uint32_t q = 0; };
enum Step : uint8_t { IDLE, WAITING, WORKING, DONE, FOUND, ABANDONED };
// `matchFor(slot)` returns a predicate over table positions q: "entry q is a
// twin of slot". `gen` is the table's current generation, `n` its entry count.
// On FOUND, `foundSlot` is the deferred AP to flag.
template <class MatchFor>
inline Step step(Job& j, DeferRing& r, Bucket& b, uint32_t nowMs, uint32_t gen,
                 uint32_t n, MatchFor matchFor, uint16_t& foundSlot) {
    if (!j.active) {
        DeferRing::Item it;
        if (!r.popNewest(it, gen)) return IDLE;
        j.active = true; j.slot = it.slot; j.gen = it.gen; j.q = 0;
    }
    if (j.gen != gen) { j.active = false; return ABANDONED; }
    const uint32_t left = j.q < n ? n - j.q : 0;
    const uint32_t todo = left < CHUNK ? left : CHUNK;
    if (!b.take(nowMs, todo)) return WAITING;
    auto match = matchFor(j.slot);
    for (uint32_t k = 0; k < todo; k++, j.q++)
        if (match(j.q)) { j.active = false; foundSlot = j.slot; return FOUND; }
    if (j.q >= n) { j.active = false; return DONE; }
    return WORKING;
}

} // namespace twinbudget
