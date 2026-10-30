// aspect_present.h -- the ASPECT device adapter's pure core. Shared machinery
// from scanpres::; ASPECT adds the AspectStore (owned name strings), the
// status/body scene diff, and the AspectEngine sequencer. A theme switch is a
// themeChanged()/enter() (full repaint), as the palette and preview both move.
#pragma once
#include "aspect_render.h"
#include "scan_present.h"
#include <cstdint>

namespace asppres {

using aspr::AspectScene;
using scanpres::StripScheduler;
using scanpres::Transition;
using scanpres::PresentMetrics;

class AspectStore {
public:
    static constexpr int STRMAX = 20;

    void begin(uint8_t theme, uint32_t now) { sc_ = AspectScene{}; sc_.theme = theme; sc_.now = now; wire_(); }
    void gps(const aspr::GnssFix& f) { sc_.gps = f; }
    void header(bool bridge, bool pmu, bool charging) { sc_.bridgeOn=bridge; sc_.pmuOk=pmu; sc_.charging=charging; }
    void poi(ui::PoiStatus value) { sc_.poi=value; }
    void status(int nodes, bool hasCell, int batt, aspr::Rec rec) {
        sc_.nodes = nodes; sc_.hasCell = hasCell; sc_.battPct = batt; sc_.rec = rec;
    }
    void appearance(const char* themeName, const char* sweepName, const char* alertName,
                    bool alertLive, int packsSkipped) {
        copy_(tBuf_, STRMAX, themeName); copy_(sBuf_, STRMAX, sweepName); copy_(aBuf_, STRMAX, alertName);
        sc_.alertLive = alertLive; sc_.packsSkipped = packsSkipped; wire_();
    }
    const AspectScene& scene() const { return sc_; }

private:
    void wire_() { sc_.themeName = tBuf_; sc_.sweepName = sBuf_; sc_.alertName = aBuf_; }
    static void copy_(char* d, int cap, const char* src){ int k=0; for(;src&&src[k]&&k<cap-1;k++) d[k]=src[k]; d[k]=0; }
    char tBuf_[STRMAX] = {0}, sBuf_[STRMAX] = {0}, aBuf_[STRMAX] = {0};
    AspectScene sc_;
};

inline void sceneDirty(const AspectScene& a, const AspectScene& b, bool& status, bool& body) {
    char la[16], lb[16];
    const bool gpsSame = scanr::gpsLabel(a.gps, la, 16) == scanr::gpsLabel(b.gps, lb, 16) && !__builtin_strcmp(la, lb);
    char ra[16], rb[16];
    scanr::ScanScene sa; sa.rec = a.rec; scanr::ScanScene sb; sb.rec = b.rec;
    const bool recSame = scanr::recLabel(sa, ra, 16) == scanr::recLabel(sb, rb, 16) && !__builtin_strcmp(ra, rb);
    status = !gpsSame || !recSame || a.nodes != b.nodes || a.hasCell != b.hasCell || a.battPct != b.battPct || a.bridgeOn != b.bridgeOn || a.pmuOk != b.pmuOk || a.charging != b.charging;

    const bool same = a.theme == b.theme && a.alertLive == b.alertLive && a.packsSkipped == b.packsSkipped
        && !__builtin_strcmp(a.themeName?a.themeName:"", b.themeName?b.themeName:"")
        && !__builtin_strcmp(a.sweepName?a.sweepName:"", b.sweepName?b.sweepName:"")
        && !__builtin_strcmp(a.alertName?a.alertName:"", b.alertName?b.alertName:"");
    body = !same;
}

class AspectEngine {
public:
    void reset(int W, int H) {
        W_ = W; H_ = H; sched_.reset(H); trans_.leave(); metrics_.clear();
        store_[0].begin(0,0); store_[1].begin(0,0); cur_ = 0; haveFront_ = false; forceFull_ = true;
    }
    void enter()        { trans_.enter(); forceFull_ = true; sched_.reset(H_); }
    void themeChanged() { trans_.enter(); forceFull_ = true; sched_.reset(H_); }
    void leave()        { trans_.leave(); sched_.reset(H_); }
    bool acceptsTouch() const { return trans_.acceptsTouch(); }
    bool pending()      const { return sched_.pending(); }
    AspectStore&       back()  { return store_[cur_ ^ 1]; }
    const AspectScene& scene() const { return store_[cur_].scene(); }
    void commit() {
        const AspectScene& nw = store_[cur_ ^ 1].scene();
        if (forceFull_ || !haveFront_) sched_.invalidateAll();
        else { bool st=false,bd=false; sceneDirty(store_[cur_].scene(), nw, st, bd);
            const uieng::AspectLayout L = uieng::aspect(W_, H_);
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
    AspectStore store_[2]; StripScheduler sched_; Transition trans_; PresentMetrics metrics_;
    int W_ = 0, H_ = 0, cur_ = 0; bool haveFront_ = false, forceFull_ = true;
};

}  // namespace asppres
