// detail_present.h -- the DETAIL device adapter's pure core. Shared machinery
// from scanpres::; DETAIL adds the DetailStore (owned strings + RSSI history),
// the status/body scene diff, and the DetailEngine sequencer. Entering DETAIL
// from a row is an enter() (full repaint); within it, a new sighting moves the
// body (rssi/history/age).
#pragma once
#include "detail_render.h"
#include "scan_present.h"
#include "model.h"
#include <cstdint>

namespace detpres {

using detr::DetailScene;
using scanpres::StripScheduler;
using scanpres::Transition;
using scanpres::PresentMetrics;

class DetailStore {
public:
    static constexpr int STRMAX = ui::SSID_MAX;   // 20
    static constexpr int HN     = ui::HIST_N;      // 28

    void begin(uint8_t theme, uint32_t now) {
        sc_ = DetailScene{}; sc_.theme = theme; sc_.now = now; sc_.hist = hist_; sc_.histN = 0;
        wire_();
    }
    void gps(const detr::GnssFix& f) { sc_.gps = f; }
    void status(int nodes, bool hasCell, int batt, detr::Rec rec) {
        sc_.nodes = nodes; sc_.hasCell = hasCell; sc_.battPct = batt; sc_.rec = rec;
    }
    // The device identity + verdict, all strings copied into owned storage.
    void device(bool valid, const char* name, const char* mac, const char* bandChan, uint8_t band,
                int rssi, const char* auth, bool authOpen,
                const char* threat, uint8_t conf, const char* why,
                const char* firstSeen, const char* lastSeen) {
        sc_.valid = valid; sc_.band = band; sc_.rssi = rssi; sc_.authOpen = authOpen; sc_.conf = conf;
        copy_(name_, STRMAX, name); copy_(mac_, 20, mac); copy_(bc_, 16, bandChan);
        copy_(auth_, 12, auth); copy_(threat_, STRMAX, threat); copy_(why_, STRMAX, why);
        copy_(first_, 8, firstSeen); copy_(last_, 8, lastSeen);
        wire_();
    }
    void history(const int8_t* h, int n) {
        if (n < 0) n = 0; if (n > HN) n = HN;
        for (int i = 0; i < n; i++) hist_[i] = h[i];
        sc_.histN = n; sc_.hist = hist_;
    }
    const DetailScene& scene() const { return sc_; }

private:
    void wire_() {
        sc_.name = name_; sc_.mac = mac_; sc_.bandChan = bc_; sc_.auth = auth_;
        sc_.threat = threat_; sc_.why = why_; sc_.firstSeen = first_; sc_.lastSeen = last_;
    }
    static void copy_(char* d, int cap, const char* src){ int k=0; for(;src&&src[k]&&k<cap-1;k++) d[k]=src[k]; d[k]=0; }
    char name_[STRMAX]={0}, mac_[20]={0}, bc_[16]={0}, auth_[12]={0},
         threat_[STRMAX]={0}, why_[STRMAX]={0}, first_[8]={0}, last_[8]={0};
    int8_t hist_[HN]={0};
    DetailScene sc_;
};

inline void sceneDirty(const DetailScene& a, const DetailScene& b, bool& status, bool& body) {
    char la[16], lb[16];
    const bool gpsSame = scanr::gpsLabel(a.gps, la, 16) == scanr::gpsLabel(b.gps, lb, 16) && !__builtin_strcmp(la, lb);
    char ra[16], rb[16];
    scanr::ScanScene sa; sa.rec = a.rec; scanr::ScanScene sb; sb.rec = b.rec;
    const bool recSame = scanr::recLabel(sa, ra, 16) == scanr::recLabel(sb, rb, 16) && !__builtin_strcmp(ra, rb);
    status = !gpsSame || !recSame || a.nodes != b.nodes || a.hasCell != b.hasCell || a.battPct != b.battPct;

    bool same = a.valid == b.valid && a.band == b.band && a.rssi == b.rssi && a.conf == b.conf
        && a.authOpen == b.authOpen && a.histN == b.histN
        && !__builtin_strcmp(a.name?a.name:"", b.name?b.name:"")
        && !__builtin_strcmp(a.mac?a.mac:"", b.mac?b.mac:"")
        && !__builtin_strcmp(a.threat?a.threat:"", b.threat?b.threat:"")
        && !__builtin_strcmp(a.lastSeen?a.lastSeen:"", b.lastSeen?b.lastSeen:"");
    for (int i = 0; same && i < a.histN; i++) same = a.hist[i] == b.hist[i];
    body = !same;
}

class DetailEngine {
public:
    void reset(int W, int H) {
        W_ = W; H_ = H; sched_.reset(H); trans_.leave(); metrics_.clear();
        store_[0].begin(0,0); store_[1].begin(0,0); cur_ = 0; haveFront_ = false; forceFull_ = true;
    }
    void enter() { trans_.enter(); forceFull_ = true; sched_.reset(H_); }
    void leave() { trans_.leave(); sched_.reset(H_); }
    bool acceptsTouch() const { return trans_.acceptsTouch(); }
    bool pending()      const { return sched_.pending(); }
    DetailStore&       back()  { return store_[cur_ ^ 1]; }
    const DetailScene& scene() const { return store_[cur_].scene(); }
    void commit() {
        const DetailScene& nw = store_[cur_ ^ 1].scene();
        if (forceFull_ || !haveFront_) sched_.invalidateAll();
        else { bool st=false,bd=false; sceneDirty(store_[cur_].scene(), nw, st, bd);
            const uieng::DetailLayout L = uieng::detail(W_, H_);
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
    DetailStore store_[2]; StripScheduler sched_; Transition trans_; PresentMetrics metrics_;
    int W_ = 0, H_ = 0, cur_ = 0; bool haveFront_ = false, forceFull_ = true;
};

}  // namespace detpres
