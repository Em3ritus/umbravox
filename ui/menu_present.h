// menu_present.h -- the MENU (hub) device adapter's pure core.
//
// Mirrors the other engine screens. The screen-agnostic machinery
// (StripScheduler / Transition / TouchGate / PresentMetrics / suspend drain) is
// SHARED from scanpres:: rather than duplicated; MENU adds only its small
// MenuStore snapshot, the status/body scene diff, and the MenuEngine sequencer.
// The hub has no per-row owned storage -- its tiles are a fixed table in
// menu_render.h -- so the store is just the chrome + label mode. Host + device
// compilable (no Arduino deps).
#pragma once
#include "menu_render.h"
#include "scan_present.h"     // scanpres:: scheduler/transition/touch-gate/metrics
#include <cstdint>

namespace menpres {

using menr::MenuScene;

using scanpres::StripScheduler;
using scanpres::Transition;
using scanpres::TouchGate;
using scanpres::PresentMetrics;
using scanpres::serviceWhileSuspended;

// --- stable scene snapshot ----------------------------------------------------
class MenuStore {
public:
    void begin(uint8_t theme, uint32_t now) {
        sc_ = MenuScene{};
        sc_.theme = theme; sc_.now = now;
    }
    void gps(const menr::GnssFix& f) { sc_.gps = f; }
    void status(int nodes, bool hasCell, int batt, menr::Rec rec) {
        sc_.nodes = nodes; sc_.hasCell = hasCell; sc_.battPct = batt; sc_.rec = rec;
    }
    void labels(bool plain) { sc_.plain = plain; }

    const MenuScene& scene() const { return sc_; }

private:
    MenuScene sc_;
};

// --- scene diff: which display bands changed ---------------------------------
// status = top chrome; body = the tile grid, which only changes when the label
// mode flips (the tile set itself is fixed). Pure.
inline void sceneDirty(const MenuScene& a, const MenuScene& b, bool& status, bool& body) {
    char la[16], lb[16];
    const bool gpsSame = scanr::gpsLabel(a.gps, la, 16) == scanr::gpsLabel(b.gps, lb, 16)
                         && !__builtin_strcmp(la, lb);
    char ra[16], rb[16];
    scanr::ScanScene sa; sa.rec = a.rec; scanr::ScanScene sb; sb.rec = b.rec;
    const bool recSame = scanr::recLabel(sa, ra, 16) == scanr::recLabel(sb, rb, 16)
                         && !__builtin_strcmp(ra, rb);
    status = !gpsSame || !recSame || a.nodes != b.nodes
             || a.hasCell != b.hasCell || a.battPct != b.battPct;
    body = a.plain != b.plain;
}

// --- the adapter's pure sequencer (MenuScene twin of the other engines) -------
class MenuEngine {
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

    MenuStore&       back()  { return store_[cur_ ^ 1]; }
    const MenuScene& scene() const { return store_[cur_].scene(); }

    void commit() {
        const MenuScene& nw = store_[cur_ ^ 1].scene();
        if (forceFull_ || !haveFront_) {
            sched_.invalidateAll();
        } else {
            bool st = false, bd = false;
            sceneDirty(store_[cur_].scene(), nw, st, bd);
            const uieng::MenuLayout L = uieng::menu(W_, H_);
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
    const PresentMetrics& metrics() const { return metrics_; }
    void clearMetrics() { metrics_.clear(); }
    bool forcingFull() const { return forceFull_; }

private:
    MenuStore       store_[2];
    StripScheduler  sched_;
    Transition      trans_;
    PresentMetrics  metrics_;
    int  W_ = 0, H_ = 0, cur_ = 0;
    bool haveFront_ = false, forceFull_ = true;
};

}  // namespace menpres
