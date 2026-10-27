// sdcmd -- install theme packs and delete files on the SD card over the wired
// USB console, without pulling the card. Header-only and pure: no Arduino, no
// SD, no heap. The card is reached through the Io interface (device/main.cpp
// backs it with sdlog; sim/test_sdcmd.cpp with an in-memory fake), so the
// host test runs the very policy and state machine the board runs.
//
// This is a WIRED console feature only. Nothing here touches a radio or a
// network; it is reached only from pollConsole() on the USB UART.
//
// Protocol (one command per line, ASCII, LF or CRLF; every reply one line):
//   SDPUT <path> <size> <crc32>   -> OK put <canon> <size>  | ERR <reason>
//   D <offset> <hex>              -> OK <offset>            | ERR <reason>
//   END                           -> DONE <size> <crc32>    | ERR <reason>
//   ABORT                         -> OK aborted             | ERR no upload
//   SDDEL <path>                  -> OK                     | ERR <reason>
//   SDRMDIR <folder>              -> OK                     | ERR <reason>
//   REBOOT                        -> OK rebooting           (then the board restarts)
// <size> is decimal, <crc32> exactly 8 hex digits (zlib / IEEE 802.3 CRC-32,
// replies in lower case), <hex> 2..192 hex digits (1..96 bytes). Commands
// match exactly: one space between tokens, no leading or trailing space, and
// the exact upper-case word. A timed-out upload prints one unsolicited
// "ERR upload timed out" line. Other console output (log lines) may appear
// between replies; a host must ignore lines that are not replies.
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include "themepack.h"

