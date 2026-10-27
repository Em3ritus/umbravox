// Host test for the logging state machine. sdlog is faked with an in-memory
// card, and the capture tables with fake row sources, so start/stop/timer/
// resume, append-only flushing, the rollover and RESET COUNTERS are pinned
// without hardware.
#include "../device/logging.h"
#include "../device/sdlog.h"
#include "../device/wigle.h"
#include <stdio.h>
#include <string.h>
#include <map>
#include <string>
#include <vector>

// --- fake card (sdlog) ----------------------------------------------------
namespace {
bool cardPresent = true;
uint32_t maxOnCard = 0;
std::map<std::string, std::string> files;
std::string openName;
bool isOpen = false, failWrites = false;
// A card whose last flush silently fails: the tail of each append never
// lands. `verifyClose` = sdlog's size check after the flush (the fix).
int  dropTail = 0; bool verifyClose = true;
uint32_t sizeAtOpen = 0, written = 0;
int opens = 0;
}
namespace sdlog {
bool begin() { return cardPresent; }
bool present() { return cardPresent; }
uint32_t maxIndex(const char*, const char*) { return maxOnCard; }
bool exists(const char* name) { return cardPresent && files.count(name); }
bool openAppend(const char* name, uint32_t& size) {
    if (!cardPresent) return false;
    opens++;
    openName = name; isOpen = true;
    size = (uint32_t)files[openName].size();
    sizeAtOpen = size; written = 0;
    return true;
}
bool writeAppend(const char* d, uint32_t len) {
    if (!isOpen || failWrites) return false;
    files[openName].append(d, len);
    written += len;
    return true;
}
bool closeAppend() {
    const bool was = isOpen; isOpen = false;
    std::string& f = files[openName];
    if (dropTail) f.resize(f.size() > (size_t)dropTail ? f.size() - dropTail : 0);
    const bool ok = was && !failWrites;
    return verifyClose ? applog::closeVerified(ok, sizeAtOpen, written, (uint32_t)f.size()) : ok;
}
bool truncateTo(const char* name, uint32_t len) {
    if (!cardPresent || !files.count(name)) return false;
    if (files[name].size() > len) files[name].resize(len);
    return true;
}
}

// --- fake capture tables ----------------------------------------------------
struct FakeSrc : applog::Source {
    const char* tag;
    std::vector<applog::Verdict> v;
    std::vector<uint8_t> bits = std::vector<uint8_t>(32768 / 8);
    explicit FakeSrc(const char* t) : tag(t) {}
    uint32_t count() override { return (uint32_t)v.size(); }
    uint32_t capacity() override { return 32768; }
    uint8_t* doneBits() override { return bits.data(); }
    // held = rows that follow capture's hold rule (located, still heard).
    std::vector<bool> heldRow;
    applog::Verdict verdict(uint32_t i, uint32_t now, bool final) override {
        if (i < heldRow.size() && heldRow[i])
            return applog::holdVerdict(true, 7, now, now, now, final);  // heard just now
        return v[i];
    }
    int format(uint32_t i, char* buf, int n) override {
        return snprintf(buf, (size_t)n, "%s%05u\n", tag, (unsigned)i);
    }
    void add(int n, applog::Verdict x = applog::WRITE) { while (n--) v.push_back(x); }
};
FakeSrc wifi("W"), ble("B");
uint32_t wifiU() { return (uint32_t)wifi.v.size(); }
uint32_t bleU()  { return (uint32_t)ble.v.size(); }
int clears = 0;
void clearTables() { clears++; wifi.v.clear(); ble.v.clear(); }
int rolls = 0; bool lastForced = false, lastReset = false;
uint32_t rollAps = 0, lastLost = 0, totalLost = 0; std::string rollName;
void onRoll(const logging::RollInfo& r) {
    rolls++; rollAps = r.wifiAps; rollName = r.newName; lastForced = r.forced;
    lastReset = r.reset; lastLost = r.lost; totalLost += r.lost;
}

