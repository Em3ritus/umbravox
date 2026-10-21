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