namespace sdcmd {

// The longest console line. The longest D line is "D 9727 " + 192 hex = 199.
constexpr size_t   LINE_CAP  = 200;
// Raw bytes per D line. Hex keeps a 96-byte chunk under LINE_CAP.
constexpr size_t   CHUNK_MAX = 96;
// An upload with no D/END line for this long is abandoned and its temp removed.
constexpr uint32_t IDLE_MS   = 10000;
// "/themes/" + 31 + "/" + "theme.json" + ".tmp" + NUL = 55.
constexpr size_t   PATH_CAP  = 64;
constexpr size_t   REPLY_CAP = 112;

// --- CRC-32 (IEEE 802.3, reflected 0xEDB88320), as zlib.crc32 -------------
// Incremental: crc32(crc32(0, a), b) == crc32(0, a + b). A nibble table:
// 64 bytes of flash, two lookups a byte.
inline uint32_t crc32(uint32_t crc, const uint8_t* p, size_t n) {
    static const uint32_t T[16] = {
        0x00000000u, 0x1DB71064u, 0x3B6E20C8u, 0x26D930ACu, 0x76DC4190u, 0x6B6B51F4u,
        0x4DB26158u, 0x5005713Cu, 0xEDB88320u, 0xF00F9344u, 0xD6D6A3E8u, 0xCB61B38Cu,
        0x9B64C2B0u, 0x86D3D2D4u, 0xA00AE278u, 0xBDBDF21Cu};
    crc = ~crc;
    for (size_t i = 0; i < n; i++) {
        crc ^= p[i];
        crc = (crc >> 4) ^ T[crc & 15];
        crc = (crc >> 4) ^ T[crc & 15];
    }
    return ~crc;
}

// --- strict field decoders --------------------------------------------------
inline int hexVal(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}
// 2..2*cap hex digits, even, nothing else -> the byte count; -1 on any fault.
inline long hexDecode(const char* s, size_t n, uint8_t* out, size_t cap) {
    if (!s || !out || n == 0 || (n & 1) || n / 2 > cap) return -1;
    for (size_t i = 0; i < n; i += 2) {
        const int hi = hexVal(s[i]), lo = hexVal(s[i + 1]);
        if (hi < 0 || lo < 0) return -1;
        out[i / 2] = (uint8_t)(hi << 4 | lo);
    }
    return (long)(n / 2);
}
// Decimal, 1..10 digits, no sign, no leading zero (except "0"), fits uint32.
inline bool parseDec(const char* s, size_t n, uint32_t& v) {
    if (!s || n == 0 || n > 10 || (n > 1 && s[0] == '0')) return false;
    uint64_t x = 0;
    for (size_t i = 0; i < n; i++) {
        if (s[i] < '0' || s[i] > '9') return false;
        x = x * 10 + (uint64_t)(s[i] - '0');
    }
    if (x > 0xFFFFFFFFull) return false;
    v = (uint32_t)x;
    return true;
}
// Exactly 8 hex digits, either case.
inline bool parseCrc(const char* s, size_t n, uint32_t& v) {
    if (!s || n != 8) return false;
    uint32_t x = 0;
    for (size_t i = 0; i < 8; i++) {
        const int h = hexVal(s[i]);
        if (h < 0) return false;
        x = x << 4 | (uint32_t)h;
    }
    v = x;
    return true;
}

// Split on single spaces. count is -1 when the line is malformed: leading or
// trailing space, two spaces in a row, or more than MAX tokens.
struct Tokens {
    static constexpr int MAX = 4;
    const char* p[MAX];
    size_t      n[MAX];
    int         count = 0;
    bool is(int i, const char* w) const {
        return i < count && strlen(w) == n[i] && !memcmp(p[i], w, n[i]);
    }
};
inline Tokens split(const char* line) {
    Tokens t;
    if (!line || !*line) return t;
    const char* s = line;
    for (;;) {
        const char* e = s;
        while (*e && *e != ' ') e++;
        if (e == s || t.count == Tokens::MAX) { t.count = -1; return t; }
        t.p[t.count] = s; t.n[t.count] = (size_t)(e - s); t.count++;
        if (!*e) return t;
        s = e + 1;
    }
}

// --- line framing -------------------------------------------------------------
// Bytes in, whole lines out. A line is LINE_CAP printable ASCII bytes
// (0x20..0x7E) at most; a longer one is discarded whole (OVERLONG), never
// truncated and run, and a line holding any other byte (line noise, a NUL)
// is discarded whole (BADBYTE). Blank lines are skipped. Fixed buffer.
struct LineBuf {
    enum Kind { NONE, LINE, OVERLONG, BADBYTE };
    char   buf[LINE_CAP + 1] = "";
    size_t n = 0;
    bool   over = false, bad = false;
    Kind feed(char c) {
        if (c == '\n' || c == '\r') {
            const Kind k = bad ? BADBYTE : over ? OVERLONG : n ? LINE : NONE;
            if (k == NONE) return NONE;              // a blank line, or the LF of a CRLF
            buf[n] = 0;
            n = 0; over = bad = false;
            if (k != LINE) buf[0] = 0;
            return k;
        }
        const unsigned char u = (unsigned char)c;
        if (u < 0x20 || u > 0x7E) bad = true;
        else if (n < LINE_CAP) buf[n++] = c;
        else over = true;
        return NONE;
    }
};

// --- path policy ----------------------------------------------------------------
// The only files these commands may touch:
//   SDPUT  /themes/<folder>/theme.json   (<= themepack::MAX_JSON bytes)
//          /themes/<folder>/emblem.bin   (<= themepack::MAX_EMBLEM bytes)
//   SDDEL  the two above, and their .tmp/.old leftovers (an interrupted
//          upload's), and root session logs /WD-<digits and '-'>.csv (any
//          case of the extension: older firmware wrote .CSV) except the one
//          logging is writing now.
//   SDRMDIR /themes/<folder>, only if empty (FAT refuses a non-empty one).
// <folder> passes themepack::folderNameOk (1-31 of A-Z a-z 0-9 _ -, so no
// '.', '/' or '\\', and thus no "..") and is not a DOS device name (CON, NUL,
// COM1...), which a Windows host could not open. The "themes" component and
// the file name compare case-insensitively, as FAT does, and the canonical
// path uses lower case for both; the folder keeps the case it was given (FAT
// finds an existing folder in any case). Nothing else: no diag.log, perf.csv,
// prefs, or any file in the root but a session log.
enum Kind : uint8_t { K_NONE, K_THEME_JSON, K_EMBLEM, K_SESSION };
struct Path {
    Kind kind = K_NONE;
    bool temp = false;                 // a .tmp / .old leftover (delete only)
    char folder[32] = "";
    char canon[PATH_CAP] = "";
};

inline char lc(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c; }
inline bool ieq(const char* a, size_t an, const char* b) {
    const size_t bn = strlen(b);
    if (an != bn) return false;
    for (size_t i = 0; i < an; i++) if (lc(a[i]) != lc(b[i])) return false;
    return true;
}

inline bool dosReserved(const char* f) {
    const size_t n = strlen(f);
    if (ieq(f, n, "con") || ieq(f, n, "prn") || ieq(f, n, "aux") || ieq(f, n, "nul")) return true;
    if (n == 4 && (ieq(f, 3, "com") || ieq(f, 3, "lpt")) && f[3] >= '0' && f[3] <= '9') return true;
    return false;
}
inline bool themeFolderOk(const char* f) { return themepack::folderNameOk(f) && !dosReserved(f); }

// Upper bound on any path this module reads. Longer is refused before parsing.
constexpr size_t PATH_IN_MAX = 120;

inline uint32_t capOf(Kind k) {
    return k == K_THEME_JSON ? (uint32_t)themepack::MAX_JSON
         : k == K_EMBLEM     ? (uint32_t)themepack::MAX_EMBLEM : 0;
}

// "/themes/<folder>/<file>" where <file> is theme.json or emblem.bin, plus
// ".tmp"/".old" when allowTemp.
inline bool themeFile(const char* path, Path& out, bool allowTemp) {
    out = Path();
    if (!path || path[0] != '/') return false;
    const size_t len = strnlen(path, PATH_IN_MAX + 1);
    if (len > PATH_IN_MAX) return false;
    if (len < 8 || !ieq(path, 8, "/themes/")) return false;
    const char* f = path + 8;
    const char* slash = strchr(f, '/');
    if (!slash) return false;
    const size_t fn = (size_t)(slash - f);
    if (fn == 0 || fn > 31) return false;
    memcpy(out.folder, f, fn);
    out.folder[fn] = 0;
    if (!themeFolderOk(out.folder)) return false;
    const char* file = slash + 1;
    const size_t n = strlen(file);           // contains no '/' unless refused below
    if (strchr(file, '/')) return false;
    const char* suffix = "";
    size_t base = n;
    if (allowTemp && n > 4 && (ieq(file + n - 4, 4, ".tmp") || ieq(file + n - 4, 4, ".old"))) {
        suffix = lc(file[n - 3]) == 't' ? ".tmp" : ".old";
        base = n - 4;
        out.temp = true;
    }
    if (ieq(file, base, "theme.json"))      out.kind = K_THEME_JSON;
    else if (ieq(file, base, "emblem.bin")) out.kind = K_EMBLEM;
    else { out = Path(); return false; }
    snprintf(out.canon, sizeof out.canon, "/themes/%s/%s%s", out.folder,
             out.kind == K_THEME_JSON ? "theme.json" : "emblem.bin", suffix);
    return true;
}

// "/WD-<1..24 of digits and '-'>.csv", any case. canon is the path as given.
inline bool sessionFile(const char* path, Path& out) {
    out = Path();
    if (!path || path[0] != '/') return false;
    const size_t n = strnlen(path, PATH_IN_MAX + 1);
    if (n < 9 || n > 1 + 3 + 24 + 4 || !ieq(path, 4, "/wd-") || !ieq(path + n - 4, 4, ".csv")) return false;
    for (size_t i = 4; i < n - 4; i++)
        if (!((path[i] >= '0' && path[i] <= '9') || path[i] == '-')) return false;
    out.kind = K_SESSION;
    memcpy(out.canon, path, n);
    out.canon[n] = 0;
    return true;
}

constexpr const char* NOT_ALLOWED = "path not allowed";

// nullptr = allowed (out filled); otherwise the reason for "ERR <reason>".
inline const char* putPolicy(const char* path, uint32_t size, Path& out) {
    if (!themeFile(path, out, false)) { out = Path(); return NOT_ALLOWED; }
    if (size == 0) return "empty file";
    if (size > capOf(out.kind))
        return out.kind == K_THEME_JSON ? "too large (theme.json max 4096 bytes)"
                                        : "too large (emblem.bin max 9728 bytes)";
    return nullptr;
}

// `active` is logging::fileName() ("" or null when not logging).
inline const char* delPolicy(const char* path, const char* active, Path& out) {
    if (themeFile(path, out, true)) return nullptr;
    if (sessionFile(path, out)) {
        if (active && active[0]) {
            const char* a = active[0] == '/' ? active : nullptr;
            if (a && strlen(a) == strlen(out.canon) && ieq(out.canon, strlen(out.canon), a))
                return "file is the active logging session";
        }
        return nullptr;
    }
    out = Path();
    return NOT_ALLOWED;
}

inline const char* rmdirPolicy(const char* folder, Path& out) {
    out = Path();
    const size_t n = folder ? strnlen(folder, 32) : 32;
    if (n > 31 || !themeFolderOk(folder)) return NOT_ALLOWED;
    memcpy(out.folder, folder, n);
    out.folder[n] = 0;
    snprintf(out.canon, sizeof out.canon, "/themes/%s", out.folder);
    return nullptr;
}

// --- the card -------------------------------------------------------------------
// All paths absolute. One temp file is open at a time (the upload's).
struct Io {
    virtual ~Io() {}
    virtual bool present() = 0;
    virtual bool exists(const char* path) = 0;
    virtual bool isDir(const char* path) = 0;
    virtual bool mkdir(const char* path) = 0;           // false if it exists
    virtual bool remove(const char* path) = 0;          // a file
    virtual bool rmdir(const char* path) = 0;           // an empty directory
    virtual bool rename(const char* from, const char* to) = 0;   // fails if `to` exists
    virtual bool tmpOpen(const char* path) = 0;         // create/truncate, read+write
    virtual bool tmpAppend(const uint8_t* data, size_t n) = 0;   // false on a short write
    virtual long tmpRead(uint32_t off, uint8_t* buf, size_t n) = 0;  // bytes, -1 on error
    virtual bool tmpClose() = 0;                        // flush + close; false on error
    virtual bool crcOf(const char* path, uint32_t& size, uint32_t& crc) = 0;  // re-read from the card
    virtual void note(bool warn, const char* msg) = 0;  // one diag-log line
};

enum Action { NOT_MINE, HANDLED, REBOOT };

class Console {
public:
    explicit Console(Io& io) : io_(io) {}

