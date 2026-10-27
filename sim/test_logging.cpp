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
    return true;
}
bool writeAppend(const char* d, uint32_t len) {
    if (!isOpen || failWrites) return false;
    files[openName].append(d, len);
    return true;
}
bool closeAppend() { const bool was = isOpen; isOpen = false; return was && !failWrites; }
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
    applog::Verdict verdict(uint32_t i) override { return v[i]; }
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
int rolls = 0; bool lastForced = false;
uint32_t rollAps = 0; std::string rollName;
void onRoll(uint32_t aps, uint32_t, const char* nm, bool forced) {
    rolls++; rollAps = aps; rollName = nm; lastForced = forced;
}

static logging::Hooks hooks() {
    logging::Hooks h;
    h.wifi = &wifi; h.ble = &ble;
    h.wifiUniques = wifiU; h.bleUniques = bleU;
    h.clearTables = clearTables; h.onRollover = onRoll;
    return h;
}
static void fresh() {
    files.clear(); wifi.v.clear(); ble.v.clear();
    cardPresent = true; failWrites = false; maxOnCard = 0;
    clears = rolls = 0; opens = 0;
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
    wifi.add((int)applog::ROLLOVER_APS);           // all arrive at once
    tick(CATCHUP_MS, T0);
    check("not rolled while rows are pending", rolls == 0);
    for (uint32_t k = 2; k < 200 && rolls == 0; k++) tick(k * CATCHUP_MS, T0);
    check("rolled after the drain", rolls == 1);
    check("every row reached the old file first", occurrences(files[r2], "\nW") == (int)applog::ROLLOVER_APS);
    stop(400 * CATCHUP_MS);

    printf("rollover with a failing card is forced after ROLL_GRACE_MS:\n");
    fresh();
    begin(false, hooks(), 0, T0);
    start(0, T0);
    failWrites = true;
    wifi.add((int)applog::ROLLOVER_APS);
    tick(CATCHUP_MS, T0);
    check("not rolled at once", rolls == 0);
    tick(CATCHUP_MS + ROLL_GRACE_MS - 1, T0);
    check("not before the grace period", rolls == 0);
    tick(CATCHUP_MS + ROLL_GRACE_MS, T0);
    check("forced roll keeps capture going", rolls == 1 && clears == 1 && lastForced);
    failWrites = false;
    stop(CATCHUP_MS + ROLL_GRACE_MS + 1);

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

    printf(fails ? "\n%d FAILED\n" : "\nall passed\n", fails);
    return fails ? 1 : 0;
}
