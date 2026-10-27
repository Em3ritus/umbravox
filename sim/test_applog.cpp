// Host test for the append-only WiGLE logger core (device/applog.h): per-source
// cursors over the first-seen order, the per-flush row cap, retry of rows that
// are not exportable yet, exact-once delivery across failed writes, repair of a
// failed append, the never-truncate-a-file-we-did-not-start rule, and the
// rollover decision. The card is an in-memory sink with failure injection.
#include "../device/applog.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <map>
#include <string>
#include <vector>

static int fails = 0;
static void check(const char* what, bool ok) {
    printf(ok ? "  ok   %s\n" : "  FAIL %s\n", what);
    if (!ok) fails++;
}

// --- fake source: rows in first-seen order, each with a verdict ------------
struct FakeSrc : applog::Source {
    std::string tag;
    std::vector<applog::Verdict> v;
    std::vector<uint8_t> bits;
    uint32_t cap;
    int formats = 0;
    bool formatFails = false;        // format() reports "not exportable after all"
    explicit FakeSrc(const char* t, uint32_t c = 4096) : tag(t), bits((c + 7) / 8), cap(c) {}
    uint32_t add(applog::Verdict x) { v.push_back(x); return (uint32_t)v.size() - 1; }
    uint32_t count() override { return (uint32_t)v.size(); }
    uint32_t capacity() override { return cap; }
    uint8_t* doneBits() override { return bits.data(); }
    applog::Verdict verdict(uint32_t i, uint32_t, bool) override { return v[i]; }
    int format(uint32_t i, char* buf, int n) override {
        formats++;
        if (formatFails) return 0;
        return snprintf(buf, (size_t)n, "%s-%04u,row\n", tag.c_str(), (unsigned)i);
    }
    std::string row(uint32_t i) {
        char b[64]; snprintf(b, sizeof b, "%s-%04u,row\n", tag.c_str(), (unsigned)i);
        return b;
    }
};

// --- fake card -------------------------------------------------------------
struct FakeCard : applog::Sink {
    std::map<std::string, std::string> files;
    std::string cur;                 // the open file
    bool isOpen = false;
    std::string pending;             // bytes written but not yet "landed"
    // failure injection
    bool failOpen = false, failClose = false, failTruncate = false;
    int  failWriteAfter = -1;        // fail the Nth write call from now (0 = next)
    int  partialBytes = 0;           // on a failing write, this many bytes still land
    int  opens = 0, truncates = 0;

    bool open(const char* name, uint32_t& size) override {
        opens++;
        if (failOpen) return false;
        cur = name; isOpen = true;
        size = (uint32_t)files[cur].size();
        return true;
    }
    bool write(const char* d, uint32_t len) override {
        if (!isOpen) return false;
        if (failWriteAfter == 0) {
            failWriteAfter = -1;
            const uint32_t k = (uint32_t)partialBytes < len ? (uint32_t)partialBytes : len;
            files[cur].append(d, k);     // a short write: some bytes land
            return false;
        }
        if (failWriteAfter > 0) failWriteAfter--;
        files[cur].append(d, len);
        return true;
    }
    bool close() override {
        isOpen = false;
        return !failClose;
    }
    bool truncate(const char* name, uint32_t len) override {
        truncates++;
        if (failTruncate) return false;
        std::string& f = files[name];
        if (f.size() > len) f.resize(len);
        return true;
    }
};

static const char* HDR = "HEADER\n";

struct Rig {
    FakeCard card;
    FakeSrc  wifi{"W"}, ble{"B"};
    applog::Source* srcs[2];
    applog::Cursor  curs[2];
    applog::FileState fs;
    applog::Pending pend[applog::BATCH_ROWS];
    Rig() { srcs[0] = &wifi; srcs[1] = &ble; }
    applog::Result batch(uint32_t maxRows = applog::BATCH_ROWS, const char* name = "/A.CSV") {
        return applog::appendBatch(name, fs, card, HDR, srcs, curs, 2, maxRows, pend, 0, false);
    }
    std::string file(const char* name = "/A.CSV") { return card.files[name]; }
};

// Count occurrences of `needle` in `hay`.
static int occurrences(const std::string& hay, const std::string& needle) {
    int n = 0;
    for (size_t p = hay.find(needle); p != std::string::npos; p = hay.find(needle, p + 1)) n++;
    return n;
}