static logging::Hooks hooks() {
    logging::Hooks h;
    h.wifi = &wifi; h.ble = &ble;
    h.wifiUniques = wifiU; h.bleUniques = bleU;
    h.clearTables = clearTables; h.onRollover = onRoll;
    return h;
}
static void fresh() {
    files.clear(); wifi.v.clear(); ble.v.clear(); wifi.heldRow.clear(); ble.heldRow.clear();
    cardPresent = true; failWrites = false; maxOnCard = 0; dropTail = 0; verifyClose = true;
    clears = rolls = 0; opens = 0; totalLost = 0;
}
static std::string header() {
    struct S : ui_wigle::Sink { std::string s; void put(const char* p) override { s += p; } } s;
    ui_wigle::header(s);
    return s.s;
}
static int occurrences(const std::string& hay, const std::string& needle) {
    int n = 0;
    for (size_t p = hay.find(needle); p != std::string::npos; p = hay.find(needle, p + 1)) n++;
    return n;
}
static std::string row(const char* tag, unsigned i) {
    char b[16]; snprintf(b, sizeof b, "%s%05u\n", tag, i); return b;
}

static int fails = 0;
static void check(const char* what, bool ok) {
    printf(ok ? "  ok   %s\n" : "  FAIL %s\n", what);
    if (!ok) fails++;
}

constexpr uint32_t T0 = 1792972800u;    // 2026-10-26, a known date

