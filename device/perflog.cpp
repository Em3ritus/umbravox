// Device side of perflog: sample the metrics once per interval, hold three rows
// in RAM, and append them to /perf.csv in one write -- one SD stall per 30 s
// rather than one per row. All card access goes through sdlog, and rotation
// reuses the diag log's rule so neither file can fill the card.
#include "perflog.h"
#include "elog.h"        // shouldRotate()
#include "sdlog.h"
#include "capture.h"
#include "blescan.h"
#include "meshcore.h"
#include "model.h"
#include <Arduino.h>
#include <esp_heap_caps.h>

namespace perflog {
namespace {

Acc      acc;
char     rows[BATCH][200];
int      nrows = 0;
uint32_t lastRow = 0, prevLoop = 0;
uint32_t lastWifi = 0, lastAdv = 0, lastReps = 0;     // totals at the last row

// Write the buffered rows, with the header first when the file is new (a
// fresh card, or just after a rotation).
void writeRows() {
    if (nrows == 0) return;
    if (!sdlog::present()) { nrows = 0; return; }     // no card: rows are dropped, not queued

    size_t total = 0;
    for (int i = 0; i < nrows; i++) total += strlen(rows[i]);
    if (elog::shouldRotate(sdlog::sizeOf(PATH), (uint32_t)total, CAP))
        sdlog::rotate(PATH, OLD);
    if (sdlog::sizeOf(PATH) == 0)
        sdlog::append(PATH, (const uint8_t*)header(), (uint32_t)strlen(header()));

    // One buffer, one append: the open/close is the expensive part.
    static char blob[BATCH * 200];
    size_t n = 0;
    for (int i = 0; i < nrows; i++) {
        const size_t l = strlen(rows[i]);
        memcpy(blob + n, rows[i], l);
        n += l;
    }
    sdlog::append(PATH, (const uint8_t*)blob, (uint32_t)n);
    nrows = 0;
}

} // namespace

void loopStart(uint32_t now) {
    if (prevLoop) acc.gap(now - prevLoop);
    prevLoop = now;
}

void frame(uint32_t drawMs, uint32_t blitMs) { acc.frame(drawMs, blitMs); }

void tick(uint32_t now, const ui::State& s, bool dark) {
    if (now - lastRow < INTERVAL_MS) return;
    const uint32_t dt = lastRow ? now - lastRow : now;
    lastRow = now;

    Snap sn;
    sn.upS       = now / 1000;
    sn.dark      = dark;
    sn.heapFree  = ESP.getFreeHeap();
    sn.heapMin   = ESP.getMinFreeHeap();
    sn.heapBlock = (uint32_t)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
    sn.psramFree = ESP.getFreePsram();
    sn.stackFree = (uint32_t)uxTaskGetStackHighWaterMark(nullptr) * sizeof(StackType_t);

    const uint32_t wifi = cap::frames(), adv = blescan::adverts(), reps = meshcore::reportsRx();
    sn.wifiFrames = delta(wifi, lastWifi);     // reset-safe (perflog.h)
    sn.bleAdverts = delta(adv,  lastAdv);
    sn.reports    = delta(reps, lastReps);
    sn.wifiUniq = cap::uniques();
    sn.bleUniq  = blescan::uniques();
    sn.tableCap = cap::tableCap();
    sn.nodes    = s.nNodes;
    sn.lossPct  = s.meshLossPct;
    sn.camped   = s.coreCamping;
    sn.fix      = s.gpsFix;
    sn.theme    = s.set.theme;
    sn.sweep    = s.set.sweep;

    acc.sd(sdlog::takeMaxStallMs());               // what the card cost this interval
    row(rows[nrows], sizeof rows[nrows], acc, sn, dt);
    acc.clear();
    if (++nrows >= BATCH) writeRows();
}

void flush() { writeRows(); }

void dump(Print& out) {
    flush();                                       // include the rows still in RAM
    if (!sdlog::present()) { out.println("(no card)"); return; }
    if (!sdlog::dump(out, PATH)) out.println("(no perf.csv yet -- first rows land after 30 s)");
}

} // namespace perflog
