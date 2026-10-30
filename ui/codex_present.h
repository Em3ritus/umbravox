// codex_present.h -- the CODEX device adapter's pure core.
//
// Mirrors scan_present.h / domain_present.h. The screen-agnostic machinery
// (StripScheduler / Transition / TouchGate / PresentMetrics / the suspend
// drain, and the row-identity FrozenOrder) is SHARED from scanpres:: and
// dompres:: rather than duplicated; CODEX adds only:
//   CodexStore  -- a STABLE snapshot owning the row key/label bytes;
//   sceneDirty  -- which display bands (status / body) changed;
//   CodexEngine -- the entry->commit->service sequencer for CodexScene.
// Host + device compilable (no Arduino deps).
#pragma once
#include "codex_render.h"
#include "scan_present.h"     // scanpres:: scheduler/transition/touch-gate/metrics
#include "domain_present.h"   // dompres:: FrozenOrder (decision #5 row freeze), reused
#include "model.h"            // ui::MAX_SIGHTS / SSID_MAX
#include <cstdint>

namespace codpres {

using codr::CodexScene;
using codr::CodexRow;

using scanpres::StripScheduler;
using scanpres::Transition;
using scanpres::TouchGate;
using scanpres::PresentMetrics;
using scanpres::serviceWhileSuspended;
using dompres::FrozenOrder;     // the list-order freeze is shared, not re-made

// --- stable scene snapshot ----------------------------------------------------
class CodexStore {
public:
    static constexpr int MAXROWS = ui::MAX_SIGHTS;   // 8
    static constexpr int STRMAX  = ui::AP_SSID_MAX;     // 33

    void begin(uint8_t theme, uint32_t now) {
        nr_ = 0;
        sc_ = CodexScene{};
        sc_.theme = theme; sc_.now = now;
        sc_.rows = rows_; sc_.nRows = 0;
    }
    void gps(const codr::GnssFix& f) { sc_.gps = f; }
    void status(int nodes, bool hasCell, int batt, codr::Rec rec) {
        sc_.nodes = nodes; sc_.hasCell = hasCell; sc_.battPct = batt; sc_.rec = rec;
    }
    void filter(uint8_t f) { sc_.filter = f; }
    void page(int lo, int hi, int total) { sc_.pageLo = lo; sc_.pageHi = hi; sc_.listTotal = total; }

    // Append a row (key + label copied into owned storage). Returns false full.
    bool addRow(const char* key, const char* label, uint8_t band, int rssi, uint8_t chan, bool threat) {
        if (nr_ >= MAXROWS) return false;
        copy_(keyBuf_[nr_], STRMAX, key);
        copy_(lblBuf_[nr_], STRMAX, label);
        rows_[nr_] = CodexRow{ keyBuf_[nr_], lblBuf_[nr_], band, rssi, chan, threat };
        nr_++; sc_.nRows = nr_; return true;
    }