int main() {
    using namespace logging;

    printf("start opens a session file with the WiGLE header:\n");
    fresh();
    begin(false, hooks(), 0, T0);
    check("begins idle", state() == IDLE);
    wifi.add(3); ble.add(1);
    start(1000, T0);
    check("now active", state() == ACTIVE);
    const std::string f1 = fileName();
    check("file created", files.count(f1) == 1);
    check("file = header + the rows already in the table",
          files[f1] == header() + row("W", 0) + row("W", 1) + row("W", 2) + row("B", 0));

    printf("the timer appends only new rows, at FLUSH_MS and not before:\n");
    wifi.add(2);
    const std::string before = files[f1];
    tick(1000 + FLUSH_MS - 1, T0);
    check("no flush before the interval", files[f1] == before);
    tick(1000 + FLUSH_MS, T0);
    check("appended exactly the two new rows",
          files[f1] == before + row("W", 3) + row("W", 4));
    check("header once", occurrences(files[f1], header()) == 1);
    check("lastRows counts the session's rows", lastRows() == 6);

    printf("rows waiting for a fix are retried and counted:\n");
    wifi.add(1, applog::WAIT);
    tick(1000 + 2 * FLUSH_MS, T0);
    check("waiting row not written", occurrences(files[f1], row("W", 5)) == 0);
    check("lastSkipped reports it", lastSkipped() == 1);
    wifi.v[5] = applog::WRITE;
    tick(1000 + 3 * FLUSH_MS, T0);
    check("written once it can be", occurrences(files[f1], row("W", 5)) == 1);

    printf("stop forces a final append and closes:\n");
    wifi.add(1);
    stop(1000 + 3 * FLUSH_MS + 5);
    check("final row on the card", occurrences(files[f1], row("W", 6)) == 1);
    check("now idle", state() == IDLE);
    check("file name cleared", fileName()[0] == 0);

    printf("a backlog drains in capped batches, catching up quickly:\n");
    fresh();
    begin(false, hooks(), 0, T0);
    wifi.add(450);
    start(0, T0);
    const std::string fb = fileName();
    check("first flush capped at BATCH_ROWS",
          occurrences(files[fb], "\nW") == (int)applog::BATCH_ROWS);
    tick(CATCHUP_MS - 1, T0);
    check("not before CATCHUP_MS", occurrences(files[fb], "\nW") == (int)applog::BATCH_ROWS);
    tick(CATCHUP_MS, T0);
    check("catch-up after CATCHUP_MS", occurrences(files[fb], "\nW") == 2 * (int)applog::BATCH_ROWS);
    tick(2 * CATCHUP_MS, T0);
    check("all 450 rows, each once", occurrences(files[fb], "\nW") == 450);
    stop(3 * CATCHUP_MS);

    printf("toggle alternates:\n");
    fresh();
    begin(false, hooks(), 0, T0);
    toggle(10, T0); check("toggle -> active", state() == ACTIVE);
    toggle(20, 0);  check("toggle -> idle", state() == IDLE);

    printf("auto-resume:\n");
    begin(true, hooks(), 0, T0);
    check("begin(wasActive) resumes active", state() == ACTIVE);
    begin(false, hooks(), 0, T0);
    check("begin(idle) stays idle", state() == IDLE);

    printf("NO_CARD is entered and left:\n");
    fresh();
    cardPresent = false;
    begin(false, hooks(), 0, T0);
    start(0, T0);
    check("start with no card -> NO_CARD", state() == NO_CARD);
    check("wantsActive is true despite no card", wantsActive());
    cardPresent = true;
    tick(100, T0);
    check("card appears -> ACTIVE", state() == ACTIVE);

    printf("a later session starts a new file and logs the table again:\n");
    fresh();
    wifi.add(2);
    begin(true, hooks(), 0, T0);
    const std::string a = fileName();
    const std::string aBody = files[a];
    stop(50);
    begin(true, hooks(), 60, T0 + 60);
    check("a different file", a != fileName());
    check("the new file has the rows again", files[fileName()] == header() + row("W", 0) + row("W", 1));
    check("the earlier file untouched", files[a] == aBody);

    printf("dateless counter advances past files already on the card:\n");
    fresh();
    maxOnCard = 7;
    begin(false, hooks(), 0, 0);
    start(0, 0);
    check("names past WD-0007 -> WD-0008", strcmp(fileName(), "/WD-0008.CSV") == 0);

    printf("a name that already exists is never reused:\n");
    fresh();
    files["/WD-20261026-000000.CSV"] = "EARLIER SESSION\n";
    files["/WD-0001.CSV"] = "EARLIER DATELESS\n";      // maxIndex missed it
    begin(false, hooks(), 0, T0);
    wifi.add(1);
    start(0, T0);
    check("not the dated name", strcmp(fileName(), "/WD-20261026-000000.CSV") != 0);
    check("not the existing dateless name", strcmp(fileName(), "/WD-0001.CSV") != 0);
    check("earlier dated file untouched", files["/WD-20261026-000000.CSV"] == "EARLIER SESSION\n");
    check("earlier dateless file untouched", files["/WD-0001.CSV"] == "EARLIER DATELESS\n");
    check("the session landed in its own file", files[fileName()] == header() + row("W", 0));

    printf("a failed write latches the error and loses nothing:\n");
    fresh();
    begin(false, hooks(), 0, T0);
    start(0, T0);
    const std::string fe = fileName();
    wifi.add(2);
    failWrites = true;
    tick(FLUSH_MS, T0);
    check("error latched", takeWriteError());
    check("latch clears on read", !takeWriteError());
    check("still active", state() == ACTIVE);
    failWrites = false;
    tick(2 * FLUSH_MS, T0);
    check("rows land on the retry, once each",
          files[fe] == header() + row("W", 0) + row("W", 1));
    stop(2 * FLUSH_MS + 1);

    printf("rollover at ROLLOVER_APS: drain, new file, cleared table:\n");
    fresh();
    begin(false, hooks(), 0, T0);
    start(0, T0);
    const std::string r1 = fileName();
    wifi.add((int)applog::ROLLOVER_APS - 1);
    // Drain the backlog so the old file is complete before the roll.
    for (uint32_t t = CATCHUP_MS; t < 200 * CATCHUP_MS && occurrences(files[r1], "\nW") < (int)applog::ROLLOVER_APS - 1; t += CATCHUP_MS)
        tick(t, T0);
    check("no roll below the threshold", rolls == 0 && clears == 0);
    wifi.add(1);                                   // the ROLLOVER_APS-th AP
    uint32_t t = 300 * CATCHUP_MS;
    tick(t, T0 + 300);
    check("rolled once", rolls == 1 && clears == 1);
    check("reported the AP count", rollAps == applog::ROLLOVER_APS);
    check("not forced", !lastForced);
    check("a new file", rollName == fileName() && rollName != r1);
    check("old file holds every row exactly once, header once",
          occurrences(files[r1], "\nW") == (int)applog::ROLLOVER_APS &&
          occurrences(files[r1], header()) == 1 &&
          occurrences(files[r1], row("W", applog::ROLLOVER_APS - 1)) == 1);
    check("new file starts with the header", files[fileName()] == header());
    check("still active", state() == ACTIVE);
    const std::string oldBody = files[r1];
    wifi.add(2);                                   // capture continues
    tick(t + FLUSH_MS, T0 + 400);
    check("new rows go to the new file, from position 0",
          files[fileName()] == header() + row("W", 0) + row("W", 1));
    check("old file not touched after the roll", files[r1] == oldBody);
    check("rollovers() counts", rollovers() == 1);
    stop(t + FLUSH_MS + 1);

    printf("rollover waits for the backlog to drain:\n");
    fresh();
    begin(false, hooks(), 0, T0);
    start(0, T0);
    const std::string r2 = fileName();
    wifi.add((int)applog::ROLLOVER_APS - 4000);
    uint32_t tw = 0;
    while (occurrences(files[r2], "\nW") < (int)applog::ROLLOVER_APS - 4000) { tw += CATCHUP_MS; tick(tw, T0); }
    wifi.add(4000);                                // a 4000-row burst crosses the line
    tw += CATCHUP_MS; tick(tw, T0);
    check("not rolled while rows are pending", rolls == 0);
    for (int k = 0; k < 40 && rolls == 0; k++) { tw += CATCHUP_MS; tick(tw, T0); }
    check("rolled after the drain, not forced", rolls == 1 && !lastForced && lastLost == 0);
    check("every row reached the old file first", occurrences(files[r2], "\nW") == (int)applog::ROLLOVER_APS);
    stop(tw + 1);

    printf("rollover with a failing card is forced after ROLL_MAX_WAIT_MS:\n");
    fresh();
    begin(false, hooks(), 0, T0);
    start(0, T0);
    failWrites = true;
    wifi.add((int)applog::ROLLOVER_APS);
    tick(CATCHUP_MS, T0);
    check("not rolled at once", rolls == 0);
    tick(CATCHUP_MS + ROLL_MAX_WAIT_MS - 1, T0);
    check("not before ROLL_MAX_WAIT_MS", rolls == 0);
    tick(CATCHUP_MS + ROLL_MAX_WAIT_MS, T0);
    check("forced roll keeps capture going", rolls == 1 && clears == 1 && lastForced);
    check("every unwritten row counted", lastLost == applog::ROLLOVER_APS);
    failWrites = false;
    stop(CATCHUP_MS + ROLL_MAX_WAIT_MS + 1);

    printf("BLE reaching its threshold rolls too:\n");
    fresh();
    begin(false, hooks(), 0, T0);
    start(0, T0);
    ble.add((int)applog::ROLLOVER_BLE);
    for (uint32_t k = 1; k < 100 && rolls == 0; k++) tick(k * CATCHUP_MS, T0);
    check("rolled on BLE", rolls == 1 && clears == 1);
    stop(200 * CATCHUP_MS);

    printf("rollover while not logging just clears the table:\n");
    fresh();
    begin(false, hooks(), 0, T0);
    wifi.add((int)applog::ROLLOVER_APS);
    tick(10, T0);
    check("cleared", clears == 1 && rolls == 1);
    check("no file written", files.empty());
    check("still idle", state() == IDLE);

    printf("RESET COUNTERS while logging is a rollover, never a lost cursor:\n");
    fresh();
    begin(false, hooks(), 0, T0);
    wifi.add(3);
    start(0, T0);
    const std::string s1 = fileName();
    wifi.add(2);                                   // pending, not flushed yet
    int userClears = 0;
    struct UC { static void clear() { wifi.v.clear(); ble.v.clear(); } };
    resetTables(UC::clear, 500, T0 + 5);
    userClears = (int)wifi.v.size();
    check("pending rows reached the old file first",
          files[s1] == header() + row("W", 0) + row("W", 1) + row("W", 2) + row("W", 3) + row("W", 4));
    check("a new session file", strcmp(fileName(), s1.c_str()) != 0);
    check("the new file has the header", files[fileName()] == header());
    check("table cleared", userClears == 0);
    const std::string s1Body = files[s1];
    wifi.add(2);                                   // post-reset sightings
    tick(500 + FLUSH_MS, T0 + 20);
    check("post-reset rows logged in the new file from position 0",
          files[fileName()] == header() + row("W", 0) + row("W", 1));
    check("the old file kept every pre-reset row", files[s1] == s1Body);
    stop(500 + FLUSH_MS + 1);

    printf("RESET COUNTERS while idle just clears:\n");
    fresh();
    begin(false, hooks(), 0, T0);
    wifi.add(3);
    resetTables(UC::clear, 10, T0);
    check("no file", files.empty() && state() == IDLE);
    wifi.add(1);
    start(20, T0);
    check("the next session logs from position 0", files[fileName()] == header() + row("W", 0));

    printf("a sustained flood above the drain rate still rolls, by the timer:\n");
    {
        fresh();
        begin(false, hooks(), 0, T0);
        start(0, T0);
        const std::string old = fileName();
        const uint32_t R = 400;                    // new APs per second, > 200/s drain
        uint32_t t = 0, maxSeen = 0, dueAt = 0, rolledAt = 0, atRoll = 0;
        bool due = false;
        while (t < 200000 && rolls == 0) {
            t += 100;
            wifi.add((int)(R / 10));
            if (!due && wifi.v.size() >= applog::ROLLOVER_APS) { due = true; dueAt = t; }
            if (wifi.v.size() > maxSeen) maxSeen = (uint32_t)wifi.v.size();
            atRoll = (uint32_t)wifi.v.size();
            tick(t, T0);
            if (rolls) rolledAt = t;
        }
        printf("     (due at %u ms, rolled at %u ms, peak %u APs, lost %u)\n",
               (unsigned)dueAt, (unsigned)rolledAt, (unsigned)maxSeen, (unsigned)lastLost);
        check("rolled", rolls == 1 && clears == 1);
        check("within ROLL_MAX_WAIT_MS of becoming due", rolledAt - dueAt <= ROLL_MAX_WAIT_MS);
        check("ROLL_MAX_WAIT_MS is 30 s", ROLL_MAX_WAIT_MS == 30000);
        check("table never past the hard ceiling + one flush interval of arrivals",
              maxSeen <= applog::ROLL_HARD_APS + R * FLUSH_MS / 1000);
        check("forced, with a counted loss", lastForced && lastLost > 0);
        check("every row is on the card or counted lost (never silent)",
              (uint32_t)occurrences(files[old], "\nW") + lastLost == atRoll);
        stop(t + 1);
    }

    printf("a faster flood rolls at the hard ceiling, before the timer:\n");
    {
        fresh();
        begin(false, hooks(), 0, T0);
        start(0, T0);
        uint32_t t = 0, maxSeen = 0;
        while (t < 200000 && rolls == 0) {
            t += 100;
            wifi.add(500);                          // 5000 APs/s
            if (wifi.v.size() > maxSeen) maxSeen = (uint32_t)wifi.v.size();
            tick(t, T0);
        }
        check("ROLL_HARD_APS is 27000", applog::ROLL_HARD_APS == 27000);
        check("rolled by the ceiling (well inside 30 s)", rolls == 1 && t < 10000);
        check("never past the ceiling + one tick of arrivals", maxSeen <= applog::ROLL_HARD_APS + 500);
        check("BLE has its own ceiling under its 4096 table",
              applog::ROLL_HARD_BLE > applog::ROLLOVER_BLE && applog::ROLL_HARD_BLE < 4096);
        stop(t + 1);
    }

    printf("the forced roll writes at most FINAL_BATCHES first:\n");
    {
        fresh();
        begin(false, hooks(), 0, T0);
        start(0, T0);
        const std::string old = fileName();
        failWrites = true;
        wifi.add((int)applog::ROLLOVER_APS);
        tick(100, T0);
        failWrites = false;                          // the card recovers...
        wifi.add((int)(applog::ROLL_HARD_APS - applog::ROLLOVER_APS));   // ...a burst hits the ceiling
        tick(200, T0);
        check("rolled, forced", rolls == 1 && lastForced);
        check("old file got at most FINAL_BATCHES * BATCH_ROWS rows",
              occurrences(files[old], "\nW") <= FINAL_BATCHES * (int)applog::BATCH_ROWS);
        check("the rest is counted",
              (uint32_t)occurrences(files[old], "\nW") + lastLost == applog::ROLL_HARD_APS);
        stop(300);
    }

    printf("RESET with a 5000-row backlog: every row on the card or counted:\n");
    {
        fresh();
        begin(false, hooks(), 0, T0);
        start(0, T0);
        const std::string old = fileName();
        tick(FLUSH_MS, T0);
        wifi.add(5000);
        resetTables(UC::clear, FLUSH_MS + 1, T0 + 1);
        check("not cleared while rows are still going to the card", rolls == 0 || clears == 0);
        uint32_t t = FLUSH_MS + 1;
        for (int k = 0; k < 100 && rolls == 0; k++) { t += CATCHUP_MS; tick(t, T0 + 1); }
        check("the reset completed", rolls == 1 && lastReset);
        check("table cleared by the user's clear", wifi.v.empty());
        check("old file has all 5000 rows (drained in time, nothing lost)",
              occurrences(files[old], "\nW") == 5000 && totalLost == 0);
        check("a new file", fileName() != old);
        stop(t + 1);

        fresh();
        begin(false, hooks(), 0, T0);
        start(0, T0);
        const std::string old2 = fileName();
        wifi.add(5000);
        failWrites = true;                           // the card is failing
        resetTables(UC::clear, 10, T0 + 1);
        t = 10;
        for (int k = 0; k < 100 && rolls == 0; k++) { t += CATCHUP_MS; tick(t, T0 + 1); }
        check("forced after ROLL_MAX_WAIT_MS", rolls == 1 && lastForced && lastReset && t <= 10 + ROLL_MAX_WAIT_MS + CATCHUP_MS);
        check("the loss is counted, not silent",
              (uint32_t)occurrences(files[old2], "\nW") + lastLost == 5000);
        failWrites = false;
        stop(t + 1);
    }

    printf("a silently failed fsync is caught by the size check and retried:\n");
    {
        fresh();
        begin(false, hooks(), 0, T0);
        start(0, T0);
        const std::string f = fileName();
        wifi.add(3);
        dropTail = 4;                              // the final flush loses 4 bytes
        tick(FLUSH_MS, T0);
        check("reported as a write error", takeWriteError());
        check("nothing committed: the fragment is cut back", files[f] == header());
        dropTail = 0;
        tick(2 * FLUSH_MS, T0);
        check("retried into the same file, each row once",
              files[f] == header() + row("W", 0) + row("W", 1) + row("W", 2));
        stop(2 * FLUSH_MS + 1);
    }

    printf("a file shorter than committed (undetected loss) moves to a new file:\n");
    {
        fresh();
        begin(false, hooks(), 0, T0);
        start(0, T0);
        const std::string f = fileName();
        wifi.add(3);
        verifyClose = false; dropTail = 4;         // a sink that cannot tell
        tick(FLUSH_MS, T0);
        verifyClose = true; dropTail = 0;
        wifi.add(1);
        tick(2 * FLUSH_MS, T0);
        check("the short file is left alone", files[f].size() + 4 == header().size() + 3 * row("W", 0).size());
        check("the restart is reported once", takeFileRestart() && !takeFileRestart());
        check("a new session file", fileName() != f);
        tick(3 * FLUSH_MS, T0);
        check("the new file has the whole table, each row once",
              files[fileName()] == header() + row("W", 0) + row("W", 1) + row("W", 2) + row("W", 3));
        stop(3 * FLUSH_MS + 1);
    }

    printf("held rows (AP still in range) are released by STOP, rollover and RESET:\n");
    fresh();
    begin(false, hooks(), 0, T0);
    start(0, T0);
    const std::string h1 = fileName();
    wifi.add(1); wifi.heldRow = {true};
    tick(FLUSH_MS, T0);
    check("a routine flush keeps it held", occurrences(files[h1], "\nW") == 0);
    stop(FLUSH_MS + 1);
    check("STOP writes it", occurrences(files[h1], row("W", 0)) == 1);

    fresh();
    begin(false, hooks(), 0, T0);
    start(0, T0);
    const std::string h2 = fileName();
    wifi.add(1); wifi.heldRow = {true};
    resetTables(UC::clear, 100, T0 + 1);
    check("RESET writes it to the old file first", occurrences(files[h2], row("W", 0)) == 1);
    stop(200);

    fresh();
    begin(false, hooks(), 0, T0);
    start(0, T0);
    const std::string h3 = fileName();
    wifi.add((int)applog::ROLLOVER_APS - 1);
    uint32_t th = 0;
    while (occurrences(files[h3], "\nW") < (int)applog::ROLLOVER_APS - 1) { th += CATCHUP_MS; tick(th, T0); }
    wifi.add(1);
    wifi.heldRow.assign(applog::ROLLOVER_APS, false);
    wifi.heldRow[applog::ROLLOVER_APS - 1] = true;      // the last AP is still in range
    for (uint32_t k = 1; k < 300 && rolls == 0; k++) tick(th + k * CATCHUP_MS, T0);
    check("rollover writes it to the old file before the clear",
          rolls == 1 && occurrences(files[h3], row("W", applog::ROLLOVER_APS - 1)) == 1);
    stop(400 * CATCHUP_MS);

    printf(fails ? "\n%d FAILED\n" : "\nall passed\n", fails);
    return fails ? 1 : 0;
}
