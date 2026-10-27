// The append-only WiGLE logger core. Pure -- no Arduino, no SD -- so it is
// host-tested in sim/test_applog.cpp against a fake card.
//
// Why append: the logger used to rewrite the whole CSV every flush, so the
// stall grew with the file (1.1 s at 2.5k rows, 5.4 s at 8k, 37-64 s later in
// the field) and froze the UI. Now each flush appends only rows not yet
// written, capped at BATCH_ROWS, so the stall is proportional to new rows.
//
// The model. Each source (Wi-Fi, BLE) lists its rows in first-seen order --
// the capture tables' order_ arrays, which only ever grow until a clear. A
// done-bit per position records "on the card, or never will be". `low` is the
// first position not done, so a flush scans from there: rows waiting for a fix
// keep low back and are re-checked each flush until they can be written.
//
// Exact-once. A batch is scanned first (no I/O), then written and closed.
// Only a clean close marks its rows done and advances the file's good length.
// On any failure nothing is marked, and the bytes that did land are cut back
// to the good length -- now if possible, else before the next append -- so a
// retried row can never sit next to a fragment of itself.
//
// Never someone else's file: a file this session has not written to yet must
// be empty when opened, and only bytes this session wrote are ever cut.
#pragma once
#include <stdint.h>
#include <string.h>

