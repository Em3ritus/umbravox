// detail_render.h -- static DETAIL (one device) compositor + scene.
//
// Everything known about one device: identity, signature verdict, an RSSI
// history chart, and first/last-seen. RSSI history shows changes in observed
// signal strength; it does not establish direction or transmitter distance.
// Regions come from uieng::detail(W,H); the caller supplies the identity/
// signature strings pre-formatted and the raw RSSI history for the chart.
// Reuses subr:: chrome + domr::bandColor. Host + device compilable.
#pragma once
#include "scan_render.h"
#include "sub_render.h"
#include "domain_render.h"   // domr::bandColor (shared legend)
#include "layout_engine.h"
#include "palette.h"
#include "font5x7.h"
#include "canvas.h"
#include "model.h"           // ui::HIST_N (chart sample cap)
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace detr {

using scanr::Sink;
using scanr::GnssFix;
using scanr::Rec;

struct DetailScene {
    GnssFix  gps;
    int      nodes = 0;
    bool     hasCell = false; int battPct = 0;
    ui::PoiStatus poi = ui::PoiStatus::Ready;
    bool pmuOk = false, charging = false;
    ui::StorageState sd=ui::StorageState::Missing;
    bool bridgeOn = false;
    Rec      rec = Rec::Off;
    // the device (invalid -> the selection is gone; render "GONE")
    uint8_t page=0;
    ui::ClientList clients;
    const char* clientLines[ui::CLIENT_ROWS][3]={{"","",""},{"","",""}};
    const char* facts[7]={"","","","","","",""};
    bool     valid = false;
    bool located = false; int32_t lat7 = 0, lon7 = 0; int bestRssi = 0;
    const char* coordinates = "";
    const char* name = "";       // SSID, or "" for hidden
    const char* mac = "";        // "AA:BB:CC:DD:EE:FF"
    const char* bandChan = "";   // "2.4GHZ CH6" / "BLE"
    uint8_t  band = 0;           // for the chart ink
    int      rssi = 0;           // dBm
    const char* auth = "";       // "WPA2" / "OPEN" / "" (BLE)
    const char* cipher = ""; // Observed pairwise ciphers; not group cipher or negotiated security.
    bool     authOpen = false;   // open network -> amber
    const char* threat = "";     // threat name, "" if none
    uint8_t  conf = 0;           // 0 LOW 1 MED 2 HIGH
    const char* why = "";        // signature reason
    const char* firstSeen = "";  // "3H"
    const char* lastSeen = "";   // "45S"
    const int8_t* hist = nullptr; int histN = 0;   // RSSI samples (oldest..newest)
    uint8_t  theme = 0;
    uint32_t now = 0;
};

inline uint16_t confInk(uint8_t c) { return c >= 2 ? pal::RED : c == 1 ? pal::AMBER : pal::WITCH; }

// RSSI -> chart y within [top,bot] px rows, clamped to the -30..-95 dBm band.
inline int rssiToY(int rssi, int top, int h) {
    const int RT = -30, RB = -95;
    if (rssi > RT) rssi = RT; if (rssi < RB) rssi = RB;
    return top + h - 1 - (rssi - RB) * (h - 1) / (RT - RB);
}

inline void composeStrip(scanr::S& s, int W, int H, const DetailScene& sc) {
    using scanr::box; using scanr::txt; using scanr::frame; using scanr::centered;
    const uieng::DetailLayout L = uieng::detail(W, H);
    // shared design-1 chrome (header + nav, no tab lit) + STOP/REC record control
    scanr::drawChrome(s, W, H, L.body, {sc.gps, sc.nodes, sc.hasCell, sc.battPct, sc.rec, sc.theme, sc.bridgeOn, sc.pmuOk, sc.charging, sc.poi, sc.sd}, -1);

    // BACK button (always present)
    box(s, L.back.x, L.back.y + 1, 44, 42, ui::Canvas::mix(pal::PANEL, pal::VOID, 1, 3));
    frame(s, L.back.x, L.back.y + 1, 44, 42, pal::EDGE_LIT);
    centered(s, L.back.x + 22, L.back.y + 15, "<", 2, pal::WITCH_HI);

    if (!sc.valid) {
        centered(s, L.body.x + L.body.w / 2, L.body.y + L.body.h / 2 - 3, "GONE", 1, pal::DIM);
        return;
    }

    frame(s,L.more.x,L.more.y,44,44,pal::EDGE_LIT);
    centered(s,L.more.x+22,L.more.y+12,"MORE",1,pal::WITCH_HI);
    char pageLabel[8];snprintf(pageLabel,sizeof pageLabel,"%u/%u",unsigned(sc.page+1),sc.band==ui::BAND_BLE?2u:3u);
    centered(s,L.more.x+22,L.more.y+26,pageLabel,1,pal::TEXT);
    const uint16_t bc = domr::bandColor(sc.band);
    // Full 32-byte printable SSID: 2x when it fits, otherwise 1x, never truncated.
    const uieng::Rect hd = L.header;
    const char* name=(sc.name && sc.name[0])?sc.name:"<HIDDEN>";
    const int ns=scanr::fitScale(name,hd.w-8,2);
    txt(s,hd.x+4,hd.y+2,name,ns,pal::BRIGHT);
    char address[32]; snprintf(address,sizeof address,"%s %s",sc.band==ui::BAND_BLE?"ADDR":"BSSID",sc.mac?sc.mac:"");
    txt(s,hd.x+4,hd.y+20,address,1,pal::TEXT);
    txt(s,hd.x+4,hd.y+33,sc.bandChan?sc.bandChan:"",1,bc);
    char rb[40];snprintf(rb,sizeof rb,"%d dBm",sc.rssi);
    txt(s,hd.x+hd.w-4-Font::width(rb,1),hd.y+33,rb,1,pal::BRIGHT);

    if(sc.page==2 && sc.band!=ui::BAND_BLE){
        const auto c=uieng::detailClients(W,H);const auto& list=sc.clients;
        txt(s,c.heading.x+3,c.heading.y+2,"OBSERVED CLIENTS / LAST 5 MIN",1,pal::DIM);
        if(!list.available || !list.total){
            txt(s,c.rows[0].x+3,c.rows[0].y+3,list.available?"NONE OBSERVED ON THIS RADIO":"CLIENT CAPTURE UNAVAILABLE",1,pal::TEXT);
            txt(s,c.rows[0].x+3,c.rows[0].y+14,"Not a live association count.",1,pal::DIM);
        }
        for(int i=0;i<list.count && i<c.nRows;i++){
            const auto r=c.rows[i];const int step=W>=400?16:10;
            frame(s,r.x,r.y,r.w,r.h,pal::EDGE);
            for(int j=0;j<3;j++){
                char clipped[80];const int maxChars=std::min(79,(r.w-6)/6);
                snprintf(clipped,sizeof clipped,"%.*s",maxChars,sc.clientLines[i][j]);
                if(int(strlen(sc.clientLines[i][j]))>maxChars && maxChars>=3)memcpy(clipped+maxChars-3,"...",3);
                const char* line=clipped;
                txt(s,r.x+3,r.y+2+j*step,line,scanr::fitScale(line,r.w-6,W>=400?2:1),j==0?pal::BRIGHT:pal::TEXT);
            }
        }
        frame(s,c.prev.x,c.prev.y,c.prev.w,c.prev.h,pal::EDGE_LIT);centered(s,c.prev.x+22,c.prev.y+15,"<",2,pal::TEXT);
        frame(s,c.next.x,c.next.y,c.next.w,c.next.h,pal::EDGE_LIT);centered(s,c.next.x+22,c.next.y+15,">",2,pal::TEXT);
        char summary[80];snprintf(summary,sizeof summary,"%u-%u / %u OBSERVED",list.total?list.offset+1:0,list.offset+list.count,list.total);
        centered(s,c.page.x+c.page.w/2,c.page.y+6,summary,1,pal::TEXT);
        snprintf(summary,sizeof summary,"QDROP %lu / EVICT %lu",(unsigned long)list.drops,(unsigned long)list.evictions);
        centered(s,c.page.x+c.page.w/2,c.page.y+20,summary,scanr::fitScale(summary,c.page.w,1),(list.drops||list.evictions)?pal::AMBER:pal::DIM);
        return;
    }
    const auto st=L.stats;
    const int scale=W>=400?2:1, step=W>=400?18:10;
    const int x=st.x+4, y=st.y+3;
    if(sc.page){
        for(int row=0;row<7;row++){
            const char* value=sc.facts[row]?sc.facts[row]:"";
            char line[80];const int limit=std::max(0,std::min(79,(st.w-8)/6));
            snprintf(line,sizeof line,"%.*s",limit,value);
            if((int)strlen(value)>limit && limit>=3){line[limit-3]=line[limit-2]=line[limit-1]='.';}
            txt(s,x,y+row*step,line,scanr::fitScale(line,st.w-8,scale),row==0?pal::BRIGHT:pal::TEXT);
        }
    }else{
    // Separate observed authentication and pairwise cipher capabilities.
    // Fit each complete line independently; do not clip a mixed-mode network.
    char sec[128];
    for(int row=0;row<2;++row) {
        const char* value=row?sc.cipher:sc.auth;
        if(sc.band==ui::BAND_BLE) value="N/A (WIFI ONLY)";
        snprintf(sec,sizeof sec,"%s %s",row?"CIPHER":"AUTH",value&&value[0]?value:"NOT REPORTED");
        txt(s,x,y+row*step,sec,scanr::fitScale(sec,st.w-8,scale),sc.authOpen?pal::AMBER:pal::TEXT);
    }
    char info[96];
    if(sc.located) {
        snprintf(info,sizeof info,"HEARD HERE / BEST %d dBm",sc.bestRssi);
        txt(s,x,y+2*step,info,scanr::fitScale(info,st.w-8,scale),pal::DIM);
        txt(s,x,y+3*step,sc.coordinates,scanr::fitScale(sc.coordinates,st.w-8,scale),pal::TEXT);
    } else {
        txt(s,x,y+2*step,"NO RECORDED LOCATION",scale,pal::DIM);
        txt(s,x,y+3*step,"GPS fix needed when observed.",scanr::fitScale("GPS fix needed when observed.",st.w-8,scale),pal::DIM);
    }
    snprintf(info,sizeof info,"FIRST %s AGO / LAST %s AGO",sc.firstSeen?sc.firstSeen:"?",sc.lastSeen?sc.lastSeen:"?");
    txt(s,x,y+4*step,info,scanr::fitScale(info,st.w-8,scale),pal::TEXT);
    if(sc.threat && sc.threat[0]) {
        snprintf(info,sizeof info,"%s / %s",sc.threat,sc.conf>=2?"HIGH":sc.conf==1?"MED":"LOW");
        txt(s,x,y+5*step,info,scanr::fitScale(info,st.w-8,scale),confInk(sc.conf));
        txt(s,x,y+6*step,sc.why?sc.why:"",scanr::fitScale(sc.why?sc.why:"",st.w-8,scale),pal::DIM);
    } else txt(s,x,y+5*step,"NO SIGNATURE MATCH",scale,pal::DIM);

    }
    // RSSI history chart: filled bars, reference lines at -50 and -75
    const uieng::Rect ch = L.spark;
    box(s, ch.x, ch.y, ch.w, ch.h, ui::Canvas::mix(pal::VOID, pal::PANEL, 1, 2));
    frame(s, ch.x, ch.y, ch.w, ch.h, pal::EDGE);
    const int top = ch.y + 2, hgt = ch.h - 4;
    for (int k = 0; k < 2; k++) { const int ry = rssiToY(k ? -75 : -50, top, hgt);
        for (int x = ch.x + 2; x < ch.x + ch.w - 2; x += 3) scanr::pt(s, x, ry, pal::EDGE); }
    if (sc.histN == 0 || !sc.hist) {
        centered(s, ch.x + ch.w / 2, ch.y + ch.h / 2 - 3, "NO HISTORY", 1, pal::DIM);
    } else {
        const int span = ch.w - 6, step = (sc.histN > 1) ? span / (sc.histN - 1 > 0 ? sc.histN : 1) : span;
        const uint16_t fill = ui::Canvas::mix(pal::VOID, bc, 1, 3);
        for (int i = 0; i < sc.histN && i < ui::HIST_N; i++) {
            const int x = ch.x + 3 + (step ? i * step : 0);
            const int y = rssiToY(sc.hist[i], top, hgt);
            box(s, x, y, 1, (top + hgt - 1) - y, fill);
            scanr::pt(s, x, y, bc);
            if (i == sc.histN - 1) { scanr::pt(s, x, y, pal::BRIGHT); scanr::pt(s, x, y - 1, pal::BRIGHT); }
        }
    }

}

class Compositor {
public:
    static constexpr int MAXW = 480, STRIP_H = 16, BUF_BYTES = MAXW * STRIP_H * 2;
    void renderRows(int W, int H, const DetailScene& sc, int y0, int y1, Sink& sink) {
        if (y0 < 0) y0 = 0; if (y1 > H) y1 = H;
        for (int sy = y0; sy < y1; sy += STRIP_H) {
            const int sh = (sy + STRIP_H > y1) ? (y1 - sy) : STRIP_H;
            scanr::S s{ buf_, W, sy, sh }; composeStrip(s, W, H, sc); sink.strip(0, sy, W, sh, buf_);
        }
    }
    void renderFull(int W, int H, const DetailScene& sc, Sink& sink) { renderRows(W, H, sc, 0, H, sink); }
    void renderRegion(int W, int H, const DetailScene& sc, uieng::Rect r, Sink& sink) {
        renderRows(W, H, sc, (r.y / STRIP_H) * STRIP_H, r.y + r.h, sink);
    }
private:
    uint16_t buf_[MAXW * STRIP_H];
};

}  // namespace detr
