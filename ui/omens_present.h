// omens_present.h -- the OMENS device adapter's pure core. Shared machinery
// from scanpres::; OMENS adds the OmenStore (owned omen/source strings), the
// status/body scene diff, and the OmenEngine sequencer.
#pragma once
#include "omens_render.h"
#include "scan_present.h"
#include "model.h"
#include <cstdint>

namespace omnpres {

using omnr::OmenScene;
using omnr::OmenRow;
using scanpres::StripScheduler;
using scanpres::Transition;
using scanpres::PresentMetrics;

class OmenStore {
public:
    static constexpr int MAXROWS = ui::MAX_OMEN_ROWS;   // 6
    static constexpr int STRMAX  = ui::SSID_MAX;         // 20

    void begin(uint8_t theme, uint32_t now) {
        nr_ = 0; sc_ = OmenScene{}; sc_.theme = theme; sc_.now = now; sc_.rows = rows_; sc_.nRows = 0;
    }
    void gps(const omnr::GnssFix& f) { sc_.gps = f; }
    void storage(ui::StorageState sd){sc_.sd=sd;}
    void header(bool bridge, bool pmu, bool charging) { sc_.bridgeOn=bridge; sc_.pmuOk=pmu; sc_.charging=charging; }
    void poi(ui::PoiStatus value) { sc_.poi=value; }
    void status(int nodes, bool hasCell, int batt, omnr::Rec rec) {
        sc_.nodes = nodes; sc_.hasCell = hasCell; sc_.battPct = batt; sc_.rec = rec;
    }
    void total(int t) { sc_.total = t; }
    bool addRow(const char* omen, const char* source, uint8_t conf, uint32_t agoS, bool located) {
        if (nr_ >= MAXROWS) return false;
        copy_(oBuf_[nr_], STRMAX, omen); copy_(sBuf_[nr_], STRMAX, source);
        rows_[nr_] = OmenRow{ oBuf_[nr_], sBuf_[nr_], conf, agoS, located };
        nr_++; sc_.nRows = nr_; return true;
    }
    const OmenScene& scene() const { return sc_; }
    int rowCount() const { return nr_; }

private:
    static void copy_(char* d, int cap, const char* src){ int k=0; for(;src&&src[k]&&k<cap-1;k++) d[k]=src[k]; d[k]=0; }
    OmenRow rows_[MAXROWS];
    char    oBuf_[MAXROWS][STRMAX], sBuf_[MAXROWS][STRMAX];
    int     nr_ = 0;
    OmenScene sc_;
};

inline void sceneDirty(const OmenScene& a, const OmenScene& b, bool& status, bool& body) {
    char la[16], lb[16];
    const bool gpsSame = scanr::gpsLabel(a.gps, la, 16) == scanr::gpsLabel(b.gps, lb, 16) && !__builtin_strcmp(la, lb);
    char ra[16], rb[16];
    scanr::ScanScene sa; sa.rec = a.rec; scanr::ScanScene sb; sb.rec = b.rec;
    const bool recSame = scanr::recLabel(sa, ra, 16) == scanr::recLabel(sb, rb, 16) && !__builtin_strcmp(ra, rb);
    status = !gpsSame || !recSame || a.nodes != b.nodes || a.hasCell != b.hasCell || a.battPct != b.battPct || a.bridgeOn != b.bridgeOn || a.pmuOk != b.pmuOk || a.charging != b.charging || a.sd != b.sd;

    bool same = a.total == b.total && a.nRows == b.nRows;
    for (int i = 0; same && i < a.nRows; i++)
        same = a.rows[i].conf == b.rows[i].conf && a.rows[i].agoS == b.rows[i].agoS
               && a.rows[i].located == b.rows[i].located
               && !__builtin_strcmp(a.rows[i].omen?a.rows[i].omen:"", b.rows[i].omen?b.rows[i].omen:"")
               && !__builtin_strcmp(a.rows[i].source?a.rows[i].source:"", b.rows[i].source?b.rows[i].source:"");
    body = !same;
}

class OmenEngine {
public:
    void reset(int W, int H) {
        W_ = W; H_ = H; sched_.reset(H); trans_.leave(); metrics_.clear();
        store_[0].begin(0,0); store_[1].begin(0,0); cur_ = 0; haveFront_ = false; forceFull_ = true;
    }
    void enter() { trans_.enter(); forceFull_ = true; sched_.reset(H_); }
    void leave() { trans_.leave(); sched_.reset(H_); }
    bool acceptsTouch() const { return trans_.acceptsTouch(); }
    bool pending()      const { return sched_.pending(); }
    OmenStore&       back()  { return store_[cur_ ^ 1]; }
    const OmenScene& scene() const { return store_[cur_].scene(); }
    void commit() {
        const OmenScene& nw = store_[cur_ ^ 1].scene();
        if (forceFull_ || !haveFront_) sched_.invalidateAll();
        else { bool st=false,bd=false; sceneDirty(store_[cur_].scene(), nw, st, bd);
            const uieng::LogLayout L = uieng::omens(W_, H_);
            const int sb = L.statusCells[0].y + L.statusCells[0].h, nt = L.navCells[0].y;
            if (st) sched_.invalidate(0, sb);
            if (bd) sched_.invalidate(sb, nt);
            { scanr::ScanScene za; za.rec = store_[cur_].scene().rec; scanr::ScanScene zb; zb.rec = nw.rec;  // design #1: STOP/REC lives on the nav bar now
              char ra[16], rb[16]; scanr::recLabel(za, ra, 16); scanr::recLabel(zb, rb, 16);
              if (__builtin_strcmp(ra, rb) || store_[cur_].scene().poi != nw.poi) sched_.invalidate(nt, H_); } }
        cur_ ^= 1; haveFront_ = true; forceFull_ = false;
    }
    StripScheduler::Slice nextSlice(int budget) {
        const bool was = sched_.pending(); const StripScheduler::Slice s = sched_.step(budget);
        if (was && s.done) { trans_.settled(); metrics_.frameDone(); }
        return s;
    }
    const PresentMetrics& metrics() const { return metrics_; }
private:
    OmenStore store_[2]; StripScheduler sched_; Transition trans_; PresentMetrics metrics_;
    int W_ = 0, H_ = 0, cur_ = 0; bool haveFront_ = false, forceFull_ = true;
};

}  // namespace omnpres
