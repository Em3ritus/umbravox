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
    // After a rollover (or a RESET while logging).
    void (*onRollover)(const struct RollInfo& r) = nullptr;
    // A wall clock (millis) for STOP's drain bound. Without one, STOP writes
    // FINAL_BATCHES at most.
    uint32_t (*clockMs)() = nullptr;
    // STOP left `lost` exportable rows unwritten (`reset`: a RESET was pending
    // and its clear has now been applied).
    void (*onStopLoss)(uint32_t lost, bool reset) = nullptr;
};

struct RollInfo {
    uint32_t    wifiAps = 0, bleAddrs = 0;  // table sizes when it rolled
    const char* newName = "";               // "" when not logging
    bool        forced = false;             // rows were given up (see `lost`)
    bool        reset = false;              // RESET COUNTERS, not the threshold
    uint32_t    lost = 0;                   // exportable rows not on the card
};

// The routine flush interval. Appends are cheap, so this is shorter than the
// old 30 s rewrite. A pulled power cable loses the rows not yet flushed plus
// those still held for a better position (applog HOLD_*: up to 120 s), so up
// to ~130 s of the newest rows.
constexpr uint32_t FLUSH_MS = 10000;
// When a flush stopped at the row cap, the next one comes this soon.
constexpr uint32_t CATCHUP_MS = 1000;
// A due rollover (or a RESET) waits for pending rows to reach the card, but is
// forced -- after at most FINAL_BATCHES more, with the rest counted as lost --
// when ROLL_MAX_WAIT_MS has passed since it first became due, or when the
// table reaches applog::ROLL_HARD_APS / ROLL_HARD_BLE. Arrivals faster than
// the drain cannot defer it.
constexpr uint32_t ROLL_MAX_WAIT_MS = 30000;
// Batches written by a forced roll / RESET before the clear (bounds that stall
// to a few hundred ms even with a backlog).
constexpr int FINAL_BATCHES = 4;
// STOP is the user's: it drains the whole backlog (with the hold, every AP
// heard in the last 20 s, parked ones up to 120 s -- easily over 800 rows in
// a dense area), stopping only after this much wall-clock time or a failed
// batch. What is left is counted and reported (Hooks::onStopLoss).
constexpr uint32_t STOP_DRAIN_MAX_MS = 3000;

// At boot: resume if we died logging. `nowEpoch` seeds the session filename
// when a date is already known.
void begin(bool wasActive, const Hooks& hooks, uint32_t nowMs, uint32_t nowEpoch);

void toggle(uint32_t nowMs, uint32_t nowEpoch);
void start(uint32_t nowMs, uint32_t nowEpoch);
void stop(uint32_t nowMs);

// Every loop. Flushes while ACTIVE; promotes NO_CARD to ACTIVE when a card
// appears; checks the rollover rule.
void tick(uint32_t nowMs, uint32_t nowEpoch);

// RESET COUNTERS. While logging this is a rollover through the same path:
// pending rows go to the current file (now, or over the next ticks within the
// roll bounds above, any rest counted as lost), then `clear` runs and the
// session continues in a NEW file from position 0 -- the old file is never
// rewritten. Idle: `clear` runs at once.
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
// Latched when the session file was found shorter than what was committed
// (rows counted as written never landed) and logging moved to a new file.
bool        takeFileRestart();

} // namespace logging
