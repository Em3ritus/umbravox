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

// A due roll waits for pending rows to reach the card, but never past these:
// at this occupancy it is forced at once (the rows not yet written are counted
// and logged), so arrivals faster than the drain cannot fill the table.
// 27000 of 32768 is 82%; 3300 of 4096 is 81% (asserted in capture/blescan).
constexpr uint32_t ROLL_HARD_APS = 27000;
constexpr uint32_t ROLL_HARD_BLE = 3300;
inline bool rolloverHard(uint32_t wifiUniques, uint32_t bleUniques) {
    return wifiUniques >= ROLL_HARD_APS || bleUniques >= ROLL_HARD_BLE;
}

enum Verdict : uint8_t {
    WRITE,   // exportable now
    WAIT,    // not yet (no position, or held): ask again next flush
    NEVER,   // can never be exported (no UTC date at first sight): settle it
};

// Row hold. A row is written once, so writing it the moment it is first
// located would freeze its position at the approach edge of range; capture
// keeps moving the position to the loudest sighting, so wait until the AP has
// gone quiet (we drove past it) or, for one heard constantly (parked beside
// it), until it has been known this long. `final` -- STOP, RESET, a rollover
// -- writes held rows at once. Cost: a power cut also loses rows still held
// (up to ~20 s of passing APs, up to 120 s of parked ones).
constexpr uint32_t HOLD_QUIET_MS = 20000;
constexpr uint32_t HOLD_MAX_MS   = 120000;

// The shared verdict rule. `firstEpoch` is set once, at first sight, so a row
// without one can never be exported; a position can still arrive later.
inline Verdict holdVerdict(bool located, uint32_t firstEpoch, uint32_t firstMs,
                           uint32_t lastMs, uint32_t nowMs, bool final) {
    if (!firstEpoch) return NEVER;
    if (!located) return WAIT;
    if (final) return WRITE;
    if (nowMs - lastMs >= HOLD_QUIET_MS || nowMs - firstMs >= HOLD_MAX_MS) return WRITE;
    return WAIT;
}

// One table of rows, in first-seen order.
struct Source {
    virtual uint32_t count() = 0;            // rows so far
    virtual uint32_t capacity() = 0;         // most rows there can be
    virtual uint8_t* doneBits() = 0;         // capacity() bits, owned by the source
    // Row i's state at `nowMs`; `final` releases held rows (holdVerdict).
    virtual Verdict  verdict(uint32_t i, uint32_t nowMs, bool final) = 0;
    // Row i as CSV, newline included. Returns its length, or 0 if the row
    // turned out not to be exportable after all (it is then retried). Ignores
    // the hold: the console dump prints every exportable row.
    virtual int      format(uint32_t i, char* buf, int cap) = 0;
};

// The close check a real card needs: the Arduino File flush ignores fflush/
// fsync failures, so the size on the card after the flush must be what was
// there at open plus every byte written, or the append did not fully land.
inline bool closeVerified(bool closedOk, uint32_t sizeAtOpen, uint32_t written,
                          uint32_t sizeAfter) {
    return closedOk && sizeAfter == sizeAtOpen + written;
}

// One append to the card: open, writes, close.
struct Sink {
    // Open `name` for appending (created if missing); `size` = its length now.
    virtual bool open(const char* name, uint32_t& size) = 0;
    virtual bool write(const char* data, uint32_t len) = 0;
    // Flush and close. False if anything since open() failed, including a
    // size on the card that does not match (closeVerified).
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
    bool     shrunk = false; // shorter than we committed (bytes we counted never
                             // landed, or the file was replaced): start a new file
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

// Rows that could be written now (with `final`, held rows count) but are not
// on the card yet: what a forced roll gives up.
inline uint32_t pendingRows(Source* const* src, Cursor* cur, int nSrc,
                            uint32_t nowMs, bool final) {
    uint32_t n = 0;
    for (int s = 0; s < nSrc; s++) {
        Source& S = *src[s];
        const uint32_t c = rowsIn(S);
        for (uint32_t i = cur[s].low; i < c; i++)
            if (!isDone(S, i) && S.verdict(i, nowMs, final) == WRITE) n++;
    }
    return n;
}

// Append up to maxRows new rows from the sources (in order: all of source 0's
// pending rows, then source 1's) to `name`. `pend` holds maxRows entries.
//
// Scan cost: every flush re-checks each not-done row from `low`, so rows that
// wait (held, or never located) keep the scan start back. Held rows are at
// most HOLD_MAX_MS old, but a row that is never located pins `low` for the
// rest of the session: the scan is then O(rows since the oldest such row) per
// flush, about 1 us each on the device (a locked read of a few fields).
inline Result appendBatch(const char* name, FileState& f, Sink& sink, const char* header,
                          Source* const* src, Cursor* cur, int nSrc,
                          uint32_t maxRows, Pending* pend,
                          uint32_t nowMs, bool final) {
    Result r;

    // 1. Pick the rows. No I/O yet, so a refusal below leaves no trace.
    uint32_t np = 0;
    for (int s = 0; s < nSrc && !r.more; s++) {
        Source& S = *src[s];
        const uint32_t n = rowsIn(S);
        for (uint32_t i = cur[s].low; i < n; i++) {
            if (isDone(S, i)) continue;
            const Verdict v = S.verdict(i, nowMs, final);
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
        // bytes is an earlier session's. One shorter than our committed length
        // lost rows we marked done (a flush that failed without saying so) or
        // was replaced: the caller moves to a new file and logs the table again.
        if (!f.started) { r.clash = true; return r; }
        if (size < f.goodLen) { r.shrunk = true; return r; }
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
