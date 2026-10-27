// elog -- a small diagnostic log written to the SD card, so a user who hits a
// problem can pull the card (or run DIAG on the console) and send back a file
// that says what happened. Three levels, a boot banner that records the reset
// reason (the crash fingerprint across reboots), and a hard size cap.
//
// The line formatter and the rotation decision are pure and live here so they
// can be host-tested (sim/test_elog.cpp); the SD-touching begin()/event()/dump()
// are defined in elog.cpp and only built for the device.
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>

class Print;

namespace elog {

enum Level : uint8_t { INFO = 0, WARN = 1, ERR = 2 };

// Fixed width (4 chars) so the columns line up in the file.
inline const char* levelName(Level l) {
    switch (l) {
        case ERR:  return "ERR ";
        case WARN: return "WARN";
        default:   return "INFO";
    }
}

// Format one line into `out`: "UUUUUUUUUU LVL tag: msg\n" (uptime ms, zero-
// padded to 10 digits). Always NUL-terminates and never writes past `n`.
// Returns the number of bytes written (excluding the NUL). Pure.
inline int format(char* out, size_t n, uint32_t upMs, Level lvl,
                  const char* tag, const char* msg) {
    if (!out || n == 0) return 0;
    const int w = snprintf(out, n, "%010lu %s %s: %s\n",
                           (unsigned long)upMs, levelName(lvl),
                           tag && tag[0] ? tag : "?", msg ? msg : "");
    if (w < 0) { out[0] = '\0'; return 0; }
    return (size_t)w >= n ? (int)(n - 1) : w;   // snprintf clamps; so do we
}

// Rotate the live log before appending `addLen` bytes would carry it past the
// cap. Pure. 64-bit math so a near-full counter cannot wrap.
inline bool shouldRotate(uint32_t curSize, uint32_t addLen, uint32_t cap) {
    return (uint64_t)curSize + addLen > cap;
}

// A rate limit for repeating diagnostics (table full, scan restarted, loop
// stall, node expired): the first event passes, then at most one per `gapMs`.
// Elapsed time is unsigned, so it holds across the millis() wrap. Pure.
struct Gate {
    bool     fired  = false;
    uint32_t lastMs = 0;
    bool allow(uint32_t nowMs, uint32_t gapMs) {
        if (fired && nowMs - lastMs < gapMs) return false;
        fired = true; lastMs = nowMs;
        return true;
    }
};

// Low-heap warnings with hysteresis. The old rule warned once below 40 KB and
// re-armed only above 60 KB, which never happens with BLE scanning, so each
// boot logged one line and then nothing, even at 16 KB. Now each level (40,
// 24, 16 KB) warns once, and re-arms only after a SUSTAINED recovery: every
// sample REARM_K above it for SUSTAIN_MS. A single high sample is not enough
// -- the BLE scan recycle swings free heap ~31 <-> 63 KB every few seconds,
// and a one-sample re-arm turned that into a line every 10 s, pushing the
// boot banners and the other diagnostics out of the 128 KB log. On top of
// that each level speaks at most once per GAP_MS (a warning that comes due
// inside the gap is written when it opens, if the heap is still low).
// step() takes free heap in KB and the time; it returns the level to warn
// about (the lowest newly crossed, so a sudden drop is one line), or 0. Pure.
struct HeapWarn {
    static constexpr int      N = 3;
    static constexpr uint32_t LEVEL_K[N] = {40, 24, 16};
    static constexpr uint32_t REARM_K    = 8;
    static constexpr uint32_t SUSTAIN_MS = 60000;
    static constexpr uint32_t GAP_MS     = 600000;    // 10 min per level
    bool     warned[N]   = {false, false, false};
    bool     rising[N]   = {false, false, false};     // a run above re-arm is under way
    uint32_t riseAt[N]   = {0, 0, 0};                 // ...since this sample
    Gate     gate[N];
    uint32_t step(uint32_t freeK, uint32_t nowMs) {
        uint32_t hit = 0;
        int hitI = -1;
        for (int i = 0; i < N; i++) {
            if (freeK >= LEVEL_K[i] + REARM_K) {
                if (!rising[i]) { rising[i] = true; riseAt[i] = nowMs; }
                if (warned[i] && nowMs - riseAt[i] >= SUSTAIN_MS) warned[i] = false;
            } else {
                rising[i] = false;
            }
            if (!warned[i] && freeK < LEVEL_K[i]) { hit = LEVEL_K[i]; hitI = i; }
        }
        if (hitI < 0) return 0;
        // The line for the lowest level crossed; the gate may hold it back.
        if (gate[hitI].fired && nowMs - gate[hitI].lastMs < GAP_MS) return 0;
        gate[hitI].allow(nowMs, GAP_MS);
        for (int i = 0; i <= hitI; i++) if (freeK < LEVEL_K[i]) warned[i] = true;
        return hit;
    }
};

// Rotate once at 128 KB (-> at most ~256 KB on the card across both files).
constexpr uint32_t CAP  = 128u * 1024u;
constexpr const char* PATH = "/diag.log";
constexpr const char* OLD  = "/diag.0.log";

// --- device side (elog.cpp) --------------------------------------------
// begin() writes the boot banner. event() formats + appends (and mirrors to
// Serial); it is printf-checked. dump() streams the log to a console.
void begin();
void event(Level lvl, const char* tag, const char* fmt, ...)
    __attribute__((format(printf, 3, 4)));
void dump(Print& out);

} // namespace elog