namespace applog {

// Rollover: at this many unique Wi-Fi APs the session moves to a new file and
// the dedup table is cleared, well before its 32768 slots fill (linear probing
// stays short at 61% load). BLE gets the same rule against its 4096-slot table
// (63%): BLE addresses rotate, so its table fills from churn, not from places.
// capture.cpp / blescan.cpp static_assert each threshold against its table.
constexpr uint32_t ROLLOVER_APS = 20000;
constexpr uint32_t ROLLOVER_BLE = 2600;

// Rows per flush. A row is ~150 bytes, so 200 rows is ~30 KB: the field logs
// put the old rewrite at ~2.3 KB/ms all-in (2.5k rows in 1.1 s), which makes
// this ~0.1 s per flush. A backlog (logging started on a full table) drains
// at this rate over successive flushes instead of in one long stall.
constexpr uint32_t BATCH_ROWS = 200;

// Longest formatted row plus its NUL. A WiFi row is at most ~210 bytes (MAC 17,
// quoted SSID 47, auth 63, date 19, numbers and commas).
constexpr int ROW_MAX = 256;

inline bool rolloverDue(uint32_t wifiUniques, uint32_t bleUniques) {
    return wifiUniques >= ROLLOVER_APS || bleUniques >= ROLLOVER_BLE;
}

enum Verdict : uint8_t {
    WRITE,   // exportable now
    WAIT,    // not yet (no position): ask again next flush
    NEVER,   // can never be exported (no UTC date at first sight): settle it
};

// One table of rows, in first-seen order.
struct Source {
    virtual uint32_t count() = 0;            // rows so far
    virtual uint32_t capacity() = 0;         // most rows there can be
    virtual uint8_t* doneBits() = 0;         // capacity() bits, owned by the source
    virtual Verdict  verdict(uint32_t i) = 0;
    // Row i as CSV, newline included. Returns its length, or 0 if the row
    // turned out not to be exportable after all (it is then retried).
    virtual int      format(uint32_t i, char* buf, int cap) = 0;
};

// One append to the card: open, writes, close.
struct Sink {
    // Open `name` for appending (created if missing); `size` = its length now.
    virtual bool open(const char* name, uint32_t& size) = 0;
    virtual bool write(const char* data, uint32_t len) = 0;
    // Flush and close. False if anything since open() failed.
    virtual bool close() = 0;
    virtual bool truncate(const char* name, uint32_t len) = 0;
};

struct Cursor { uint32_t low = 0; };            // positions < low are all done

// The session file, as far as this session knows it.
struct FileState {
    uint32_t goodLen = 0;    // bytes committed by clean closes
    bool     started = false;// the header is on the card
};

// A row picked for the batch being written. Positions fit 16 bits: tables are
// at most 65536 rows (asserted where they are defined).
struct Pending { uint16_t i; uint8_t src; };

struct Result {
    bool     ok = false;     // the batch (possibly empty) is on the card
    bool     more = false;   // stopped at the row cap: rows remain
    bool     clash = false;  // the file existed before we wrote to it: rename
    uint32_t rows = 0;       // rows committed by this call
    uint32_t waiting = 0;    // rows seen that were not exportable yet
    uint32_t bytes = 0;      // bytes committed by this call
};

inline bool isDone(Source& s, uint32_t i) {
    return (s.doneBits()[i >> 3] >> (i & 7)) & 1u;
}
inline void markDone(Source& s, uint32_t i) {
    s.doneBits()[i >> 3] = (uint8_t)(s.doneBits()[i >> 3] | (1u << (i & 7)));
}

// A new session file, or the source's table was cleared: start the order over.
inline void resetCursor(Cursor& c, Source& s) {
    memset(s.doneBits(), 0, (s.capacity() + 7) / 8);
    c.low = 0;
}

inline uint32_t rowsIn(Source& s) {
    const uint32_t n = s.count(), cap = s.capacity();
    return n < cap ? n : cap;
}

// Append up to maxRows new rows from the sources (in order: all of source 0's
// pending rows, then source 1's) to `name`. `pend` holds maxRows entries.
inline Result appendBatch(const char* name, FileState& f, Sink& sink, const char* header,
                          Source* const* src, Cursor* cur, int nSrc,
                          uint32_t maxRows, Pending* pend) {
    Result r;

    // 1. Pick the rows. No I/O yet, so a refusal below leaves no trace.
    uint32_t np = 0;
    for (int s = 0; s < nSrc && !r.more; s++) {
        Source& S = *src[s];
        const uint32_t n = rowsIn(S);
        for (uint32_t i = cur[s].low; i < n; i++) {
            if (isDone(S, i)) continue;
            const Verdict v = S.verdict(i);
            if (v == NEVER) { markDone(S, i); continue; }   // nothing to write, ever
            if (v == WAIT)  { r.waiting++; continue; }
            if (np == maxRows) { r.more = true; break; }
            pend[np].i = (uint16_t)i;
            pend[np].src = (uint8_t)s;
            np++;
        }
    }
    // Settle the cursors past rows that were NEVER.
    for (int s = 0; s < nSrc; s++) {
        const uint32_t n = rowsIn(*src[s]);
        while (cur[s].low < n && isDone(*src[s], cur[s].low)) cur[s].low++;
    }
    if (np == 0 && f.started) { r.ok = true; return r; }  // nothing new: no I/O

    // 2. Open, and make sure the file is exactly what we last committed.
    uint32_t size = 0;
    if (!sink.open(name, size)) return r;
    if (size != f.goodLen) {
        sink.close();
        // Not ours to cut: a file we have not written yet that already has
        // bytes is an earlier session's; one shorter than our committed length
        // has been replaced or damaged under us.
        if (!f.started) { r.clash = true; return r; }
        if (size < f.goodLen) return r;
        // Longer: the residue of a failed append. Cut it, or write nothing.
        if (!sink.truncate(name, f.goodLen)) return r;
        if (!sink.open(name, size)) return r;
        if (size != f.goodLen) { sink.close(); return r; }
    }

    // 3. Write the header (new file) and the rows.
    uint32_t bytes = 0;
    bool wok = true;
    if (!f.started && header) {
        const uint32_t len = (uint32_t)strlen(header);
        wok = sink.write(header, len);
        bytes += len;
    }
    char row[ROW_MAX];
    uint32_t keep = 0;                   // pend[0..keep) actually written
    for (uint32_t k = 0; k < np && wok; k++) {
        const int len = src[pend[k].src]->format(pend[k].i, row, ROW_MAX);
        if (len <= 0 || len >= ROW_MAX) { r.waiting++; continue; }   // retried later
        if (!sink.write(row, (uint32_t)len)) { wok = false; break; }
        bytes += (uint32_t)len;
        pend[keep++] = pend[k];
    }
    const bool closed = sink.close();

    // 4. Commit, or roll back.
    if (!wok || !closed) {
        sink.truncate(name, f.goodLen);  // best effort; the next open re-checks
        return r;
    }
    for (uint32_t k = 0; k < keep; k++) markDone(*src[pend[k].src], pend[k].i);
    for (int s = 0; s < nSrc; s++) {
        const uint32_t n = rowsIn(*src[s]);
        while (cur[s].low < n && isDone(*src[s], cur[s].low)) cur[s].low++;
    }
    f.goodLen += bytes;
    f.started = true;
    r.ok = true;
    r.rows = keep;
    r.bytes = bytes;
    return r;
}

} // namespace applog
