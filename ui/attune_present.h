// attune_present.h -- the ATTUNE device adapter's pure core. Shared machinery
// from scanpres::; ATTUNE adds the AttuneStore (owned label/value strings), the
// status/body scene diff, and the AttuneEngine sequencer.
#pragma once
#include "attune_render.h"
#include "scan_present.h"
#include <cstdint>

namespace atnpres {

using atnr::AttuneScene;
using atnr::AttuneRow;
using scanpres::StripScheduler;
using scanpres::Transition;
using scanpres::PresentMetrics;

class AttuneStore {
public:
    static constexpr int MAXROWS = 4;      // == max rows per page the layout can show
    static constexpr int STRMAX  = 24;

    void begin(uint8_t theme, uint32_t now) {
        nr_ = 0; sc_ = AttuneScene{}; sc_.theme = theme; sc_.now = now; sc_.rows = rows_; sc_.nRows = 0;
    }
    void gps(const atnr::GnssFix& f) { sc_.gps = f; }
    void status(int nodes, bool hasCell, int batt, atnr::Rec rec) {
        sc_.nodes = nodes; sc_.hasCell = hasCell; sc_.battPct = batt; sc_.rec = rec;
    }
    void paging(int page, int nPages) { sc_.page = page; sc_.nPages = nPages; }
    bool addRow(const char* label, const char* value) {
        if (nr_ >= MAXROWS) return false;
        copy_(lBuf_[nr_], STRMAX, label); copy_(vBuf_[nr_], STRMAX, value);
        rows_[nr_] = AttuneRow{ lBuf_[nr_], vBuf_[nr_] };
        nr_++; sc_.nRows = nr_; return true;
    }
    const AttuneScene& scene() const { return sc_; }
    int rowCount() const { return nr_; }

private:
    static void copy_(char* d, int cap, const char* src){ int k=0; for(;src&&src[k]&&k<cap-1;k++) d[k]=src[k]; d[k]=0; }
    AttuneRow rows_[MAXROWS];
    char      lBuf_[MAXROWS][STRMAX], vBuf_[MAXROWS][STRMAX];
    int       nr_ = 0;
    AttuneScene sc_;
};

inline void sceneDirty(const AttuneScene& a, const AttuneScene& b, bool& status, bool& body) {
    char la[16], lb[16];
    const bool gpsSame = scanr::gpsLabel(a.gps, la, 16) == scanr::gpsLabel(b.gps, lb, 16) && !__builtin_strcmp(la, lb);
    char ra[16], rb[16];
    scanr::ScanScene sa; sa.rec = a.rec; scanr::ScanScene sb; sb.rec = b.rec;
    const bool recSame = scanr::recLabel(sa, ra, 16) == scanr::recLabel(sb, rb, 16) && !__builtin_strcmp(ra, rb);
    status = !gpsSame || !recSame || a.nodes != b.nodes || a.hasCell != b.hasCell || a.battPct != b.battPct;

    bool same = a.page == b.page && a.nPages == b.nPages && a.nRows == b.nRows;
    for (int i = 0; same && i < a.nRows; i++)
        same = !__builtin_strcmp(a.rows[i].label?a.rows[i].label:"", b.rows[i].label?b.rows[i].label:"")
               && !__builtin_strcmp(a.rows[i].value?a.rows[i].value:"", b.rows[i].value?b.rows[i].value:"");
    body = !same;
}

class AttuneEngine {
public:
    void reset(int W, int H) {
        W_ = W; H_ = H; sched_.reset(H); trans_.leave(); metrics_.clear();
        store_[0].begin(0,0); store_[1].begin(0,0); cur_ = 0; haveFront_ = false; forceFull_ = true;
    }
    void enter() { trans_.enter(); forceFull_ = true; sched_.reset(H_); }
    void leave() { trans_.leave(); sched_.reset(H_); }
    bool acceptsTouch() const { return trans_.acceptsTouch(); }
    bool pending()      const { return sched_.pending(); }
    AttuneStore&       back()  { return store_[cur_ ^ 1]; }
    const AttuneScene& scene() const { return store_[cur_].scene(); }
    void commit() {
        const AttuneScene& nw = store_[cur_ ^ 1].scene();
        if (forceFull_ || !haveFront_) sched_.invalidateAll();
        else { bool st=false,bd=false; sceneDirty(store_[cur_].scene(), nw, st, bd);
            const uieng::AttuneLayout L = uieng::attune(W_, H_);
            const int sb = L.statusCells[0].y + L.statusCells[0].h, nt = L.navCells[0].y;
            if (st) sched_.invalidate(0, sb);
            if (bd) sched_.invalidate(sb, nt);
            { scanr::ScanScene za; za.rec = store_[cur_].scene().rec; scanr::ScanScene zb; zb.rec = nw.rec;  // design #1: STOP/REC lives on the nav bar now
              char ra[16], rb[16]; scanr::recLabel(za, ra, 16); scanr::recLabel(zb, rb, 16);
              if (__builtin_strcmp(ra, rb)) sched_.invalidate(nt, H_); } }
        cur_ ^= 1; haveFront_ = true; forceFull_ = false;
    }
    StripScheduler::Slice nextSlice(int budget) {
        const bool was = sched_.pending(); const StripScheduler::Slice s = sched_.step(budget);
        if (was && s.done) { trans_.settled(); metrics_.frameDone(); }
        return s;
    }
    const PresentMetrics& metrics() const { return metrics_; }
private:
    AttuneStore store_[2]; StripScheduler sched_; Transition trans_; PresentMetrics metrics_;
    int W_ = 0, H_ = 0, cur_ = 0; bool haveFront_ = false, forceFull_ = true;
};

}  // namespace atnpres
