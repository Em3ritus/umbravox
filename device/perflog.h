// perflog -- a performance log on the SD card, one CSV row per interval, so how
// the instrument is actually running can be read back from a file instead of
// watched over USB: frame cost, the worst stall, heap trend, capture and mesh
// rates, and what the SD writes themselves cost.
//
// The accumulator and the row formatter are pure and live here so they can be
// host-tested (sim/test_perflog.cpp). Sampling the metrics, batching rows and
// writing them is perflog.cpp, device only.
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>

namespace perflog {

constexpr uint32_t INTERVAL_MS = 10000;   // one row per this long
constexpr int      BATCH       = 3;       // rows per SD write: one stall per 30 s
constexpr uint32_t CAP         = 1u << 20; // rotate at 1 MiB (~19 h of rows)
constexpr const char* PATH = "/perf.csv";
constexpr const char* OLD  = "/perf.0.csv";

// What one interval accumulates, a sample at a time.
struct Acc {
    uint32_t frames = 0;
    uint32_t drawSum = 0, drawMax = 0;
    uint32_t blitSum = 0, blitMax = 0;
    uint32_t gapMax = 0;      // longest time between loop() calls: the stall
    uint32_t sdMax  = 0;      // longest SD write in the interval

    void frame(uint32_t drawMs, uint32_t blitMs) {
        frames++;
        drawSum += drawMs; if (drawMs > drawMax) drawMax = drawMs;
        blitSum += blitMs; if (blitMs > blitMax) blitMax = blitMs;
    }
    void gap(uint32_t ms) { if (ms > gapMax) gapMax = ms; }
    void sd(uint32_t ms)  { if (ms > sdMax)  sdMax  = ms; }
    void clear() { *this = Acc(); }
};

// What the device reads at row time. The rate fields are deltas over the
// interval, not running totals.
struct Snap {
    uint32_t upS = 0;
    bool     dark = false;            // screen off: no frames drawn
    uint32_t heapFree = 0, heapMin = 0, heapBlock = 0, psramFree = 0, stackFree = 0;
    uint32_t wifiFrames = 0, bleAdverts = 0, reports = 0;       // deltas
    uint32_t wifiUniq = 0, bleUniq = 0, tableCap = 0;
    uint8_t  nodes = 0, lossPct = 0;
    bool     camped = false, fix = false;
    uint8_t  theme = 0, sweep = 0;
};

// The header row. It must name exactly the columns row() writes, in order; the
// host test counts them.
inline const char* header() {
    return "t_s,dark,fps,draw_avg,draw_max,blit_avg,blit_max,gap_max,sd_max,"
           "heap_free,heap_min,heap_blk,psram_free,stack_free,"
           "wifi_fps,ble_aps,wifi_n,ble_n,tbl_pct,nodes,rep_s,loss_pct,camp,fix,theme,sweep\n";
}

// One CSV row for the interval, newline terminated. Always NUL-terminates and
// never writes past `n`; returns the bytes written. Safe for an interval with
// no frames (a dark screen), a zero-length interval, and an empty table. Pure.
inline int row(char* out, size_t n, const Acc& a, const Snap& s, uint32_t intervalMs) {
    if (!out || n == 0) return 0;
    const uint32_t dt  = intervalMs ? intervalMs : 1;
    const uint32_t fr  = a.frames ? a.frames : 1;
    const uint32_t fps10 = (uint32_t)((uint64_t)a.frames * 10000u / dt);
    auto perSec = [dt](uint32_t d) { return (unsigned long)(((uint64_t)d * 1000u + dt / 2) / dt); };
    const unsigned long tbl = s.tableCap ? (unsigned long)((uint64_t)s.wifiUniq * 100u / s.tableCap) : 0;

    const int w = snprintf(out, n,
        "%lu,%d,%lu.%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%d,%d,%lu,%lu\n",
        (unsigned long)s.upS, s.dark ? 1 : 0,
        (unsigned long)(fps10 / 10), (unsigned long)(fps10 % 10),
        (unsigned long)(a.drawSum / fr), (unsigned long)a.drawMax,
        (unsigned long)(a.blitSum / fr), (unsigned long)a.blitMax,
        (unsigned long)a.gapMax, (unsigned long)a.sdMax,
        (unsigned long)s.heapFree, (unsigned long)s.heapMin, (unsigned long)s.heapBlock,
        (unsigned long)s.psramFree, (unsigned long)s.stackFree,
        perSec(s.wifiFrames), perSec(s.bleAdverts),
        (unsigned long)s.wifiUniq, (unsigned long)s.bleUniq, tbl,
        (unsigned long)s.nodes, perSec(s.reports), (unsigned long)s.lossPct,
        s.camped ? 1 : 0, s.fix ? 1 : 0,
        (unsigned long)s.theme, (unsigned long)s.sweep);
    if (w < 0) { out[0] = '\0'; return 0; }
    return (size_t)w >= n ? (int)(n - 1) : w;
}

} // namespace perflog

class Print;
namespace ui { struct State; }

namespace perflog {

// --- device side (perflog.cpp) --------------------------------------------
void loopStart(uint32_t now);                   // every loop iteration, at its top
void frame(uint32_t drawMs, uint32_t blitMs);   // after each drawn frame
// Every iteration, with the live state: writes a row when an interval is up and
// a batch to the card every BATCH rows.
void tick(uint32_t now, const ui::State& s, bool dark);
void flush();                                   // force buffered rows to the card
void dump(Print& out);                          // flush, then stream /perf.csv

} // namespace perflog