    bool uploading() const { return up_; }

    // One console line (from LineBuf). Writes a one-line reply (no newline)
    // to `reply` unless NOT_MINE, in which case the caller's own command
    // table runs. `active` is the live logging session file.
    Action line(const char* line, uint32_t nowMs, const char* active, char* reply, size_t cap) {
        if (cap) reply[0] = 0;
        const Tokens t = split(line);
        const bool isPut = firstIs(line, "SDPUT"), isD = firstIs(line, "D"), isEnd = firstIs(line, "END"),
                   isAbort = firstIs(line, "ABORT"), isDel = firstIs(line, "SDDEL"),
                   isRmdir = firstIs(line, "SDRMDIR"), isReboot = firstIs(line, "REBOOT");
        if (isReboot) {
            if (t.count != 1) { out(reply, cap, "ERR usage: REBOOT"); return HANDLED; }
            if (up_) abort("reboot");
            out(reply, cap, "OK rebooting");
            return REBOOT;
        }
        if (isPut) {
            if (up_) abort("replaced by a new SDPUT");
            put(t, nowMs, reply, cap);
            return HANDLED;
        }
        if (isD || isEnd || isAbort) {
            if (!up_) { out(reply, cap, "ERR no upload"); return HANDLED; }
            if (isAbort) {
                if (t.count != 1) { out(reply, cap, "ERR usage: ABORT"); return HANDLED; }
                abort("aborted by the host");
                out(reply, cap, "OK aborted");
                return HANDLED;
            }
            lastMs_ = nowMs;
            if (isD) chunk(t, reply, cap);
            else if (t.count != 1) out(reply, cap, "ERR usage: END");
            else finish(reply, cap);
            return HANDLED;
        }
        if (up_) { out(reply, cap, "ERR upload in progress (ABORT to cancel)"); return HANDLED; }
        if (isDel)   { del(t, active, reply, cap); return HANDLED; }
        if (isRmdir) { rmdir(t, reply, cap); return HANDLED; }
        return NOT_MINE;
    }

