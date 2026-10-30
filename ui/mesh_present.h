// mesh_present.h -- the MESH device adapter's pure core.
//
// Mirrors the other engine screens; the screen-agnostic machinery is SHARED
// from scanpres:: (no second copy). MESH adds the MeshStore snapshot (owned
// node rows), the status/body scene diff, and the MeshEngine sequencer.
#pragma once
#include "mesh_render.h"
#include "scan_present.h"
#include "model.h"
#include <cstdint>

namespace mshpres {

using mshr::MeshScene;
using mshr::MeshNode;
using scanpres::StripScheduler;
using scanpres::Transition;
using scanpres::TouchGate;
using scanpres::PresentMetrics;
using scanpres::serviceWhileSuspended;

class MeshStore {
public:
    static constexpr int MAXROWS = ui::MAX_NODE_ROWS;   // 16

    void begin(uint8_t theme, uint32_t now) {
        nr_ = 0; sc_ = MeshScene{};
        sc_.theme = theme; sc_.now = now; sc_.rows = rows_; sc_.nRows = 0;
    }
    void gps(const mshr::GnssFix& f) { sc_.gps = f; }
    void header(bool bridge, bool pmu, bool charging) { sc_.bridgeOn=bridge; sc_.pmuOk=pmu; sc_.charging=charging; }
    void poi(ui::PoiStatus value) { sc_.poi=value; }
    void status(int nodes, bool hasCell, int batt, mshr::Rec rec) {
        sc_.nodes = nodes; sc_.hasCell = hasCell; sc_.battPct = batt; sc_.rec = rec;
    }
    void header(int coords, bool camping, int chan, int lossPct, int netId, int enrollSecs) {
        sc_.meshCoords = coords; sc_.coreCamping = camping; sc_.coreChan = chan;
        sc_.meshLossPct = lossPct; sc_.netId = netId; sc_.enrollSecs = enrollSecs;
    }
    bool addNode(const uint8_t id[3], uint32_t ageS, uint8_t lossPct, bool live) {
        if (nr_ >= MAXROWS) return false;
        rows_[nr_] = MeshNode{ {id[0], id[1], id[2]}, ageS, lossPct, live };
        nr_++; sc_.nRows = nr_; return true;
    }
    const MeshScene& scene() const { return sc_; }
    int rowCount() const { return nr_; }

private:
    MeshNode rows_[MAXROWS];
    int      nr_ = 0;
    MeshScene sc_;
};

inline void sceneDirty(const MeshScene& a, const MeshScene& b, bool& status, bool& body) {
    char la[16], lb[16];
    const bool gpsSame = scanr::gpsLabel(a.gps, la, 16) == scanr::gpsLabel(b.gps, lb, 16) && !__builtin_strcmp(la, lb);
    char ra[16], rb[16];
    scanr::ScanScene sa; sa.rec = a.rec; scanr::ScanScene sb; sb.rec = b.rec;
    const bool recSame = scanr::recLabel(sa, ra, 16) == scanr::recLabel(sb, rb, 16) && !__builtin_strcmp(ra, rb);
    status = !gpsSame || !recSame || a.nodes != b.nodes || a.hasCell != b.hasCell || a.battPct != b.battPct || a.bridgeOn != b.bridgeOn || a.pmuOk != b.pmuOk || a.charging != b.charging;

    bool same = a.meshCoords == b.meshCoords && a.coreCamping == b.coreCamping && a.coreChan == b.coreChan
                && a.meshLossPct == b.meshLossPct && a.netId == b.netId && a.enrollSecs == b.enrollSecs
                && a.nRows == b.nRows;
    for (int i = 0; same && i < a.nRows; i++)
        same = a.rows[i].ageS == b.rows[i].ageS && a.rows[i].lossPct == b.rows[i].lossPct
               && a.rows[i].live == b.rows[i].live
               && a.rows[i].id[1] == b.rows[i].id[1] && a.rows[i].id[2] == b.rows[i].id[2];
    body = !same;
}

class MeshEngine {
public:
    void reset(int W, int H) {
        W_ = W; H_ = H; sched_.reset(H); trans_.leave(); metrics_.clear();
        store_[0].begin(0, 0); store_[1].begin(0, 0); cur_ = 0; haveFront_ = false; forceFull_ = true;
    }
    void enter()  { trans_.enter(); forceFull_ = true; sched_.reset(H_); }
    void leave()  { trans_.leave(); sched_.reset(H_); }
    bool acceptsTouch() const { return trans_.acceptsTouch(); }
    bool pending()      const { return sched_.pending(); }
    MeshStore&       back()  { return store_[cur_ ^ 1]; }
    const MeshScene& scene() const { return store_[cur_].scene(); }
    void commit() {
        const MeshScene& nw = store_[cur_ ^ 1].scene();
        if (forceFull_ || !haveFront_) sched_.invalidateAll();
        else {
            bool st = false, bd = false; sceneDirty(store_[cur_].scene(), nw, st, bd);
            const uieng::MeshLayout L = uieng::mesh(W_, H_);
            const int statusBottom = L.statusCells[0].y + L.statusCells[0].h, navTop = L.navCells[0].y;
            if (st) sched_.invalidate(0, statusBottom);
            if (bd) sched_.invalidate(statusBottom, navTop);
            { scanr::ScanScene za; za.rec = store_[cur_].scene().rec; scanr::ScanScene zb; zb.rec = nw.rec;  // design #1: STOP/REC lives on the nav bar now
              char ra[16], rb[16]; scanr::recLabel(za, ra, 16); scanr::recLabel(zb, rb, 16);
              if (__builtin_strcmp(ra, rb) || store_[cur_].scene().poi != nw.poi) sched_.invalidate(navTop, H_); }
        }
        cur_ ^= 1; haveFront_ = true; forceFull_ = false;
    }
    StripScheduler::Slice nextSlice(int budget) {
        const bool was = sched_.pending(); const StripScheduler::Slice s = sched_.step(budget);
        if (was && s.done) { trans_.settled(); metrics_.frameDone(); }
        return s;
    }
    const PresentMetrics& metrics() const { return metrics_; }
private:
    MeshStore store_[2]; StripScheduler sched_; Transition trans_; PresentMetrics metrics_;
    int W_ = 0, H_ = 0, cur_ = 0; bool haveFront_ = false, forceFull_ = true;
};

}  // namespace mshpres
