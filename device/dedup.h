// The dedup tables' probe, as a pure function so it can be host-tested
// (sim/test_dedup.cpp): open addressing with linear probing, the same probe
// order capture and blescan always used, plus the guards the field logs showed
// were missing.
//
// What went wrong (Task 1 report, section 1b): at a full table an unknown BSSID
// probed every slot (8192 PSRAM reads with interrupts off), the evil-twin pass
// then walked all 8192 entries again, and the frame finally overwrote an
// unrelated AP's slot. In mesh mode that starved the loop until the unit froze.
//
// Now:
//  - Inserts stop at insertLimit() (15/16 of the table). Past it, and when no
//    free slot turns up within PROBE_LIMIT probes, the answer is FULL: the
//    caller counts the frame and touches no slot.
//  - Every key sits at most `maxProbe` slots from its home (the longest probe
//    any insert has needed). Nothing is deleted except by clearing the whole
//    table, so once inserts are refused a lookup can stop after maxProbe + 1
//    probes: O(cluster), not O(table).
//  - The evil-twin pass runs only for a key that was actually inserted.
//
// This is a backstop. The session rolls over (applog::ROLLOVER_APS, forced at
// ROLL_HARD_APS) long before the table gets near insertLimit().
#pragma once
#include <stdint.h>

namespace dedup {

enum Kind : uint8_t {
    FOUND,    // the key's own slot
    INSERT,   // a free slot the caller should claim for the key
    FULL,     // no slot: count the frame, change nothing
};

struct Hit {
    Kind     kind;
    int      slot;      // -1 when FULL
    uint32_t probes;    // slots examined, for the bound tests
};

// The most slots one insert may examine. At the forced-roll ceiling (~82%
// load) linear probing's expected miss is ~16 probes, so 512 is never reached
// by a sane table, and it caps the worst case at well under a millisecond.
constexpr uint32_t PROBE_LIMIT = 512;

// Inserts stop here: 15/16 of the table, above every rollover ceiling.
constexpr uint32_t insertLimit(uint32_t cap) { return cap - cap / 16; }

// Look `hash`'s key up, or find it a slot. `used(i)` says slot i is occupied,
// `same(i)` that slot i holds this key. `nUsed` is the occupied count and
// `limit` the count at which inserts stop (insertLimit(cap), or cap itself).
template <class Used, class Same>
inline Hit find(uint32_t hash, uint32_t cap, uint32_t nUsed, uint32_t limit,
                uint32_t maxProbe, Used used, Same same) {
    const bool canInsert = nUsed < limit && nUsed < cap;
    uint32_t bound = canInsert ? PROBE_LIMIT : maxProbe + 1;
    if (bound > cap) bound = cap;
    const uint32_t home = hash % cap;
    for (uint32_t d = 0; d < bound; d++) {
        uint32_t i = home + d;
        if (i >= cap) i -= cap;
        if (!used(i)) {
            // An empty slot ends every probe chain: the key is not stored
            // beyond it, because nothing is ever deleted on its own.
            if (canInsert) return Hit{INSERT, (int)i, d + 1};
            return Hit{FULL, -1, d + 1};
        }
        if (same(i)) return Hit{FOUND, (int)i, d + 1};
    }
    return Hit{FULL, -1, bound};
}

// After the caller claims an INSERT slot: remember the longest probe distance.
inline void noteInsert(uint32_t& maxProbe, const Hit& h) {
    if (h.kind == INSERT && h.probes - 1 > maxProbe) maxProbe = h.probes - 1;
}

// The evil-twin pass compares a newly seen AP with every other one, so it runs
// only for an AP that was really inserted, with a name and security to compare.
inline bool runTwinPass(const Hit& h, const char* name, uint16_t auth) {
    return h.kind == INSERT && name && name[0] && auth;
}

} // namespace dedup
