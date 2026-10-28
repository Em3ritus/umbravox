// filescan -- which files the FILES screen lists, in what order, and which of
// them it may delete. Header-only and pure (no Arduino, no SD), so the host
// test (sim/test_files.cpp) runs the same rules the board runs.
//
// The rules are not written here. "Is this a session log" is
// sdcmd::sessionFile and "may it be deleted" is sdcmd::delPolicy, the serial
// SDDEL's own policy (device/sdcmd.h), so the screen and the console can
// never disagree about a file. delPolicy also allows theme files; this screen
// lists logs only, so every verdict here is sessionFile first.
//
// The walk: main.cpp feeds visit() one root directory entry at a time from
// sdlog::listDirSized(). It stops at MAX_ENTRIES entries or BUDGET_MS of wall
// clock, whichever comes first, and marks the list partial. Logs are kept
// newest first; past MAX_FILE_ROWS the oldest drop off.
#pragma once
#include <stdint.h>
#include <string.h>
#include "sdcmd.h"
#include "sdlog.h"
#include "model.h"

namespace filescan {

constexpr uint32_t MAX_ENTRIES = 512;   // root entries looked at, logs or not
constexpr uint32_t BUDGET_MS   = 1000;  // under the 2 s NMEA buffer and stall warning

// "/" + name into out. False if it does not fit or name is empty.
inline bool pathOf(const char* name, char* out, size_t cap) {
    if (!name || !name[0] || cap < 2) return false;
    const size_t n = strlen(name);
    if (n + 2 > cap) return false;
    out[0] = '/';
    memcpy(out + 1, name, n + 1);
    return true;
}

// A session log, by the console's policy: /WD-<digits and '-'>.csv, any case.
inline bool isLog(const char* path) {
    sdcmd::Path p;
    return sdcmd::sessionFile(path, p);
}

// The file the logging session is writing (`active` is logging::fileName()).
inline bool isActive(const char* path, const char* active) {
    sdcmd::Path p;
    return isLog(path) && sdcmd::delPolicy(path, active, p) != nullptr;
}

// nullptr when `path` may be deleted from the screen; otherwise the reason,
// in SDDEL's words. The device calls this on the name, never on an index.
inline const char* deleteCheck(const char* path, const char* active) {
    if (!isLog(path)) return sdcmd::NOT_ALLOWED;
    sdcmd::Path p;
    return sdcmd::delPolicy(path, active, p);
}

inline bool deletable(const char* path, const char* active) {
    return deleteCheck(path, active) == nullptr;
}

// WD-YYYYMMDD-HHMMSS.csv (a session started with the clock known).
inline bool dated(const char* name) {
    if (name[0] == '/') name++;
    if (strlen(name) != 22 || sdcmd::lc(name[0]) != 'w' || sdcmd::lc(name[1]) != 'd' || name[2] != '-')
        return false;
    for (int i = 3; i < 18; i++) {
        if (i == 11) { if (name[i] != '-') return false; continue; }
        if (name[i] < '0' || name[i] > '9') return false;
    }
    return sdcmd::ieq(name + 18, 4, ".csv");
}

inline int icmp(const char* a, const char* b) {
    for (;; a++, b++) {
        const char x = sdcmd::lc(*a), y = sdcmd::lc(*b);
        if (x != y) return x < y ? -1 : 1;
        if (!x) return 0;
    }
}

// Is `a` newer than `b`? Dated names first, newest first (the stamp sorts
// as text). Dateless names have no clock: they follow, highest number first
// (logging numbers them upward). Anything else policy allows (other runs of
// digits and '-') comes by name, descending.
inline bool newer(const char* a, const char* b) {
    const bool da = dated(a), db = dated(b);
    if (da != db) return da;
    if (!da) {
        uint32_t va = 0, vb = 0;
        const bool na = sdlog::matchIndex(a, "WD-", ".csv", va);
        const bool nb = sdlog::matchIndex(b, "WD-", ".csv", vb);
        if (na != nb) return na;
        if (na && va != vb) return va > vb;
    }
    return icmp(a, b) > 0;
}

// Insert one log, keeping the list newest first and at most MAX_FILE_ROWS.
inline void insert(ui::FileList& L, const char* name, uint32_t size) {
    int i = 0;
    while (i < L.n && !newer(name, L.rows[i].name)) i++;
    if (i >= ui::MAX_FILE_ROWS) return;                 // older than all 64 kept
    const int last = L.n < ui::MAX_FILE_ROWS ? L.n : ui::MAX_FILE_ROWS - 1;
    for (int k = last; k > i; k--) L.rows[k] = L.rows[k - 1];
    memset(L.rows[i].name, 0, sizeof L.rows[i].name);
    strncpy(L.rows[i].name, name, sizeof L.rows[i].name - 1);
    L.rows[i].size = size;
    if (L.n < ui::MAX_FILE_ROWS) L.n++;
}

// Empty the list in place (no FileList temporary: it is 1.8 KB and the loop
// task's stack is 8 KB). All-zero is exactly the default-constructed list.
static_assert(__is_trivially_copyable(ui::FileList), "reset() zeroes FileList bytewise");
inline void reset(ui::FileList& L) { memset(static_cast<void*>(&L), 0, sizeof L); }

struct Walk {
    ui::FileList* L = nullptr;
    uint32_t t0 = 0;
    uint32_t seen = 0;
};

// Start a walk: the list is emptied and marked as reached.
inline void begin(Walk& w, ui::FileList& L, uint32_t nowMs) {
    reset(L);
    L.card = true;
    w.L = &L;
    w.t0 = nowMs;
    w.seen = 0;
}

// One directory entry. False = stop the walk (a bound was reached; the list
// is marked partial and this entry is not counted).
inline bool visit(Walk& w, const char* name, bool isDir, uint32_t size, uint32_t nowMs) {
    if (w.seen >= MAX_ENTRIES || nowMs - w.t0 >= BUDGET_MS) {
        w.L->partial = true;
        return false;
    }
    w.seen++;
    if (isDir || !name) return true;
    char path[ui::FILE_NAME_MAX + 1];
    // A log longer than a row holds (policy allows 31 characters; the
    // firmware writes at most 22) is left to the serial console.
    if (strlen(name) >= (size_t)ui::FILE_NAME_MAX || !pathOf(name, path, sizeof path)) return true;
    if (!isLog(path)) return true;
    if (w.L->found < 0xFFFF) w.L->found++;
    const uint32_t kb = size / 1024 + (size % 1024 ? 1 : 0);
    w.L->logKB = w.L->logKB > 0xFFFFFFFFu - kb ? 0xFFFFFFFFu : w.L->logKB + kb;
    insert(*w.L, name, size);
    return true;
}

// Deleting: SDDEL's sequence (sdcmd Console::del) on the card's primitives.
struct RemoveIo {
    bool (*exists)(const char* path);
    bool (*isDir)(const char* path);
    bool (*remove)(const char* path);
};
enum RemoveResult : uint8_t { RM_OK, RM_REFUSED, RM_MISSING, RM_DIR, RM_FAILED };
inline RemoveResult removeLog(const char* path, const char* active, const RemoveIo& io) {
    if (deleteCheck(path, active)) return RM_REFUSED;
    if (!io.exists(path)) return RM_MISSING;
    if (io.isDir(path)) return RM_DIR;          // never remove a directory
    return io.remove(path) ? RM_OK : RM_FAILED;
}

// The list index of the logging session's file, -1 if it is not listed.
inline int activeIndex(const ui::FileList& L, const char* active) {
    if (!active || !active[0]) return -1;
    char path[ui::FILE_NAME_MAX + 1];
    for (int i = 0; i < L.n; i++)
        if (pathOf(L.rows[i].name, path, sizeof path) && isActive(path, active)) return i;
    return -1;
}

} // namespace filescan
