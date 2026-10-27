// Host tests for the dedup table's probe (device/dedup.h): the lookup/insert
// that cap::insert and the BLE scan callback run with interrupts off.
//
// The field failure this pins: at a full table every frame from an unknown AP
// probed all 8192 slots, ran the evil-twin pass over all 8192 again, and then
// overwrote an unrelated AP's slot. Now a full (or nearly full) table answers
// FULL after a bounded probe, touches nothing, and skips the evil-twin pass.
#include "../device/dedup.h"
#include <stdio.h>
#include <string.h>
#include <stdint.h>

static int fails = 0;
static void check(const char* what, bool ok) {
    printf(ok ? "  ok   %s\n" : "  FAIL %s\n", what);
    if (!ok) fails++;
}

// A miniature of capture's table: used flags plus a key per slot.
template <int N>
struct Tbl {
    bool     used[N];
    uint32_t key[N];
    uint32_t nUsed = 0;
    uint32_t maxProbe = 0;
    Tbl() { memset(used, 0, sizeof used); memset(key, 0, sizeof key); }

    dedup::Hit find(uint32_t k, uint32_t hash, uint32_t limit) {
        return dedup::find(hash, (uint32_t)N, nUsed, limit, maxProbe,
                           [&](uint32_t i) { return used[i]; },
                           [&](uint32_t i) { return key[i] == k; });
    }
    // What cap::insert does with the answer: claim the slot only on INSERT.
    dedup::Hit put(uint32_t k, uint32_t hash, uint32_t limit) {
        const dedup::Hit h = find(k, hash, limit);
        if (h.kind == dedup::INSERT) {
            used[h.slot] = true; key[h.slot] = k; nUsed++;
            dedup::noteInsert(maxProbe, h);
        }
        return h;
    }
};

static uint32_t mix(uint32_t x) {            // any decent spread will do
    x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15; x *= 0x846ca68bU; x ^= x >> 16;
    return x;
}

// The probe as it was before this change, for the "same slots" check.
template <int N>
static int oldProbe(const Tbl<N>& t, uint32_t k, uint32_t hash) {
    const int home = (int)(hash % N);
    for (int p = 0; p < N; p++) {
        const int i = (home + p) % N;
        if (!t.used[i]) return i;
        if (t.key[i] == k) return i;
    }
    return home;
}

int main() {
    printf("insert limit:\n");
    check("15/16 of 64 is 60",            dedup::insertLimit(64) == 60);
    check("15/16 of 32768 is 30720",      dedup::insertLimit(32768) == 30720);

    printf("normal load, same slots as the old probe:\n");
    {
        static Tbl<1024> t;
        bool same = true;
        for (uint32_t k = 1; k <= 600; k++) {
            const int want = oldProbe(t, k, mix(k));
            const dedup::Hit h = t.put(k, mix(k), dedup::insertLimit(1024));
            if (h.kind != dedup::INSERT || h.slot != want) same = false;
        }
        check("600 new keys land where the old probe put them", same);
        bool found = true;
        for (uint32_t k = 1; k <= 600; k++) {
            const dedup::Hit h = t.find(k, mix(k), dedup::insertLimit(1024));
            if (h.kind != dedup::FOUND || t.key[h.slot] != k) found = false;
        }
        check("every known key is FOUND in its own slot", found);
    }

    printf("table at the insert limit:\n");
    {
        static Tbl<64> t;
        const uint32_t lim = dedup::insertLimit(64);
        for (uint32_t k = 1; t.nUsed < lim && k < 100000; k++) t.put(k, mix(k), lim);
        check("filled to the limit", t.nUsed == lim);

        Tbl<64> before = t;
        const dedup::Hit h = t.put(999999, mix(999999), lim);
        check("an unknown key answers FULL",             h.kind == dedup::FULL);
        check("FULL names no slot",                      h.slot < 0);
        check("every slot is byte-identical after FULL",
              !memcmp(before.used, t.used, sizeof t.used) &&
              !memcmp(before.key, t.key, sizeof t.key) && before.nUsed == t.nUsed);
        check("unknown key probes at most maxProbe + 1", h.probes <= t.maxProbe + 1);

        bool found = true;
        for (uint32_t i = 0; i < 64; i++) {
            if (!t.used[i]) continue;
            const uint32_t k = t.key[i];
            const dedup::Hit f = t.find(k, mix(k), lim);
            if (f.kind != dedup::FOUND || f.slot != (int)i) found = false;
        }
        check("known keys are still FOUND at the limit", found);
    }

    printf("completely full table:\n");
    {
        static Tbl<64> t;
        for (uint32_t k = 1; t.nUsed < 64 && k < 100000; k++) t.put(k, mix(k), 64);   // limit = all
        check("all 64 slots used", t.nUsed == 64);
        const dedup::Hit h = t.put(424242, mix(424242), 64);
        check("unknown key in a full table is FULL, not an overwrite", h.kind == dedup::FULL);
        check("and stops within the table", h.probes <= 64);
        const dedup::Hit f = t.find(t.key[17], mix(t.key[17]), 64);
        check("a known key in a full table is FOUND", f.kind == dedup::FOUND && f.slot == 17);
    }

    printf("probe bound:\n");
    {
        // Every key hashes to slot 0: one cluster that grows past the bound.
        static Tbl<2048> t;
        const uint32_t lim = dedup::insertLimit(2048);
        int inserted = 0;
        for (uint32_t k = 1; k <= dedup::PROBE_LIMIT + 10; k++)
            if (t.put(k, 0, lim).kind == dedup::INSERT) inserted++;
        check("a cluster stops growing at PROBE_LIMIT", inserted == (int)dedup::PROBE_LIMIT);
        const dedup::Hit h = t.put(77777, 0, lim);
        check("the next key is dropped as FULL",        h.kind == dedup::FULL);
        check("after exactly PROBE_LIMIT probes",       h.probes == dedup::PROBE_LIMIT);
        check("maxProbe never exceeds the bound",       t.maxProbe < dedup::PROBE_LIMIT);
        const dedup::Hit f = t.find(dedup::PROBE_LIMIT, 0, lim);
        check("the last key in the cluster is FOUND",   f.kind == dedup::FOUND);
    }

    printf("evil-twin gate:\n");
    {
        dedup::Hit ins{dedup::INSERT, 3, 1}, fnd{dedup::FOUND, 3, 1}, full{dedup::FULL, -1, 9};
        check("a new AP with a name and auth runs the pass", dedup::runTwinPass(ins, "NET", 4));
        check("a known AP does not",                         !dedup::runTwinPass(fnd, "NET", 4));
        check("a dropped AP (FULL) does not",                !dedup::runTwinPass(full, "NET", 4));
        check("no name: no pass",                            !dedup::runTwinPass(ins, "", 4));
        check("null name: no pass",                          !dedup::runTwinPass(ins, nullptr, 4));
        check("no auth: no pass",                            !dedup::runTwinPass(ins, "NET", 0));
    }

    printf(fails ? "\n%d FAILED\n" : "\nall passed\n", fails);
    return fails ? 1 : 0;
}