    // Every loop. True (and a reply line to print) when an upload timed out.
    bool tick(uint32_t nowMs, char* reply, size_t cap) {
        if (!up_ || nowMs - lastMs_ < IDLE_MS) return false;
        abort("timed out, no data for 10 s");
        out(reply, cap, "ERR upload timed out");
        return true;
    }

    // Abandon a running upload: close and remove the temp, keep the target.
    void abort(const char* why) {
        if (!up_) return;
        up_ = false;
        io_.tmpClose();
        io_.remove(tmp_);
        char m[REPLY_CAP];
        snprintf(m, sizeof m, "put %s abandoned (%s), card unchanged", target_, why);
        io_.note(true, m);
    }

private:
    Io&      io_;
    bool     up_ = false;
    uint32_t size_ = 0, crc_ = 0, written_ = 0, run_ = 0, lastMs_ = 0;
    char     target_[PATH_CAP] = "", tmp_[PATH_CAP + 4] = "", old_[PATH_CAP + 4] = "";

    static bool firstIs(const char* line, const char* w) {
        const size_t n = strlen(w);
        return line && !strncmp(line, w, n) && (line[n] == 0 || line[n] == ' ');
    }
    static void out(char* r, size_t cap, const char* fmt, ...) __attribute__((format(printf, 3, 4))) {
        if (!cap) return;
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(r, cap, fmt, ap);
        va_end(ap);
    }

