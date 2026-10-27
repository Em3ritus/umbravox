// See logging.h. Pure: the card is reached only through sdlog.h, so this
// compiles and tests on the host with no Arduino.
#include "logging.h"
#include "sdlog.h"
#include "csv.h"
#include "wigle.h"
#include <stdio.h>
#include <string.h>

namespace logging {
namespace {

State    st = IDLE;
Hooks    hk;
bool     want = false;
uint32_t startedMs = 0;
uint32_t nextFlushMs = 0;
uint32_t rows = 0, skipped = 0;
uint32_t nRolls = 0;
// Names sessions when no date is known yet. The in-memory counter restarts at
// 0 each boot, so begin() seeds it from the card (sdlog::maxIndex scans for
// existing WD-NNNN.CSV/.csv, any case) to advance PAST them -- so a reboot before a GPS fix
// cannot overwrite an earlier dateless session (spec s7).
uint32_t counter = 0;
char     name[32] = "";
char     prevName[32] = "";  // the last session's file, never reused
// Latched when a flush fails with the card STILL present (full / I/O error, as
// opposed to a card that was simply removed). main reads+clears it and writes
// one diagnostic line, so this file keeps no dependency on elog.
bool     writeErr = false;
// Latched when the session file came up shorter than committed and logging
// moved to a new file (main logs it).
bool     fileRestart = false;

// The append machinery: one cursor per source, the session file's state, and
// the batch scratch (static: the loop task's stack is only 8 KB).
applog::Source*   srcs[2];
applog::Cursor    curs[2];
int               nSrc = 0;
applog::FileState fst;
applog::Pending   pend[applog::BATCH_ROWS];
char              header[320] = "";

// A rollover that is due but waiting for pending rows to reach the card.
// rollDueAt is set once, when it first becomes due, and never refreshed.
bool     rollWaiting = false;
uint32_t rollDueAt = 0, nextRollTry = 0;
// A RESET COUNTERS waiting on the same path: its clear replaces clearTables.
void   (*resetClear)() = nullptr;

// Anything earlier than this is the RTC's power-on default, not a real date.
constexpr uint32_t EPOCH_KNOWN = 1577836800u;   // 2020-01-01 00:00:00 UTC

// sdlog's append calls as an applog::Sink.
struct SdSink : applog::Sink {
    bool open(const char* n, uint32_t& size) override { return sdlog::openAppend(n, size); }
    bool write(const char* d, uint32_t len) override { return sdlog::writeAppend(d, len); }
    bool close() override { return sdlog::closeAppend(); }
    bool truncate(const char* n, uint32_t len) override { return sdlog::truncateTo(n, len); }
} sink;

struct HeaderSink : ui_wigle::Sink {
    void put(const char* s) override {
        strncat(header, s, sizeof header - strlen(header) - 1);
    }
};

// Absolute path -- SD.open requires the leading '/'. A name already on the
// card is never used: appending to it would mix two sessions (and applog
// refuses it anyway), so fall through to the next free dateless number.
void makeName(uint32_t nowEpoch) {
    if (nowEpoch >= EPOCH_KNOWN) {
        char ts[24];
        csv::stampUtc(ts, nowEpoch);              // "YYYY-MM-DD HH:MM:SS"
        snprintf(name, sizeof name, "/WD-%.4s%.2s%.2s-%.2s%.2s%.2s.csv",
                 ts, ts + 5, ts + 8, ts + 11, ts + 14, ts + 17);
        // Two sessions in the same second would share a name.
        if (strcmp(name, prevName) != 0 && !sdlog::exists(name)) {
            strcpy(prevName, name);
            return;
        }
    }
    for (int tries = 0; tries < 100; tries++) {
        snprintf(name, sizeof name, "/WD-%04u.csv", (unsigned)(++counter % 10000));
        if (!sdlog::exists(name)) break;
    }
    strcpy(prevName, name);
}

void resetCursors() {
    for (int s = 0; s < nSrc; s++) applog::resetCursor(curs[s], *srcs[s]);
}

// A fresh file: nothing of it is on the card yet, every row is owed to it.
void newFile(uint32_t nowEpoch) {
    makeName(nowEpoch);
    resetCursors();
    fst = applog::FileState{};
    rows = skipped = 0;
}

// One capped batch. A failure with the card gone drops to NO_CARD; a failure
// with the card still there is retried at the next interval.
// `final` releases rows held for a better position (applog::holdVerdict):
// STOP, RESET and a due rollover pass it, so nothing held is cleared unwritten.
applog::Result flushOnce(uint32_t nowMs, uint32_t nowEpoch, bool final = false) {
    applog::Result r = applog::appendBatch(name, fst, sink, header, srcs, curs, nSrc,
                                           applog::BATCH_ROWS, pend, nowMs, final || rollWaiting);
    if (r.ok) {
        rows += r.rows;
        skipped = r.waiting;
        nextFlushMs = nowMs + (r.more ? CATCHUP_MS : FLUSH_MS);
        return r;
    }
    nextFlushMs = nowMs + FLUSH_MS;
    if (r.clash || r.shrunk) {
        // Someone else's file, or ours lost rows we had counted: a new file,
        // with the cursors reset so the whole table is logged into it.
        newFile(nowEpoch);
        nextFlushMs = nowMs + CATCHUP_MS;
        if (r.shrunk) fileRestart = true;
    }
    if (!sdlog::present()) st = NO_CARD;
    else if (!r.clash && !r.shrunk) writeErr = true;   // card there but the write failed
    return r;
}

// Up to FINAL_BATCHES, stopping early once drained or failing.
void drain(uint32_t nowMs, uint32_t nowEpoch) {
    for (int k = 0; k < FINAL_BATCHES && st == ACTIVE; k++) {
        const applog::Result r = flushOnce(nowMs, nowEpoch, true);
        if (!r.ok || !r.more) break;
    }
}

// Begin waiting for a roll (threshold or RESET), if not already.
void armRoll(uint32_t nowMs) {
    if (rollWaiting) return;
    rollWaiting = true;
    rollDueAt = nextRollTry = nowMs;
}

void doRoll(uint32_t nowMs, uint32_t nowEpoch, bool forced, uint32_t lost) {
    RollInfo info;
    info.wifiAps  = hk.wifiUniques ? hk.wifiUniques() : 0;
    info.bleAddrs = hk.bleUniques  ? hk.bleUniques()  : 0;
    info.forced = forced;
    info.lost = lost;
    info.reset = resetClear != nullptr;
    if (resetClear) resetClear();
    else if (hk.clearTables) hk.clearTables();
    resetClear = nullptr;
    nRolls++;
    rollWaiting = false;
    if (st == ACTIVE) {
        newFile(nowEpoch);
        info.newName = name;
        if (hk.onRollover) hk.onRollover(info);
        flushOnce(nowMs, nowEpoch);       // the new file, header first
    } else {
        resetCursors();                   // nothing logged; the table restarts
        if (hk.onRollover) hk.onRollover(info);
    }
}

// Forced: up to FINAL_BATCHES more (~800 rows, ~0.4 s), then count the rest.
void forceRoll(uint32_t nowMs, uint32_t nowEpoch) {
    drain(nowMs, nowEpoch);
    const uint32_t lost = applog::pendingRows(srcs, curs, nSrc, nowMs, true);
    doRoll(nowMs, nowEpoch, lost > 0, lost);
}

// What a flush this tick did, if any.
enum Flushed { NONE, FAILED, PARTIAL, DRAINED };
Flushed outcome(const applog::Result& r) {
    return !r.ok ? FAILED : r.more ? PARTIAL : DRAINED;
}

bool thresholdDue() {
    return applog::rolloverDue(hk.wifiUniques ? hk.wifiUniques() : 0,
                               hk.bleUniques ? hk.bleUniques() : 0);
}

// Runs every tick, so a due rollover is noticed within one loop, not one
// flush interval. It completes when a (final) flush drains everything, or is
// forced by the bounds in logging.h.
void checkRoll(uint32_t nowMs, uint32_t nowEpoch, Flushed f) {
    if (!thresholdDue() && !resetClear) { rollWaiting = false; return; }
    if (st != ACTIVE) { doRoll(nowMs, nowEpoch, false, 0); return; }
    armRoll(nowMs);
    if (f == NONE && (int32_t)(nowMs - nextRollTry) >= 0) {
        nextRollTry = nowMs + CATCHUP_MS;
        f = outcome(flushOnce(nowMs, nowEpoch));
    }
    if (f == DRAINED) { doRoll(nowMs, nowEpoch, false, 0); return; }
    const bool hard = applog::rolloverHard(hk.wifiUniques ? hk.wifiUniques() : 0,
                                           hk.bleUniques ? hk.bleUniques() : 0);
    if (hard || nowMs - rollDueAt >= ROLL_MAX_WAIT_MS)
        forceRoll(nowMs, nowEpoch);
}

} // namespace

void start(uint32_t nowMs, uint32_t nowEpoch) {
    if (st != IDLE) return;
    want = true;
    newFile(nowEpoch);
    startedMs = nowMs;
    nextFlushMs = nowMs + FLUSH_MS;
    st = ACTIVE;
    if (sdlog::present()) flushOnce(nowMs, nowEpoch);   // creates the file; may demote
    else st = NO_CARD;
}

void stop(uint32_t nowMs) {
    uint32_t lost = 0;
    const bool wasActive = st == ACTIVE;
    if (wasActive) {
        // Drain everything (final: held rows too), bounded by the wall clock
        // and stopping at the first failed batch; then count what is left.
        const uint32_t t0 = hk.clockMs ? hk.clockMs() : 0;
        for (int k = 0; st == ACTIVE; k++) {
            if (hk.clockMs ? hk.clockMs() - t0 >= STOP_DRAIN_MAX_MS : k >= FINAL_BATCHES) break;
            const applog::Result r = flushOnce(nowMs, 0, true);
            if (!r.ok || !r.more) break;
        }
        lost = applog::pendingRows(srcs, curs, nSrc, nowMs, true);
    }
    const bool hadReset = resetClear != nullptr;
    want = false;
    st = IDLE;
    name[0] = 0;
    rollWaiting = false;
    if (resetClear) {                     // a RESET was still draining: apply it
        resetClear();
        resetClear = nullptr;
        resetCursors();
    }
    if (wasActive && lost && hk.onStopLoss) hk.onStopLoss(lost, hadReset);
}

void toggle(uint32_t nowMs, uint32_t nowEpoch) {
    if (st == IDLE) start(nowMs, nowEpoch);
    else stop(nowMs);
}

void begin(bool wasActive, const Hooks& h, uint32_t nowMs, uint32_t nowEpoch) {
    hk = h;
    nSrc = 0;
    if (hk.wifi) srcs[nSrc++] = hk.wifi;
    if (hk.ble)  srcs[nSrc++] = hk.ble;
    header[0] = 0;
    HeaderSink hs;
    ui_wigle::header(hs);
    st = IDLE;
    want = false;
    name[0] = 0;
    prevName[0] = 0;
    rollWaiting = false;
    resetClear = nullptr;
    resetCursors();
    // Advance the dateless counter past any WD-NNNN.csv (or .CSV from older firmware) already on the card, so
    // a reboot before a GPS fix cannot overwrite an earlier dateless session.
    counter = sdlog::maxIndex("/WD-", ".csv");
    if (wasActive) start(nowMs, nowEpoch);   // a fresh file, never the old one
}

void tick(uint32_t nowMs, uint32_t nowEpoch) {
    // Notice a due roll before this tick's flush, so that flush is already
    // final (releases held rows) and can count as the drain.
    if (st == ACTIVE && thresholdDue()) armRoll(nowMs);
    Flushed f = NONE;
    if (st == NO_CARD) {
        if (sdlog::present()) {
            st = ACTIVE;
            f = outcome(flushOnce(nowMs, nowEpoch));   // may demote again
        }
    } else if (st == ACTIVE && (int32_t)(nowMs - nextFlushMs) >= 0) {
        f = outcome(flushOnce(nowMs, nowEpoch));
    }
    checkRoll(nowMs, nowEpoch, f);
}

void resetTables(void (*clear)(), uint32_t nowMs, uint32_t nowEpoch) {
    if (st != ACTIVE) {
        if (clear) clear();
        resetCursors();
        return;
    }
    resetClear = clear;
    armRoll(nowMs);
    drain(nowMs, nowEpoch);               // usually everything, in one batch
    if (st == ACTIVE && applog::pendingRows(srcs, curs, nSrc, nowMs, true) == 0)
        doRoll(nowMs, nowEpoch, false, 0);
    // else: checkRoll finishes it on the next ticks, within the roll bounds.
}

State       state()                 { return st; }
bool        wantsActive()           { return want; }
const char* fileName()              { return name; }
uint32_t    lastRows()              { return rows; }
uint32_t    lastSkipped()           { return skipped; }
uint32_t    rollovers()             { return nRolls; }
uint32_t    sessionSecs(uint32_t nowMs) {
    return st == IDLE ? 0 : (nowMs - startedMs) / 1000;
}
bool        takeWriteError()        { bool e = writeErr; writeErr = false; return e; }
bool        takeFileRestart()       { bool e = fileRestart; fileRestart = false; return e; }

} // namespace logging
