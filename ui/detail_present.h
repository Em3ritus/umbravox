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
    static constexpr int STRMAX = ui::AP_SSID_MAX;   // 33
    static constexpr int HN     = ui::HIST_N;      // 28

    void begin(uint8_t theme, uint32_t now) {
        sc_ = DetailScene{}; sc_.theme = theme; sc_.now = now; sc_.hist = hist_; sc_.histN = 0;
        cipher_[0]=0; for(auto& f:facts_)f[0]=0;for(auto& row:clientLines_)for(auto& line:row)line[0]=0; wire_();
    }
    void gps(const detr::GnssFix& f) { sc_.gps = f; }
    void storage(ui::StorageState sd){sc_.sd=sd;}
    void header(bool bridge, bool pmu, bool charging) { sc_.bridgeOn=bridge; sc_.pmuOk=pmu; sc_.charging=charging; }
    void poi(ui::PoiStatus value) { sc_.poi=value; }
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
        copy_(auth_, sizeof auth_, auth); copy_(threat_, STRMAX, threat); copy_(why_, STRMAX, why);
        copy_(first_, 8, firstSeen); copy_(last_, 8, lastSeen);
        wire_();
    }
    void page(uint8_t p){sc_.page=p%3;}
    void clients(const ui::ClientList& v){sc_.clients=v;}
    void clientLine(int row,int line,const char* text){if(row>=0 && row<ui::CLIENT_ROWS && line>=0 && line<3)copy_(clientLines_[row][line],80,text);}
    void fact(int i,const char* text){if(i>=0 && i<7)copy_(facts_[i],sizeof facts_[i],text);}
    void cipher(const char* value) { copy_(cipher_,sizeof cipher_,value); }
    void location(bool located,int32_t lat7,int32_t lon7,int best) {
        sc_.located=located;sc_.lat7=lat7;sc_.lon7=lon7;sc_.bestRssi=best;
        // Format once per snapshot, not floating point conversion per strip.
        const int64_t lat=lat7<0?-(int64_t)lat7:lat7, lon=lon7<0?-(int64_t)lon7:lon7;
        snprintf(coordinates_,sizeof coordinates_,"%s%ld.%07ld, %s%ld.%07ld",
            lat7<0?"-":"",(long)(lat/10000000),(long)(lat%10000000),
            lon7<0?"-":"",(long)(lon/10000000),(long)(lon%10000000));
    }
    void history(const int8_t* h, int n) {
        if (n < 0) n = 0; if (n > HN) n = HN;
        for (int i = 0; i < n; i++) hist_[i] = h[i];
        sc_.histN = n; sc_.hist = hist_;
    }
    const DetailScene& scene() const { return sc_; }

private:
    void wire_() {
        for(int i=0;i<ui::CLIENT_ROWS;i++)for(int j=0;j<3;j++)sc_.clientLines[i][j]=clientLines_[i][j];
        for(int i=0;i<7;i++)sc_.facts[i]=facts_[i];
        sc_.coordinates = coordinates_; sc_.name = name_; sc_.mac = mac_; sc_.bandChan = bc_; sc_.auth = auth_; sc_.cipher = cipher_;
        sc_.threat = threat_; sc_.why = why_; sc_.firstSeen = first_; sc_.lastSeen = last_;
    }
    static void copy_(char* d, int cap, const char* src){ int k=0; for(;src&&src[k]&&k<cap-1;k++) d[k]=src[k]; d[k]=0; }
    char clientLines_[ui::CLIENT_ROWS][3][80]{};
    char facts_[7][96]{};
    char cipher_[24]={0};
    char coordinates_[40]={0};
    char name_[STRMAX]={0}, mac_[20]={0}, bc_[16]={0}, auth_[112]={0},
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
    status = !gpsSame || !recSame || a.nodes != b.nodes || a.hasCell != b.hasCell || a.battPct != b.battPct || a.bridgeOn != b.bridgeOn || a.pmuOk != b.pmuOk || a.charging != b.charging || a.sd != b.sd;

    bool same = a.page==b.page && a.located==b.located && a.lat7==b.lat7 && a.lon7==b.lon7 && a.bestRssi==b.bestRssi
        && !__builtin_strcmp(a.cipher,b.cipher) && !__builtin_strcmp(a.auth,b.auth) && !__builtin_strcmp(a.bandChan,b.bandChan)
        && !__builtin_strcmp(a.why,b.why) && !__builtin_strcmp(a.firstSeen,b.firstSeen)
        && a.valid == b.valid && a.band == b.band && a.rssi == b.rssi && a.conf == b.conf
        && a.authOpen == b.authOpen && a.histN == b.histN
        && !__builtin_strcmp(a.name?a.name:"", b.name?b.name:"")
        && !__builtin_strcmp(a.mac?a.mac:"", b.mac?b.mac:"")
        && !__builtin_strcmp(a.threat?a.threat:"", b.threat?b.threat:"")
        && !__builtin_strcmp(a.lastSeen?a.lastSeen:"", b.lastSeen?b.lastSeen:"");
    for(int i=0;same && i<7;i++)same=!__builtin_strcmp(a.facts[i],b.facts[i]);
    for (int i = 0; same && i < a.histN; i++) same = a.hist[i] == b.hist[i];
    same=same && a.clients.available==b.clients.available && a.clients.total==b.clients.total && a.clients.offset==b.clients.offset && a.clients.count==b.clients.count && a.clients.drops==b.clients.drops && a.clients.evictions==b.clients.evictions;
    for(int i=0;same && i<ui::CLIENT_ROWS;i++)for(int j=0;same && j<3;j++)same=!__builtin_strcmp(a.clientLines[i][j],b.clientLines[i][j]);
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
    DetailStore store_[2]; StripScheduler sched_; Transition trans_; PresentMetrics metrics_;
    int W_ = 0, H_ = 0, cur_ = 0; bool haveFront_ = false, forceFull_ = true;
};

}  // namespace detpres
