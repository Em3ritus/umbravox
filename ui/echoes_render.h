// echoes_render.h -- static ECHOES compositor + scene.
//
// ECHOES is probe requests: who is looking for what. The sought network is the
// finding (bright ink); the full station address is labeled local/universal.
// Neither address type proves device identity or prior membership. Regions come from
// uieng::echoes(W,H) (the shared LogLayout). Reuses subr:: chrome. Host +
// device compilable (no Arduino deps).
#pragma once
#include "scan_render.h"
#include "sub_render.h"
#include "layout_engine.h"
#include "palette.h"
#include "font5x7.h"
#include "canvas.h"
#include <cstdint>
#include <cstdio>

namespace echr {

using scanr::Sink;
using scanr::GnssFix;
using scanr::Rec;

struct EchoRow {
    const char* seeking;   // SSID being probed for
    const char* who;       // full station MAC
    bool        randomised;// locally-administered address
    uint32_t    agoS;
    int rssi=0;uint8_t channel=0;uint32_t count=0;bool wildcard=false;
    const char* link="";
};

struct EchoScene {
    GnssFix  gps;
    int      nodes = 0;
    bool     hasCell = false; int battPct = 0;
    ui::PoiStatus poi = ui::PoiStatus::Ready;
    bool pmuOk = false, charging = false;
    bool bridgeOn = false;
    Rec      rec = Rec::Off;
    const EchoRow* rows = nullptr; int nRows = 0;
    int      total = 0;        // probes recorded (>= nRows)
    int offset=0;uint32_t drops=0,evicted=0;bool available=true;
    int      clients = 0;      // distinct askers
    uint8_t  theme = 0;
    uint32_t now = 0;
};

inline void agoStr(char* out, int n, uint32_t s) {
    if (s < 60)        snprintf(out, n, "%uS", (unsigned)s);
    else if (s < 3600) snprintf(out, n, "%uM", (unsigned)(s / 60));
    else               snprintf(out, n, "%uH", (unsigned)(s / 3600));
}

inline void composeStrip(scanr::S& s, int W, int H, const EchoScene& sc) {
    using scanr::box; using scanr::txt; using scanr::centered;
    const uieng::LogLayout L = uieng::echoes(W, H);
    // shared design-1 chrome (header + nav, no tab lit) + STOP/REC record control
    scanr::drawChrome(s, W, H, L.body, {sc.gps, sc.nodes, sc.hasCell, sc.battPct, sc.rec, sc.theme, sc.bridgeOn, sc.pmuOk, sc.charging, sc.poi}, -1);

    txt(s, L.title.x, L.title.y + 2, "PROBES", 2, pal::BRIGHT);
    { char tb[20];
      if (sc.clients) snprintf(tb, sizeof tb, "%d MACS", sc.clients);
      else if (sc.total > sc.nRows) snprintf(tb, sizeof tb, "%d", sc.total);
      else tb[0] = 0;
      if (tb[0]) txt(s, L.title.x + L.title.w - Font::width(tb,1), L.title.y + 4, tb, 1, pal::DIM); }

    txt(s,L.header.x+3,L.header.y+2,"REQUESTED SSID / OBSERVED STATION",1,pal::EDGE_LIT);
    box(s,L.list.x,L.list.y,L.list.w,L.list.h,ui::Canvas::mix(pal::VOID,pal::PANEL,1,3));
    if(!sc.available || !sc.total){
        centered(s,L.list.x+L.list.w/2,L.list.y+10,sc.available?"NO PROBES HEARD":"PROBE CAPTURE UNAVAILABLE",1,pal::DIM);
    }
    for(int i=0;i<sc.nRows && i<L.nRows;i++){
        const auto& e=sc.rows[i];const int y=L.list.y+i*L.rowH+3,step=W>=400?15:11;
        const char* name=e.wildcard?"<ANY NETWORK>":e.seeking?e.seeking:"";
        txt(s,L.list.x+3,y,name,scanr::fitScale(name,L.list.w-6,W>=400?2:1),pal::BRIGHT);
        char line[96];snprintf(line,sizeof line,"%s / %s",e.who?e.who:"",e.randomised?"LOCAL MAC":"UNIVERSAL MAC");
        txt(s,L.list.x+3,y+step,line,1,pal::ICE);
        snprintf(line,sizeof line,"%d dBm CH%u / %luS / %lu REQ",e.rssi,e.channel,(unsigned long)e.agoS,(unsigned long)e.count);
        txt(s,L.list.x+3,y+2*step,line,scanr::fitScale(line,L.list.w-6,1),pal::TEXT);
        txt(s,L.list.x+3,y+3*step,e.link&&e.link[0]?e.link:"NO OBSERVED AP LINK",1,pal::DIM);
        if(i)box(s,L.list.x,y-3,L.list.w,1,pal::EDGE);
    }
    scanr::frame(s,L.prev.x,L.prev.y,44,44,pal::EDGE_LIT);centered(s,L.prev.x+22,L.prev.y+15,"<",2,pal::TEXT);
    scanr::frame(s,L.next.x,L.next.y,44,44,pal::EDGE_LIT);centered(s,L.next.x+22,L.next.y+15,">",2,pal::TEXT);
    char page[64];const int shown=std::min(sc.nRows,L.nRows);
    snprintf(page,sizeof page,"%d-%d / %d",sc.total?sc.offset+1:0,sc.offset+shown,sc.total);
    centered(s,L.page.x+L.page.w/2,L.page.y+6,page,1,pal::TEXT);
    snprintf(page,sizeof page,"QDROP %lu / EVICT %lu",(unsigned long)sc.drops,(unsigned long)sc.evicted);
    centered(s,L.page.x+L.page.w/2,L.page.y+20,page,scanr::fitScale(page,L.page.w,1),(sc.drops||sc.evicted)?pal::AMBER:pal::DIM);

}

class Compositor {
public:
    static constexpr int MAXW = 480, STRIP_H = 16, BUF_BYTES = MAXW * STRIP_H * 2;
    void renderRows(int W, int H, const EchoScene& sc, int y0, int y1, Sink& sink) {
        if (y0 < 0) y0 = 0; if (y1 > H) y1 = H;
        for (int sy = y0; sy < y1; sy += STRIP_H) {
            const int sh = (sy + STRIP_H > y1) ? (y1 - sy) : STRIP_H;
            scanr::S s{ buf_, W, sy, sh }; composeStrip(s, W, H, sc); sink.strip(0, sy, W, sh, buf_);
        }
    }
    void renderFull(int W, int H, const EchoScene& sc, Sink& sink) { renderRows(W, H, sc, 0, H, sink); }
    void renderRegion(int W, int H, const EchoScene& sc, uieng::Rect r, Sink& sink) {
        renderRows(W, H, sc, (r.y / STRIP_H) * STRIP_H, r.y + r.h, sink);
    }
private:
    uint16_t buf_[MAXW * STRIP_H];
};

}  // namespace echr
