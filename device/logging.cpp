// See logging.h. Pure: the card is reached only through sdlog.h, so this
// compiles and tests on the host with no Arduino.
#include "logging.h"
#include "sdlog.h"
#include "csv.h"
#include <stdio.h>
#include <string.h>

namespace logging {
namespace {

State    st = IDLE;
Body     body = nullptr;
bool     want = false;
uint32_t startedMs = 0;
uint32_t nextFlushMs = 0;
uint32_t rows = 0, skipped = 0;
// Names sessions when no date is known yet. NOT persisted: after a dateless
// reboot it restarts at 1, so a new dateless session reuses WD-0001.CSV and
// OVERWRITES (destroys) the earlier dateless session's file. Spec s7 wants the
// counter advanced PAST any WD-*.CSV already on the card; that card scan is
// deferred to hardware (plan, Hardware Acceptance).
uint32_t counter = 0;
char     name[32] = "";
char     prevName[32] = "";  // the last session's file, never reused

// Anything earlier than this is the RTC's power-on default, not a real date.
constexpr uint32_t EPOCH_KNOWN = 1577836800u;   // 2020-01-01 00:00:00 UTC

// Absolute path -- SD.open requires the leading '/'.
void makeName(uint32_t nowEpoch) {
    if (nowEpoch >= EPOCH_KNOWN) {
        char ts[24];
        csv::stampUtc(ts, nowEpoch);              // "YYYY-MM-DD HH:MM:SS"
        snprintf(name, sizeof name, "/WD-%.4s%.2s%.2s-%.2s%.2s%.2s.CSV",
                 ts, ts + 5, ts + 8, ts + 11, ts + 14, ts + 17);
        // Two sessions in the same second would overwrite each other.
        if (strcmp(name, prevName) != 0) { strcpy(prevName, name); return; }
    }
    snprintf(name, sizeof name, "/WD-%04u.CSV", (unsigned)(++counter % 10000));
    strcpy(prevName, name);
}

// Write the whole document. A failure with the card gone drops to NO_CARD;
// a failure with the card still there is retried at the next interval.
bool flush() {
    if (!body) return false;
    uint32_t r = 0, s = 0;
    if (sdlog::writeAtomic(name, body, r, s)) { rows = r; skipped = s; return true; }
    if (!sdlog::present()) st = NO_CARD;
    return false;
}

} // namespace

void start(uint32_t nowMs, uint32_t nowEpoch) {
    if (st != IDLE) return;
    want = true;
    makeName(nowEpoch);
    startedMs = nowMs;
    nextFlushMs = nowMs + FLUSH_MS;
    rows = skipped = 0;
    st = ACTIVE;
    if (sdlog::present()) flush();    // opens the file; may demote to NO_CARD
    else st = NO_CARD;
}

void stop(uint32_t nowMs) {
    (void)nowMs;
    if (st == ACTIVE) flush();
    want = false;
    st = IDLE;
    name[0] = 0;
}

void toggle(uint32_t nowMs, uint32_t nowEpoch) {
    if (st == IDLE) start(nowMs, nowEpoch);
    else stop(nowMs);
}

void begin(bool wasActive, Body b, uint32_t nowMs, uint32_t nowEpoch) {
    body = b;
    st = IDLE;
    want = false;
    name[0] = 0;
    if (wasActive) start(nowMs, nowEpoch);   // a fresh file, never the old one
}

void tick(uint32_t nowMs, uint32_t nowEpoch) {
    (void)nowEpoch;
    if (st == NO_CARD) {
        if (sdlog::present()) {
            st = ACTIVE;
            nextFlushMs = nowMs + FLUSH_MS;
            flush();                  // opens the file; may demote again
        }
    } else if (st == ACTIVE && (int32_t)(nowMs - nextFlushMs) >= 0) {
        nextFlushMs += FLUSH_MS;
        if ((int32_t)(nowMs - nextFlushMs) >= 0) nextFlushMs = nowMs + FLUSH_MS;
        flush();
    }
}

State       state()                 { return st; }
bool        wantsActive()           { return want; }
const char* fileName()              { return name; }
uint32_t    lastRows()              { return rows; }
uint32_t    lastSkipped()           { return skipped; }
uint32_t    sessionSecs(uint32_t nowMs) {
    return st == IDLE ? 0 : (nowMs - startedMs) / 1000;
}

} // namespace logging
