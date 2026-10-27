// Host tests for the evil-twin pass budget (device/twinbudget.h).
//
// The pass compares each new named, encrypted AP with every AP in the table,
// with interrupts off: ~20-40 ms at 20,000 entries. A beacon-spam flood of
// hundreds of fake SSIDs a second would saturate the Wi-Fi task. A token
// bucket of entry visits skips the pass under such a flood (counted) and
// leaves normal driving alone.
#include "../device/twinbudget.h"
#include <stdio.h>
#include <string.h>

static int fails = 0;
static void check(const char* what, bool ok) {
    printf(ok ? "  ok   %s\n" : "  FAIL %s\n", what);
    if (!ok) fails++;
}

int main() {
    using namespace twinbudget;

    printf("normal driving:\n");
    {
        // 2 new named APs a second against a 20,000-entry table, 10 minutes.
        Bucket b;
        int skipped = 0;
        for (uint32_t t = 1000; t < 1000 + 600000; t += 500)
            if (!b.take(t, 20000)) skipped++;
        check("2/s at 20,000 entries for 10 min: never skipped", skipped == 0);
        Bucket s;
        int sk = 0;
        for (uint32_t t = 0; t < 600000; t += 100)                 // 10/s, small table
            if (!s.take(t, 3000)) sk++;
        check("10/s at 3,000 entries: never skipped", sk == 0);
        Bucket full;
        check("a pass over a full 32,768-entry table fits the burst", full.take(0, 32768));
    }

    printf("flood:\n");
    {
        // 300 fake SSIDs a second at 20,000 entries for 10 s.
        Bucket b;
        int ran = 0, skipped = 0;
        for (uint32_t i = 0; i < 3000; i++) {
            const uint32_t t = 5000 + i * 10 / 3;
            if (b.take(t, 20000)) ran++; else skipped++;
        }
        printf("       ran %d, skipped %d\n", ran, skipped);
        check("most passes are skipped",                 skipped > 2800);
        check("work stays within burst + rate x time",
              (uint64_t)ran * 20000 <= BURST + (uint64_t)RATE_PER_MS * 10000);
    }

    printf("refill:\n");
    {
        Bucket b;
        for (int g = 0; g < 100 && b.take(0, 20000); g++) {}
        check("drained: the next pass is skipped",      !b.take(0, 20000));
        check("300 ms later: still short",                !b.take(300, 20000));
        check("after enough quiet: runs again",          b.take(600, 20000));
        check("an empty table costs nothing",            b.take(600, 0));
    }

    printf("millis wrap:\n");
    {
        Bucket b;
        b.take(0xFFFFFF00u, 0);
        for (int g = 0; g < 100 && b.take(0xFFFFFF00u, 20000); g++) {}
        check("drained just before the wrap",            !b.take(0xFFFFFF00u, 20000));
        check("refills across the wrap (1 s later)",      b.take(0xFFFFFF00u + 1000u, 20000));
        Bucket c;
        c.take(0, 0);
        for (int g = 0; g < 100 && c.take(0, 20000); g++) {}
        check("a very long quiet caps at the burst, no overflow",
              c.take(0xF0000000u, BURST) && !c.take(0xF0000000u, 1));
    }

    printf("chunking:\n");
    check("lock released every CHUNK visits",      chunkDue(CHUNK - 1) && !chunkDue(0) && !chunkDue(CHUNK - 2));
    check("and again a CHUNK later",               chunkDue(2 * CHUNK - 1));

    printf("SSID filter:\n");
    {
        static SsidFilter f;
        const uint32_t seed = 0xC0FFEE11u;
        check("empty: definitely not present",          !f.mayHave("HOMENET", seed));
        f.add("HOMENET", seed);
        check("added: may be present",                   f.mayHave("HOMENET", seed));
        int fp = 0;
        char nm[24];
        for (int i = 0; i < 1000; i++) { snprintf(nm, sizeof nm, "FAKE%06d", i); if (f.mayHave(nm, seed)) fp++; }
        check("1000 unknown SSIDs against one entry: almost all negative", fp < 5);
        f.clear();
        check("clear empties it",                       !f.mayHave("HOMENET", seed));
        f.add("HOMENET", seed);
        check("keyed: the bit differs under another seed",
              SsidFilter::bitOf("HOMENET", seed) != SsidFilter::bitOf("HOMENET", seed ^ 0x5A5A5A5Au));
        check("only the stored 19 chars count",
              SsidFilter::bitOf("ABCDEFGHIJKLMNOPQRSTUVW", seed) == SsidFilter::bitOf("ABCDEFGHIJKLMNOPQRS", seed));
    }

    printf("inline gate:\n");
    {
        static SsidFilter f; f.clear();
        DeferRing r; Bucket b;
        const uint32_t seed = 7;
        check("no pass wanted: NO_PASS",                 gate(false, "X", seed, f, b, 0, 5000, r, 1, 1) == NO_PASS);
        const uint32_t before = b.tokens;
        check("SSID not in the table: FILTERED",         gate(true, "NEWNAME", seed, f, b, 0, 20000, r, 1, 1) == FILTERED);
        check("and the budget is untouched",             b.tokens == before);
        f.add("NEWNAME", seed);
        check("SSID may be there, budget ok: RUN",       gate(true, "NEWNAME", seed, f, b, 0, 20000, r, 2, 1) == RUN);
        for (int g = 0; g < 10; g++) gate(true, "NEWNAME", seed, f, b, 0, 20000, r, 3, 1);
        check("budget spent: DEFERRED, not dropped",     gate(true, "NEWNAME", seed, f, b, 0, 20000, r, 9, 1) == DEFERRED);
        check("the deferred check is queued",            r.n > 0);
    }

    printf("deferred ring:\n");
    {
        DeferRing r;
        for (uint16_t i = 0; i < DeferRing::N; i++) r.push(i, 1);
        check("fills without loss",                      r.lost == 0 && r.n == DeferRing::N);
        r.push(500, 1); r.push(501, 1);
        check("overflow overwrites the oldest, counted", r.lost == 2 && r.n == DeferRing::N);
        DeferRing::Item it;
        check("newest first",                            r.popNewest(it, 1) && it.slot == 501);
        check("then the next newest",                    r.popNewest(it, 1) && it.slot == 500);
        DeferRing g;
        g.push(10, 1); g.push(11, 1);
        check("a rolled-over table invalidates queued checks", !g.popNewest(it, 2) && g.stale == 2 && g.n == 0);
    }

    printf("flood, then the twin is still found:\n");
    {
        // A miniature table: SSID, auth and OUI byte per entry. Entry 0 is the
        // real HOMENET (auth 4). The attacker floods fresh BSSIDs, then
        // beacons a rogue HOMENET with different auth and another OUI.
        struct E { char ssid[20]; uint16_t auth; uint8_t oui; };
        static E tbl[25000];
        uint32_t n = 0;
        static SsidFilter f; f.clear();
        const uint32_t seed = 0xABCDu;
        // Inserted first, gated, and only then added to the filter: the
        // device's order (a new AP must not find its own SSID).
        auto addE = [&](const char* s, uint16_t a, uint8_t o) {
            snprintf(tbl[n].ssid, sizeof tbl[n].ssid, "%s", s); tbl[n].auth = a; tbl[n].oui = o;
            return (uint16_t)n++;
        };
        for (int i = 0; i < 19000; i++) {
            char nm[20]; snprintf(nm, sizeof nm, "N%05d", i % 9000); addE(nm, 4, 1); f.add(nm, seed);
        }
        addE("HOMENET", 4, 1); f.add("HOMENET", seed);
        DeferRing r; Bucket b; Job j;
        auto twinOf = [&](uint16_t tgt) {
            return [&, tgt](uint32_t q) {
                const E& o = tbl[q]; const E& t = tbl[tgt];
                return q != tgt && o.ssid[0] && o.auth && o.auth != t.auth &&
                       !strcmp(o.ssid, t.ssid) && o.oui != t.oui;
            };
        };
        uint32_t t = 1000;
        int filtered = 0, deferred = 0, ran = 0;
        // 300 fake beacons a second for 10 s; a third reuse a name in the table.
        for (int i = 0; i < 3000; i++, t += 3) {
            char nm[20]; snprintf(nm, sizeof nm, (i % 3) ? "SPAM%06d" : "N%05d", (i % 3) ? i : i % 9000);
            const uint16_t sl = addE(nm, 2, 9);
            const Inline g = gate(true, tbl[sl].ssid, seed, f, b, t, n, r, sl, 1);
            if (g == FILTERED) filtered++; else if (g == DEFERRED) deferred++; else if (g == RUN) ran++;
            f.add(tbl[sl].ssid, seed);
            if (i == 2950) {            // the rogue twin arrives late in the flood
                const uint16_t rogue = addE("HOMENET", 2, 7);
                check("rogue twin during the flood is deferred, not dropped",
                      gate(true, "HOMENET", seed, f, b, t, n, r, rogue, 1) == DEFERRED);
            }
        }
        printf("       filtered %d, ran %d, deferred %d, ring lost %u\n", filtered, ran, deferred, (unsigned)r.lost);
        check("most fake unique SSIDs cost nothing (filtered)", filtered > 1200);
        // The flood stops; the loop task works through the ring as budget returns.
        bool found = false;
        uint16_t foundSlot = 0;
        for (int k = 0; k < 200000 && !found; k++, t += 1) {
            uint16_t sl = 0;
            const Step st = step(j, r, b, t, 1, n, twinOf, sl);
            if (st == FOUND) {
                const E& e = tbl[sl];
                if (!strcmp(e.ssid, "HOMENET") && e.oui == 7) { found = true; foundSlot = sl; }
            }
            if (st == IDLE) break;
        }
        check("after the budget refills the rogue twin is detected", found);
        check("and it is the rogue entry that is flagged", found && tbl[foundSlot].auth == 2);
    }

    printf("deferred job and a rollover:\n");
    {
        DeferRing r; Bucket b; Job j;
        r.push(5, 1);
        auto never = [](uint16_t) { return [](uint32_t) { return false; }; };
        uint16_t sl;
        check("a job starts on the queued check",        step(j, r, b, 0, 1, 5000, never, sl) == WORKING && j.active);
        check("the table rolled over: abandoned",        step(j, r, b, 1, 2, 5000, never, sl) == ABANDONED && !j.active);
        check("each step visits at most CHUNK entries",  CHUNK <= 1024);
    }

    printf(fails ? "\n%d FAILED\n" : "\nall passed\n", fails);
    return fails ? 1 : 0;
}
