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
//  - Inserts stop at insertLimit() (15/16 of the table). Past it, and (above
//    crowdLimit(), 3/4 full) when no free slot turns up within PROBE_LIMIT
//    probes, the answer is FULL: the caller counts the frame and touches no
//    slot.
//  - Every key sits at most `maxProbe` slots from its home (the longest probe
//    any insert has needed). Nothing is deleted except by clearing the whole
//    table, so once inserts are refused a lookup can stop after maxProbe + 1
//    probes: O(cluster), not O(table).
//  - The evil-twin pass runs only for a key that was actually inserted.
//
// This is a backstop. The session rolls over (applog::ROLLOVER_APS, forced at
// ROLL_HARD_APS) long before the table gets near insertLimit().
//
// Collision floods (security review): anyone can transmit beacons with chosen
// BSSIDs. With an unkeyed hash they could precompute a cluster of MACs that
// share one probe chain, and a hard probe limit would then drop a target AP
// whose BSSID homes inside that chain while the table is nearly empty -- the
// unit would never log or alert on it. Two defences:
//  - hashMac() is keyed with a per-boot random seed (esp_random() in the
//    device's begin()), so a cluster computed offline does not collide on the
//    unit. The slot of a MAC therefore differs from boot to boot; slot indices
//    live only in RAM and are never stored or compared across boots.
//  - Below crowdLimit() (75% full) an insert never gives up: it probes on to
//    the next free slot. PROBE_LIMIT applies only above that, where the
//    rollover is already due. An insert that needed more than
//    PROBE_LIMIT probes is flagged (longProbe()) -- under a keyed hash that
//    is the signature of a collision attack, and the caller counts it.
// The keyed hash is a fast non-cryptographic mix (two murmur3 finalisers),
// not a PRF: it defeats precomputed clusters, which is the threat, since the
// slot layout is never visible over the air.
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

// The most slots one insert may examine once the table is crowded. At the
// forced-roll ceiling (~82% load) linear probing's expected miss is ~16
// probes, so 512 is never reached by a sane table, and it caps the worst case
// at well under a millisecond.
constexpr uint32_t PROBE_LIMIT = 512;

// Inserts stop here: 15/16 of the table, above every rollover ceiling.
constexpr uint32_t insertLimit(uint32_t cap) { return cap - cap / 16; }

// Below this (75% full) an insert probes to the next free slot, however far:
// a long chain is then an attack indicator, not a reason to drop a device.
// The cost bound there is the chain length, which under the keyed hash is
// ~a few dozen slots at 75% load unless someone knows the boot's seed.
constexpr uint32_t crowdLimit(uint32_t cap) { return cap - cap / 4; }

// murmur3's 32-bit finaliser: a bijection with full avalanche.
inline uint32_t fmix32(uint32_t h) {
    h ^= h >> 16; h *= 0x85ebca6bu;
    h ^= h >> 13; h *= 0xc2b2ae35u;
    h ^= h >> 16;
    return h;
}

// The slot hash for a 6-byte address, keyed with the per-boot `seed`. Every
// table that places a MAC (capture's APs, probe pairs and dial blips, blescan's
// addresses and blips) uses this with that table's seed.
inline uint32_t hashMac(const uint8_t* m, uint32_t seed) {
    const uint32_t a = (uint32_t)m[0] | (uint32_t)m[1] << 8 | (uint32_t)m[2] << 16 |
                       (uint32_t)m[3] << 24;
    const uint32_t b = (uint32_t)m[4] | (uint32_t)m[5] << 8;
    uint32_t h = fmix32(seed ^ a);
    h = fmix32(h ^ b ^ (seed * 0x9e3779b9u) ^ 0x00060000u);   // length-tagged
    return h;
}

// Look `hash`'s key up, or find it a slot. `used(i)` says slot i is occupied,
// `same(i)` that slot i holds this key. `nUsed` is the occupied count and
// `limit` the count at which inserts stop (insertLimit(cap), or cap itself).
template <class Used, class Same>
inline Hit find(uint32_t hash, uint32_t cap, uint32_t nUsed, uint32_t limit,
                uint32_t maxProbe, Used used, Same same) {
    const bool canInsert = nUsed < limit && nUsed < cap;
    // Uncrowded: probe to the next free slot (one exists, nUsed < cap).
    // Crowded: PROBE_LIMIT. Inserts refused: no key sits past maxProbe.
    uint32_t bound = !canInsert ? maxProbe + 1
                   : nUsed < crowdLimit(cap) ? cap : PROBE_LIMIT;
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

// An insert that needed a chain longer than PROBE_LIMIT: below the crowd
// limit, under a keyed hash, the sign of a collision flood. Count it.
inline bool longProbe(const Hit& h) {
    return h.kind == INSERT && h.probes > PROBE_LIMIT;
}

// The evil-twin pass compares a newly seen AP with every other one, so it runs
// only for an AP that was really inserted, with a name and security to compare.
inline bool runTwinPass(const Hit& h, const char* name, uint16_t auth) {
    return h.kind == INSERT && name && name[0] && auth;
}

} // namespace dedup