    const CodexScene& scene() const { return sc_; }
    int rowCount() const { return nr_; }

private:
    static void copy_(char* dst, int cap, const char* src) {
        int k = 0; for (; src && src[k] && k < cap - 1; k++) dst[k] = src[k]; dst[k] = 0;
    }
    CodexRow rows_[MAXROWS];
    char     keyBuf_[MAXROWS][STRMAX], lblBuf_[MAXROWS][STRMAX];
    int      nr_ = 0;
    CodexScene sc_;
};

// --- scene diff: which display bands changed ---------------------------------
inline void sceneDirty(const CodexScene& a, const CodexScene& b, bool& status, bool& body) {
    char la[16], lb[16];
    const bool gpsSame = scanr::gpsLabel(a.gps, la, 16) == scanr::gpsLabel(b.gps, lb, 16)
                         && !__builtin_strcmp(la, lb);
    char ra[16], rb[16];
    scanr::ScanScene sa; sa.rec = a.rec; scanr::ScanScene sb; sb.rec = b.rec;
    const bool recSame = scanr::recLabel(sa, ra, 16) == scanr::recLabel(sb, rb, 16)
                         && !__builtin_strcmp(ra, rb);
    status = !gpsSame || !recSame || a.nodes != b.nodes
             || a.hasCell != b.hasCell || a.battPct != b.battPct;

    bool bodySame = a.filter == b.filter && a.nRows == b.nRows
                    && a.pageLo == b.pageLo && a.pageHi == b.pageHi && a.listTotal == b.listTotal;
    for (int i = 0; bodySame && i < a.nRows; i++)
        bodySame = a.rows[i].band == b.rows[i].band && a.rows[i].rssi == b.rows[i].rssi
                   && a.rows[i].chan == b.rows[i].chan && a.rows[i].threat == b.rows[i].threat
                   && !__builtin_strcmp(a.rows[i].key   ? a.rows[i].key   : "", b.rows[i].key   ? b.rows[i].key   : "")
                   && !__builtin_strcmp(a.rows[i].label ? a.rows[i].label : "", b.rows[i].label ? b.rows[i].label : "");
    body = !bodySame;
}

// --- the adapter's pure sequencer (CodexScene twin of the scan/domain Engine) -
class CodexEngine {
public:
    void reset(int W, int H) {
        W_ = W; H_ = H;
        sched_.reset(H);
        trans_.leave();
        metrics_.clear();
        store_[0].begin(0, 0); store_[1].begin(0, 0);
        cur_ = 0; haveFront_ = false; forceFull_ = true;
    }
    void enter()        { trans_.enter(); forceFull_ = true; sched_.reset(H_); }
    void wake()         { trans_.enter(); forceFull_ = true; sched_.reset(H_); }
    void themeChanged() { trans_.enter(); forceFull_ = true; sched_.reset(H_); }
    void leave()        { trans_.leave(); sched_.reset(H_); }

    bool acceptsTouch() const { return trans_.acceptsTouch(); }
    bool pending()      const { return sched_.pending(); }
    bool idle()         const { return !sched_.pending(); }

    CodexStore&       back()  { return store_[cur_ ^ 1]; }
    const CodexScene& scene() const { return store_[cur_].scene(); }

    void commit() {
        const CodexScene& nw = store_[cur_ ^ 1].scene();
        if (forceFull_ || !haveFront_) {
            sched_.invalidateAll();
        } else {
            bool st = false, bd = false;
            sceneDirty(store_[cur_].scene(), nw, st, bd);
            const uieng::CodexLayout L = uieng::codex(W_, H_);
            const int statusBottom = L.statusCells[0].y + L.statusCells[0].h;
            const int navTop       = L.navCells[0].y;
            if (st) sched_.invalidate(0, statusBottom);
            if (bd) sched_.invalidate(statusBottom, navTop);
            { scanr::ScanScene za; za.rec = store_[cur_].scene().rec; scanr::ScanScene zb; zb.rec = nw.rec;  // design #1: STOP/REC lives on the nav bar now
              char ra[16], rb[16]; scanr::recLabel(za, ra, 16); scanr::recLabel(zb, rb, 16);
              if (__builtin_strcmp(ra, rb)) sched_.invalidate(navTop, H_); }
        }
        cur_ ^= 1; haveFront_ = true; forceFull_ = false;
    }

    StripScheduler::Slice nextSlice(int budget) {
        const bool was = sched_.pending();
        const StripScheduler::Slice s = sched_.step(budget);
        if (was && s.done) { trans_.settled(); metrics_.frameDone(); }
        return s;
    }
    void recordSlice(uint32_t composeUs, uint32_t transferUs) { metrics_.slice(composeUs, transferUs); }
    void sampleCapture(uint32_t f, uint32_t u, uint32_t ble)  { metrics_.capture(f, u, ble); }
    const PresentMetrics& metrics() const { return metrics_; }
    void clearMetrics() { metrics_.clear(); }
    bool forcingFull() const { return forceFull_; }

private:
    CodexStore      store_[2];
    StripScheduler  sched_;
    Transition      trans_;
    PresentMetrics  metrics_;
    int  W_ = 0, H_ = 0, cur_ = 0;
    bool haveFront_ = false, forceFull_ = true;
};

}  // namespace codpres
