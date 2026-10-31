// echoes_present.h -- the ECHOES device adapter's pure core. Shared machinery
// from scanpres::; ECHOES adds the EchoStore (owned seeking/who strings), the
// status/body scene diff, and the EchoEngine sequencer.
#pragma once
#include "echoes_render.h"
#include "scan_present.h"
#include "model.h"
#include <cstdint>

namespace echpres {

using echr::EchoScene;
using echr::EchoRow;
using scanpres::StripScheduler;
using scanpres::Transition;
using scanpres::PresentMetrics;

class EchoStore {
public:
    static constexpr int MAXROWS = ui::MAX_ECHO_ROWS;   // 6
    static constexpr int STRMAX  = ui::AP_SSID_MAX;         // 20

    void begin(uint8_t theme, uint32_t now) {
        nr_ = 0; sc_ = EchoScene{}; sc_.theme = theme; sc_.now = now; sc_.rows = rows_; sc_.nRows = 0;
    }
    void gps(const echr::GnssFix& f) { sc_.gps = f; }
    void header(bool bridge, bool pmu, bool charging) { sc_.bridgeOn=bridge; sc_.pmuOk=pmu; sc_.charging=charging; }
    void poi(ui::PoiStatus value) { sc_.poi=value; }
    void status(int nodes, bool hasCell, int batt, echr::Rec rec) {
        sc_.nodes = nodes; sc_.hasCell = hasCell; sc_.battPct = batt; sc_.rec = rec;
    }
    void page(int offset,uint32_t drops,uint32_t evicted,bool available){sc_.offset=offset;sc_.drops=drops;sc_.evicted=evicted;sc_.available=available;}
    void totals(int total, int clients) { sc_.total = total; sc_.clients = clients; }
    bool addRow(const char* seeking, const char* who, bool randomised, uint32_t agoS,int rssi=0,uint8_t channel=0,uint32_t count=0,bool wildcard=false,const char* link="") {
        if (nr_ >= MAXROWS) return false;
        copy_(sBuf_[nr_], STRMAX, seeking); copy_(wBuf_[nr_], 18, who);copy_(linkBuf_[nr_],40,link);
        rows_[nr_] = EchoRow{ sBuf_[nr_], wBuf_[nr_], randomised, agoS,rssi,channel,count,wildcard,linkBuf_[nr_] };
        nr_++; sc_.nRows = nr_; return true;
    }
    const EchoScene& scene() const { return sc_; }
    int rowCount() const { return nr_; }

private:
    static void copy_(char* d, int cap, const char* src){ int k=0; for(;src&&src[k]&&k<cap-1;k++) d[k]=src[k]; d[k]=0; }
    EchoRow rows_[MAXROWS];
    char    sBuf_[MAXROWS][STRMAX], wBuf_[MAXROWS][18],linkBuf_[MAXROWS][40];
    int     nr_ = 0;
    EchoScene sc_;
};

inline void sceneDirty(const EchoScene& a, const EchoScene& b, bool& status, bool& body) {
    char la[16], lb[16];
    const bool gpsSame = scanr::gpsLabel(a.gps, la, 16) == scanr::gpsLabel(b.gps, lb, 16) && !__builtin_strcmp(la, lb);
    char ra[16], rb[16];
    scanr::ScanScene sa; sa.rec = a.rec; scanr::ScanScene sb; sb.rec = b.rec;
    const bool recSame = scanr::recLabel(sa, ra, 16) == scanr::recLabel(sb, rb, 16) && !__builtin_strcmp(ra, rb);
    status = !gpsSame || !recSame || a.nodes != b.nodes || a.hasCell != b.hasCell || a.battPct != b.battPct || a.bridgeOn != b.bridgeOn || a.pmuOk != b.pmuOk || a.charging != b.charging;

    bool same = a.offset==b.offset && a.drops==b.drops && a.evicted==b.evicted && a.available==b.available && a.total == b.total && a.clients == b.clients && a.nRows == b.nRows;
    for (int i = 0; same && i < a.nRows; i++)
        same = a.rows[i].rssi==b.rows[i].rssi && a.rows[i].channel==b.rows[i].channel && a.rows[i].count==b.rows[i].count && a.rows[i].wildcard==b.rows[i].wildcard && !__builtin_strcmp(a.rows[i].link,b.rows[i].link) && a.rows[i].randomised == b.rows[i].randomised && a.rows[i].agoS == b.rows[i].agoS
               && !__builtin_strcmp(a.rows[i].seeking?a.rows[i].seeking:"", b.rows[i].seeking?b.rows[i].seeking:"")
               && !__builtin_strcmp(a.rows[i].who?a.rows[i].who:"", b.rows[i].who?b.rows[i].who:"");
    body = !same;
}

class EchoEngine {
public:
    void reset(int W, int H) {
        W_ = W; H_ = H; sched_.reset(H); trans_.leave(); metrics_.clear();
        store_[0].begin(0,0); store_[1].begin(0,0); cur_ = 0; haveFront_ = false; forceFull_ = true;
    }
    void enter() { trans_.enter(); forceFull_ = true; sched_.reset(H_); }
    void leave() { trans_.leave(); sched_.reset(H_); }
    bool acceptsTouch() const { return trans_.acceptsTouch(); }
    bool pending()      const { return sched_.pending(); }
    EchoStore&       back()  { return store_[cur_ ^ 1]; }
    const EchoScene& scene() const { return store_[cur_].scene(); }
    void commit() {
        const EchoScene& nw = store_[cur_ ^ 1].scene();
        if (forceFull_ || !haveFront_) sched_.invalidateAll();
        else { bool st=false,bd=false; sceneDirty(store_[cur_].scene(), nw, st, bd);
            const uieng::LogLayout L = uieng::echoes(W_, H_);
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
    EchoStore store_[2]; StripScheduler sched_; Transition trans_; PresentMetrics metrics_;
    int W_ = 0, H_ = 0, cur_ = 0; bool haveFront_ = false, forceFull_ = true;
};

}  // namespace echpres