    // Fail an upload that has started: the temp goes, the target stays.
    void fail(char* reply, size_t cap, const char* why) {
        abort(why);
        out(reply, cap, "ERR %s", why);
    }

    void put(const Tokens& t, uint32_t nowMs, char* reply, size_t cap) {
        if (t.count != 4) { out(reply, cap, "ERR usage: SDPUT <path> <size> <crc32hex>"); return; }
        if (t.n[1] > PATH_IN_MAX) { out(reply, cap, "ERR %s", NOT_ALLOWED); return; }
        char path[PATH_IN_MAX + 1];
        memcpy(path, t.p[1], t.n[1]);
        path[t.n[1]] = 0;
        uint32_t size = 0, crc = 0;
        if (!parseDec(t.p[2], t.n[2], size)) { out(reply, cap, "ERR bad size"); return; }
        if (!parseCrc(t.p[3], t.n[3], crc))  { out(reply, cap, "ERR bad crc32 (8 hex digits)"); return; }
        Path p;
        if (const char* why = putPolicy(path, size, p)) { out(reply, cap, "ERR %s", why); return; }
        if (!io_.present()) { out(reply, cap, "ERR no card"); return; }
        char dir[PATH_CAP];
        snprintf(dir, sizeof dir, "/themes/%s", p.folder);
        if (!ensureDir("/themes") || !ensureDir(dir)) { out(reply, cap, "ERR cannot create %s", dir); return; }
        snprintf(target_, sizeof target_, "%s", p.canon);
        snprintf(tmp_, sizeof tmp_, "%s.tmp", p.canon);
        snprintf(old_, sizeof old_, "%s.old", p.canon);
        if (io_.exists(tmp_) && (io_.isDir(tmp_) || !io_.remove(tmp_))) {
            out(reply, cap, "ERR cannot clear %s", tmp_); return;
        }
        if (!io_.tmpOpen(tmp_)) { io_.remove(tmp_); out(reply, cap, "ERR cannot create %s", tmp_); return; }
        up_ = true;
        size_ = size; crc_ = crc; written_ = 0; run_ = 0; lastMs_ = nowMs;
        out(reply, cap, "OK put %s %u", target_, (unsigned)size_);
    }

    bool ensureDir(const char* d) {
        if (io_.isDir(d)) return true;
        if (io_.exists(d)) return false;              // a file in the way
        return io_.mkdir(d) && io_.isDir(d);
    }

    void chunk(const Tokens& t, char* reply, size_t cap) {
        uint32_t off = 0;
        uint8_t data[CHUNK_MAX];
        if (t.count != 3 || !parseDec(t.p[1], t.n[1], off)) { out(reply, cap, "ERR usage: D <offset> <hex>"); return; }
        const long n = hexDecode(t.p[2], t.n[2], data, sizeof data);
        if (n <= 0) { out(reply, cap, "ERR bad hex"); return; }
        if ((uint64_t)off + (uint64_t)n > size_) { out(reply, cap, "ERR past declared size %u", (unsigned)size_); return; }
        if (off > written_) { out(reply, cap, "ERR gap, expected offset %u", (unsigned)written_); return; }
        if (!io_.present()) { fail(reply, cap, "no card"); return; }
        // A repeat of bytes already written (the host's retry after a lost
        // OK) is accepted only if it matches what is on the card.
        size_t same = written_ - off < (uint32_t)n ? (size_t)(written_ - off) : (size_t)n;
        if (same) {
            uint8_t have[CHUNK_MAX];
            if (io_.tmpRead(off, have, same) != (long)same) { fail(reply, cap, "read-back failed"); return; }
            if (memcmp(have, data, same)) { out(reply, cap, "ERR chunk at %u differs from bytes written", (unsigned)off); return; }
        }
        if ((size_t)n > same) {
            const size_t k = (size_t)n - same;
            if (!io_.tmpAppend(data + same, k)) { fail(reply, cap, "write failed (card full or removed?)"); return; }
            run_ = crc32(run_, data + same, k);
            written_ += (uint32_t)k;
        }
        out(reply, cap, "OK %u", (unsigned)off);
    }

