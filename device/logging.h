// The wardrive logging session: start/stop/toggle, the flush timer, auto-
// resume after a reboot, and the rollover to a new file when the capture
// tables fill. Pure -- no Arduino, no SD -- so it tests on the host against a
// fake sdlog and fake row sources. It reaches the card only through sdlog.h.
//
// Each flush APPENDS only rows not yet written (applog.h), at most
// applog::BATCH_ROWS of them, so the stall no longer grows with the file.
#pragma once
#include <stdint.h>
#include "applog.h"

namespace logging {

enum State : uint8_t { IDLE, ACTIVE, NO_CARD };

// What main wires in. Kept as pointers so logging carries no dependency on
// the capture modules.
struct Hooks {
    applog::Source* wifi = nullptr;            // rows, first-seen order
    applog::Source* ble  = nullptr;
    uint32_t (*wifiUniques)() = nullptr;       // drives the rollover rule
    uint32_t (*bleUniques)()  = nullptr;
    void (*clearTables)() = nullptr;           // rollover: clear both dedup tables
    // After a rollover. `newName` is "" when not logging; `forced` when rows
    // still pending had to be given up (the card kept failing).
    void (*onRollover)(uint32_t wifiAps, uint32_t bleAddrs,
                       const char* newName, bool forced) = nullptr;
};

// The routine flush interval. Appends are cheap, so this is shorter than the
// old 30 s rewrite: a pulled power cable loses at most this much.
constexpr uint32_t FLUSH_MS = 10000;
// When a flush stopped at the row cap, the next one comes this soon.
constexpr uint32_t CATCHUP_MS = 1000;
// A due rollover waits for pending rows to reach the card; if the card makes
// no progress for this long it rolls anyway (the unwritten rows are given up),
// so a failing card cannot let the table fill.
constexpr uint32_t ROLL_GRACE_MS = 60000;
// Batches written at STOP / RESET before the file is closed (bounds the stall
// to a few hundred ms even with a backlog).
constexpr int FINAL_BATCHES = 4;

// At boot: resume if we died logging. `nowEpoch` seeds the session filename
// when a date is already known.
void begin(bool wasActive, const Hooks& hooks, uint32_t nowMs, uint32_t nowEpoch);

void toggle(uint32_t nowMs, uint32_t nowEpoch);
void start(uint32_t nowMs, uint32_t nowEpoch);
void stop(uint32_t nowMs);

// Every loop. Flushes while ACTIVE; promotes NO_CARD to ACTIVE when a card
// appears; checks the rollover rule.
void tick(uint32_t nowMs, uint32_t nowEpoch);

// RESET COUNTERS. While logging this is a rollover: pending rows go to the
// current file, `clear` runs, and the session continues in a NEW file from
// position 0 -- the old file is never rewritten. Idle: just `clear`.
void resetTables(void (*clear)(), uint32_t nowMs, uint32_t nowEpoch);

State       state();
bool        wantsActive();   // the flag to persist: true from start to stop
uint32_t    sessionSecs(uint32_t nowMs);
const char* fileName();      // current session file, "" when idle
uint32_t    lastRows();      // rows in the current file
uint32_t    lastSkipped();   // rows waiting for a fix at the last flush
uint32_t    rollovers();     // since boot
// Latched true when a flush failed with the card still present (a card-full or
// I/O error, not a missing card). main reads+clears it to log one diagnostic.
bool        takeWriteError();

} // namespace logging
