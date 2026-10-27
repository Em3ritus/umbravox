// The enrol / un-enrol window, as a pure state machine: when the CYD transmits,
// and how long it stays on the air. Pure (no radio, no Arduino) so it tests on
// the host; main.cpp owns the radio and calls tick() every loop.
//
// Two lessons are baked in here. Boards only listen for tens of milliseconds
// per cycle, so the CYD must transmit far faster than once a second. And a
// RESET must never abandon a net while boards are still answering on it: the
// CYD drops to open only after the fleet has gone quiet, because a board that
// missed the command (one behind a coordinator, say) falls back to the CYD
// within ~16 s and needs to hear it then -- and once the CYD has left the net
// that board is stranded, since an invite only moves OPEN boards.
#pragma once
#include <stdint.h>

namespace enrollwin {

enum Mode : uint8_t { IDLE, ADOPT, UNENROL };

constexpr uint32_t ADOPT_MS        = 60000;   // an invite stays open this long
constexpr uint32_t TX_MS           = 25;      // boards listen ~30-60 ms per cycle
constexpr uint32_t UNENROL_MIN_MS  = 15000;   // at least this long on air
// Silence needed after the last board answered. Short because boardsHeard
// already counts only boards heard in the last 20 s (meshlive::BOARD_QUIET_MS;
// the coordinator table times out at 20 s too).
constexpr uint32_t UNENROL_QUIET_MS = 8000;
constexpr uint32_t UNENROL_MAX_MS  = 120000;  // hard cap: give up, and say so

struct State {
    Mode     mode      = IDLE;
    uint32_t startedAt = 0;
    uint32_t lastHeard = 0;   // last time a board of the old net answered
    uint32_t until     = 0;   // the window's current end
    uint32_t lastTx    = 0;
    bool     txd       = false;
};

struct Tick {
    bool send    = false;     // transmit one frame now
    bool done    = false;     // the window has ended
    bool gaveUp  = false;     // ...at the cap, with boards still answering
};

// Open an invite window. Refused during a reset: a stray ENROLL tap must not
// abandon an un-enrol half way and leave boards on the old net.
inline bool adopt(State& s, uint32_t now) {
    if (s.mode == UNENROL) return false;
    s.mode = ADOPT; s.startedAt = now; s.until = now + ADOPT_MS; s.txd = false;
    return true;
}

// Begin an un-enrol. Refused if one is already running (a double tap), and may
// interrupt an invite window.
inline bool unenrol(State& s, uint32_t now) {
    if (s.mode == UNENROL) return false;
    s.mode = UNENROL; s.startedAt = now; s.lastHeard = now;
    s.until = now + UNENROL_MIN_MS; s.txd = false;
    return true;
}

// Advance the window. `boardsHeard` is true while any board of the net is
// still answering (nodes in the Core's table, or live coordinators).
inline Tick tick(State& s, uint32_t now, bool boardsHeard) {
    Tick k;
    if (s.mode == IDLE) return k;

    if (s.mode == UNENROL) {
        if (boardsHeard) s.lastHeard = now;
        uint32_t until = s.lastHeard + UNENROL_QUIET_MS;
        const uint32_t floorEnd = s.startedAt + UNENROL_MIN_MS;
        if (until < floorEnd) until = floorEnd;
        const uint32_t cap = s.startedAt + UNENROL_MAX_MS;
        if (until > cap) until = cap;
        s.until = until;
    }

    if ((int32_t)(s.until - now) <= 0) {
        k.done   = true;
        k.gaveUp = s.mode == UNENROL && boardsHeard;
        s.mode   = IDLE;
        return k;
    }
    if (!s.txd || now - s.lastTx >= TX_MS) { s.txd = true; s.lastTx = now; k.send = true; }
    return k;
}

inline int secsLeft(const State& s, uint32_t now) {
    if (s.mode == IDLE) return 0;
    return (int32_t)(s.until - now) > 0 ? (int)((s.until - now + 999) / 1000) : 0;
}

} // namespace enrollwin