    void finish(char* reply, size_t cap) {
        if (written_ != size_) {
            char why[64];
            snprintf(why, sizeof why, "short, %u of %u bytes", (unsigned)written_, (unsigned)size_);
            fail(reply, cap, why);
            return;
        }
        if (run_ != crc_) { fail(reply, cap, "crc mismatch on the received bytes"); return; }
        up_ = false;                                   // from here the temp is closed
        if (!io_.tmpClose()) { io_.remove(tmp_); bad(reply, cap, "flush failed"); return; }
        // Verify what is on the CARD, not what was received: re-read it.
        uint32_t sz = 0, c = 0;
        if (!io_.crcOf(tmp_, sz, c) || sz != size_ || c != crc_) {
            io_.remove(tmp_);
            bad(reply, cap, "crc mismatch reading the card back");
            return;
        }
        // Swap: target -> .old, temp -> target, drop .old. FAT's rename will
        // not replace an existing file. Any failure puts the old file back.
        const bool hadOld = io_.exists(target_);
        if (hadOld) {
            if (io_.exists(old_)) io_.remove(old_);
            if (!io_.rename(target_, old_)) { io_.remove(tmp_); bad(reply, cap, "rename failed"); return; }
        }
        if (!io_.rename(tmp_, target_)) {
            if (hadOld) io_.rename(old_, target_);
            io_.remove(tmp_);
            bad(reply, cap, "rename failed");
            return;
        }
        if (hadOld) io_.remove(old_);
        char m[REPLY_CAP];
        snprintf(m, sizeof m, "put %s %u bytes", target_, (unsigned)size_);
        io_.note(false, m);
        out(reply, cap, "DONE %u %08x", (unsigned)size_, (unsigned)crc_);
    }

    // A failure after the temp was closed (abort() no longer applies).
    void bad(char* reply, size_t cap, const char* why) {
        char m[REPLY_CAP];
        snprintf(m, sizeof m, "put %s failed (%s), card unchanged", target_, why);
        io_.note(true, m);
        out(reply, cap, "ERR %s", why);
    }

    void del(const Tokens& t, const char* active, char* reply, size_t cap) {
        if (t.count != 2) { out(reply, cap, "ERR usage: SDDEL <path>"); return; }
        if (t.n[1] > PATH_IN_MAX) { out(reply, cap, "ERR %s", NOT_ALLOWED); return; }
        char path[PATH_IN_MAX + 1];
        memcpy(path, t.p[1], t.n[1]);
        path[t.n[1]] = 0;
        Path p;
        if (const char* why = delPolicy(path, active, p)) { out(reply, cap, "ERR %s", why); return; }
        if (!io_.present()) { out(reply, cap, "ERR no card"); return; }
        if (!io_.exists(p.canon) || io_.isDir(p.canon)) { out(reply, cap, "ERR no such file"); return; }
        if (!io_.remove(p.canon)) { out(reply, cap, "ERR remove failed"); return; }
        char m[REPLY_CAP];
        snprintf(m, sizeof m, "del %s", p.canon);
        io_.note(false, m);
        out(reply, cap, "OK");
    }

    void rmdir(const Tokens& t, char* reply, size_t cap) {
        if (t.count != 2) { out(reply, cap, "ERR usage: SDRMDIR <folder>"); return; }
        char f[33];
        if (t.n[1] > 31) { out(reply, cap, "ERR %s", NOT_ALLOWED); return; }
        memcpy(f, t.p[1], t.n[1]);
        f[t.n[1]] = 0;
        Path p;
        if (const char* why = rmdirPolicy(f, p)) { out(reply, cap, "ERR %s", why); return; }
        if (!io_.present()) { out(reply, cap, "ERR no card"); return; }
        if (!io_.isDir(p.canon)) { out(reply, cap, "ERR no such folder"); return; }
        if (!io_.rmdir(p.canon)) { out(reply, cap, "ERR folder not empty"); return; }
        char m[REPLY_CAP];
        snprintf(m, sizeof m, "rmdir %s", p.canon);
        io_.note(false, m);
        out(reply, cap, "OK");
    }
};

} // namespace sdcmd