int main() {
    using namespace applog;

    printf("a new file gets the header once, then every exportable row:\n");
    {
        Rig r;
        r.wifi.add(WRITE); r.wifi.add(WRITE); r.ble.add(WRITE);
        Result x = r.batch();
        check("batch ok", x.ok);
        check("3 rows", x.rows == 3);
        check("file = header + wifi rows + ble rows",
              r.file() == std::string(HDR) + r.wifi.row(0) + r.wifi.row(1) + r.ble.row(0));
        check("goodLen tracks the file", r.fs.goodLen == r.file().size());
        r.wifi.add(WRITE);
        x = r.batch();
        check("second batch appends only the new row", x.ok && x.rows == 1);
        check("header written once", occurrences(r.file(), HDR) == 1);
        check("old rows not repeated", occurrences(r.file(), r.wifi.row(0)) == 1);
        check("new row appended at the end",
              r.file().size() >= r.wifi.row(2).size() &&
              r.file().compare(r.file().size() - r.wifi.row(2).size(), std::string::npos, r.wifi.row(2)) == 0);
    }

    printf("an empty batch after the header does no I/O:\n");
    {
        Rig r;
        r.batch();                       // header only
        const int o = r.card.opens;
        Result x = r.batch();
        check("ok with zero rows", x.ok && x.rows == 0);
        check("card not opened", r.card.opens == o);
        r.wifi.add(WAIT);
        x = r.batch();
        check("a waiting row alone does no I/O either", x.ok && r.card.opens == o);
        check("and is counted as waiting", x.waiting == 1);
    }

    printf("a row not exportable yet is retried, then written exactly once:\n");
    {
        Rig r;
        r.wifi.add(WRITE); const uint32_t w = r.wifi.add(WAIT); r.wifi.add(WRITE);
        Result x = r.batch();
        check("2 written, 1 waiting", x.rows == 2 && x.waiting == 1);
        check("waiting row absent", occurrences(r.file(), r.wifi.row(w)) == 0);
        check("cursor low stops at the waiting row", r.curs[0].low == w);
        x = r.batch();
        check("still waiting: nothing written", x.rows == 0 && x.waiting == 1);
        r.wifi.v[w] = WRITE;             // it got a fix
        x = r.batch();
        check("now written", x.rows == 1 && occurrences(r.file(), r.wifi.row(w)) == 1);
        check("cursor low moves past everything", r.curs[0].low == 3);
        r.batch(); r.batch();
        check("never repeated", occurrences(r.file(), r.wifi.row(w)) == 1);
    }

    printf("a NEVER row is never written and does not hold the cursor back:\n");
    {
        Rig r;
        r.wifi.add(NEVER); r.wifi.add(WRITE);
        Result x = r.batch();
        check("1 row", x.rows == 1);
        check("never row absent", occurrences(r.file(), r.wifi.row(0)) == 0);
        check("cursor low past both", r.curs[0].low == 2);
    }

    printf("format() refusing a row (raced to unexportable) retries it:\n");
    {
        Rig r;
        r.wifi.add(WRITE);
        r.wifi.formatFails = true;
        Result x = r.batch();
        check("ok, nothing written but the header", x.ok && x.rows == 0 && r.file() == HDR);
        check("counted as waiting", x.waiting == 1);
        r.wifi.formatFails = false;
        x = r.batch();
        check("written on the next batch", x.rows == 1 && occurrences(r.file(), r.wifi.row(0)) == 1);
    }

    printf("the row cap bounds each flush; the rest follows in order:\n");
    {
        Rig r;
        for (int i = 0; i < 450; i++) r.wifi.add(WRITE);
        for (int i = 0; i < 30; i++) r.ble.add(WRITE);
        Result a = r.batch(200), b = r.batch(200), c = r.batch(200);
        check("200 / 200 / 80", a.rows == 200 && b.rows == 200 && c.rows == 80);
        check("more, more, done", a.more && b.more && !c.more);
        std::string want = HDR;
        for (uint32_t i = 0; i < 450; i++) want += r.wifi.row(i);
        for (uint32_t i = 0; i < 30; i++) want += r.ble.row(i);
        check("file is header + every row once, first-seen order", r.file() == want);
    }

    printf("a failed write does not advance the cursor and leaves no partial row:\n");
    {
        Rig r;
        r.wifi.add(WRITE); r.wifi.add(WRITE);
        r.batch();
        const std::string good = r.file();
        r.wifi.add(WRITE); r.wifi.add(WRITE); r.wifi.add(WRITE);
        r.card.failWriteAfter = 1; r.card.partialBytes = 5;   // 2nd row: 5 bytes land
        Result x = r.batch();
        check("batch reported failed", !x.ok);
        check("rolled back to the last good length", r.file() == good);
        check("goodLen unchanged", r.fs.goodLen == good.size());
        x = r.batch();
        check("retry writes all three", x.ok && x.rows == 3);
        for (uint32_t i = 0; i < 5; i++)
            check("  each row exactly once", occurrences(r.file(), r.wifi.row(i)) == 1);
    }

    printf("a failed close (flush) is a failure too:\n");
    {
        Rig r;
        r.wifi.add(WRITE);
        r.card.failClose = true;
        Result x = r.batch();
        check("not ok", !x.ok);
        check("row not marked done", !applog::isDone(r.wifi, 0));
        r.card.failClose = false;
        x = r.batch();
        check("retried: header + row exactly once",
              x.ok && r.file() == std::string(HDR) + r.wifi.row(0));
    }

    printf("when the rollback itself fails, the next batch repairs first:\n");
    {
        Rig r;
        r.wifi.add(WRITE);
        r.batch();
        const std::string good = r.file();
        r.wifi.add(WRITE);
        r.card.failWriteAfter = 0; r.card.partialBytes = 7; r.card.failTruncate = true;
        check("fails", !r.batch().ok);
        check("partial line left on the card", r.file().size() == good.size() + 7);
        r.card.failTruncate = false;
        Result x = r.batch();
        check("repair + append ok", x.ok && x.rows == 1);
        check("file = good + the row, no fragment", r.file() == good + r.wifi.row(1));
    }

    printf("repair that cannot truncate refuses to append (no duplicate fragment):\n");
    {
        Rig r;
        r.wifi.add(WRITE);
        r.batch();
        r.wifi.add(WRITE);
        r.card.failWriteAfter = 0; r.card.partialBytes = 3; r.card.failTruncate = true;
        r.batch();
        const std::string damaged = r.file();
        Result x = r.batch();
        check("still failing", !x.ok);
        check("nothing appended after the fragment", r.file() == damaged);
    }

    printf("a file we did not start is never truncated or appended to:\n");
    {
        Rig r;
        r.card.files["/A.CSV"] = "AN EARLIER SESSION\nhalf a li";
        r.wifi.add(WRITE);
        Result x = r.batch();
        check("refused", !x.ok);
        check("flagged as a name clash", x.clash);
        check("earlier file untouched", r.file() == "AN EARLIER SESSION\nhalf a li");
        check("no truncate attempted", r.card.truncates == 0);
        check("row still pending", !applog::isDone(r.wifi, 0));
    }

    printf("a file shorter than what we wrote is not ours any more: refuse:\n");
    {
        Rig r;
        r.wifi.add(WRITE);
        r.batch();
        r.card.files["/A.CSV"] = "XY";
        r.wifi.add(WRITE);
        Result x = r.batch();
        check("refused", !x.ok);
        check("flagged as shrunk (the caller starts a new file)", x.shrunk && !x.clash);
        check("file untouched", r.file() == "XY");
    }

    printf("close is verified against the size on the card:\n");
    check("clean and the right size: ok", closeVerified(true, 100, 50, 150));
    check("flush/fsync said ok but bytes are missing: not ok", !closeVerified(true, 100, 50, 146));
    check("more bytes than written: not ok", !closeVerified(true, 100, 50, 151));
    check("close itself failed: not ok", !closeVerified(false, 100, 50, 150));

    printf("open failure: nothing marked:\n");
    {
        Rig r;
        r.wifi.add(WRITE);
        r.card.failOpen = true;
        check("fails", !r.batch().ok);
        check("not done", !applog::isDone(r.wifi, 0));
        r.card.failOpen = false;
        check("then ok", r.batch().rows == 1);
    }

    printf("resetCursor starts the order over (new file / cleared table):\n");
    {
        Rig r;
        r.wifi.add(WRITE); r.wifi.add(NEVER);
        r.batch();
        applog::resetCursor(r.curs[0], r.wifi);
        check("bits cleared", !applog::isDone(r.wifi, 0) && !applog::isDone(r.wifi, 1));
        check("low back to 0", r.curs[0].low == 0);
        r.fs = applog::FileState{};
        Result x = r.batch(200, "/B.CSV");
        check("new file holds the row again", x.rows == 1 &&
              r.file("/B.CSV") == std::string(HDR) + r.wifi.row(0));
    }

    printf("count is bounded by capacity:\n");
    {
        Rig r;
        r.wifi.cap = 8;
        for (int i = 0; i < 12; i++) r.wifi.add(WRITE);
        Result x = r.batch();
        check("only 8 rows considered", x.rows == 8);
    }

    printf("exact-once over many cycles with random failures:\n");
    {
        srand(12345);
        Rig r;
        int failures = 0;
        for (int cycle = 0; cycle < 3000; cycle++) {
            // Rows arrive; some wait for a fix, a few never get a date.
            const int arrive = rand() % 6;
            for (int k = 0; k < arrive; k++) {
                const int p = rand() % 10;
                FakeSrc& s = (rand() % 4) ? r.wifi : r.ble;
                if (s.count() >= s.cap) continue;
                s.add(p < 6 ? WRITE : p < 9 ? WAIT : NEVER);
            }
            // Waiting rows get a fix now and then.
            for (FakeSrc* s : {&r.wifi, &r.ble})
                for (auto& x : s->v) if (x == WAIT && rand() % 20 == 0) x = WRITE;
            // The card misbehaves now and then.
            const int f = rand() % 25;
            r.card.failOpen = f == 0;
            r.card.failClose = f == 1;
            r.card.failTruncate = (rand() % 3) == 0;
            if (f == 2) { r.card.failWriteAfter = rand() % 4; r.card.partialBytes = rand() % 9; }
            Result x = r.batch(1 + rand() % 40);
            if (!x.ok) failures++;
            r.card.failWriteAfter = -1;
        }
        r.card.failOpen = r.card.failClose = r.card.failTruncate = false;
        for (FakeSrc* s : {&r.wifi, &r.ble}) for (auto& x : s->v) if (x == WAIT) x = WRITE;
        for (int i = 0; i < 1000; i++) if (!r.batch().more) break;
        r.batch();
        printf("     (%d injected failures, %u wifi + %u ble rows)\n", failures,
               (unsigned)r.wifi.count(), (unsigned)r.ble.count());
        check("some failures were injected", failures > 50);
        std::string f = r.file();
        check("starts with the header exactly once",
              f.compare(0, strlen(HDR), HDR) == 0 && occurrences(f, HDR) == 1);
        bool exact = true, neverOk = true;
        for (FakeSrc* s : {&r.wifi, &r.ble})
            for (uint32_t i = 0; i < s->count(); i++) {
                const int n = occurrences(f, s->row(i));
                if (s->v[i] == NEVER) { if (n != 0) neverOk = false; }
                else if (n != 1) exact = false;
            }
        check("every exportable row exactly once", exact);
        check("no NEVER row written", neverOk);
        // Every line complete: the file ends on a newline, and nothing but
        // header + whole rows sits in it.
        check("file ends on a newline", !f.empty() && f.back() == '\n');
        size_t lines = 0; for (char c : f) if (c == '\n') lines++;
        size_t want = 1;
        for (FakeSrc* s : {&r.wifi, &r.ble}) for (auto x : s->v) if (x != NEVER) want++;
        check("line count = header + rows", lines == want);
    }

    printf("row hold: located rows wait until the AP goes quiet or ages out:\n");
    {
        using applog::holdVerdict;
        const uint32_t Q = HOLD_QUIET_MS, M = HOLD_MAX_MS;
        check("HOLD_QUIET_MS is 20 s", Q == 20000);
        check("HOLD_MAX_MS is 120 s", M == 120000);
        check("no UTC at first sight: NEVER", holdVerdict(true, 0, 0, 0, 999999, true) == NEVER);
        check("not located: WAIT, even when final", holdVerdict(false, 7, 0, 0, 999999, true) == WAIT);
        check("located, still being heard: held", holdVerdict(true, 7, 1000, 5000, 5000 + Q - 1, false) == WAIT);
        check("quiet for HOLD_QUIET_MS: written", holdVerdict(true, 7, 1000, 5000, 5000 + Q, false) == WRITE);
        check("parked AP heard constantly: written at HOLD_MAX_MS",
              holdVerdict(true, 7, 1000, 1000 + M, 1000 + M, false) == WRITE);
        check("...and not a moment before",
              holdVerdict(true, 7, 1000, 1000 + M - 1, 1000 + M - 1, false) == WAIT);
        check("final writes a held row", holdVerdict(true, 7, 1000, 5000, 5001, true) == WRITE);
        check("millis() wrap is handled",
              holdVerdict(true, 7, 0xFFFFF000u, 0xFFFFF000u, 0xFFFFF000u + Q, false) == WRITE);
        // A sighting landing during the scan has lastMs/firstMs after `now`.
        check("heard 5 ms after now: still in range, held",
              holdVerdict(true, 7, 1000, 50005, 50000, false) == WAIT);
        check("first seen after now: held", holdVerdict(true, 7, 50003, 50003, 50000, false) == WAIT);
        check("...even across a millis() wrap",
              holdVerdict(true, 7, 0xFFFFFFF0u, 3u, 0xFFFFFFFEu, false) == WAIT);
        check("forward wrap still releases at the quiet time",
              holdVerdict(true, 7, 0xFFFFFFF0u, 0xFFFFFFF0u, 0xFFFFFFF0u + Q + 5, false) == WRITE);
        check("final still releases a row heard after now",
              holdVerdict(true, 7, 50003, 50005, 50000, true) == WRITE);
    }

    printf("a held row is written once, with the loudest position seen by then:\n");
    {
        // A source whose rows carry timing and a position that improves while
        // the AP is still in range -- what capture's best-RSSI rule does.
        struct HeldSrc : applog::Source {
            struct R { uint32_t firstMs, lastMs; int lat; };
            std::vector<R> r; std::vector<uint8_t> bits = std::vector<uint8_t>(64);
            uint32_t count() override { return (uint32_t)r.size(); }
            uint32_t capacity() override { return 512; }
            uint8_t* doneBits() override { return bits.data(); }
            applog::Verdict verdict(uint32_t i, uint32_t now, bool final) override {
                return applog::holdVerdict(true, 7, r[i].firstMs, r[i].lastMs, now, final);
            }
            int format(uint32_t i, char* b, int n) override {
                return snprintf(b, (size_t)n, "H%u,lat=%d\n", (unsigned)i, r[i].lat);
            }
        } held;
        FakeCard card;
        applog::Source* srcs[1] = {&held};
        applog::Cursor curs[1];
        applog::FileState fs;
        applog::Pending pend[applog::BATCH_ROWS];
        auto batch = [&](uint32_t now, bool final) {
            return applog::appendBatch("/H.CSV", fs, card, HDR, srcs, curs, 1,
                                       applog::BATCH_ROWS, pend, now, final);
        };
        held.r.push_back({0, 0, 1});            // first located at the edge of range
        Result x = batch(10000, false);
        check("still being heard: held", x.ok && x.rows == 0 && x.waiting == 1);
        held.r[0].lastMs = 15000; held.r[0].lat = 5;  // louder, closer
        x = batch(30000, false);
        check("heard 15 s ago: still held", x.rows == 0);
        x = batch(35000, false);
        check("quiet for 20 s: written", x.rows == 1);
        check("with the loudest position, not the first",
              card.files["/H.CSV"] == std::string(HDR) + "H0,lat=5\n");
        x = batch(90000, false);
        check("never again", x.rows == 0 && occurrences(card.files["/H.CSV"], "H0,") == 1);

        held.r.push_back({100000, 100000, 2});  // parked beside it
        for (uint32_t t = 100000; t < 100000 + HOLD_MAX_MS; t += 10000) {
            held.r[1].lastMs = t;
            batch(t, false);
        }
        check("parked AP held below the cap", occurrences(card.files["/H.CSV"], "H1,") == 0);
        held.r[1].lastMs = 100000 + HOLD_MAX_MS;
        batch(100000 + HOLD_MAX_MS, false);
        check("written at the 120 s cap", occurrences(card.files["/H.CSV"], "H1,") == 1);

        held.r.push_back({300000, 300000, 3});
        x = batch(300001, true);
        check("final (STOP / roll / RESET) writes a held row at once",
              x.rows == 1 && occurrences(card.files["/H.CSV"], "H2,") == 1);
        check("held rows pin the cursor only while held", curs[0].low == 3);
    }

    printf("rollover decision:\n");
    check("one below ROLLOVER_APS: no", !rolloverDue(ROLLOVER_APS - 1, 0));
    check("at ROLLOVER_APS: yes", rolloverDue(ROLLOVER_APS, 0));
    check("ROLLOVER_APS is 20000", ROLLOVER_APS == 20000);
    check("BLE below its threshold: no", !rolloverDue(0, ROLLOVER_BLE - 1));
    check("BLE at its threshold: yes", rolloverDue(0, ROLLOVER_BLE));
    check("BLE threshold keeps its 4096 table <= 65% full", ROLLOVER_BLE * 100 <= 4096u * 65);

    printf(fails ? "\n%d FAILED\n" : "\nall passed\n", fails);
    return fails ? 1 : 0;
}
