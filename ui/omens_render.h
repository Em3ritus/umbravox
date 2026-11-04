// omens_render.h -- static OMENS compositor + scene.
//
// OMENS is the alert record, newest first: confidence, source, age, and whether
// the hit carries a position. Tap rows for identity and evidence. Regions come from
// uieng::omens(W,H); painted as strips from scene state. Reuses the shared
// subr:: chrome. Host + device compilable (no Arduino deps).
#pragma once
#include "scan_render.h"
#include "sub_render.h"
#include "layout_engine.h"
#include "palette.h"
#include "font5x7.h"
#include "canvas.h"
#include <cstdint>
#include <cstdio>

namespace omnr {

using scanr::Sink;
using scanr::GnssFix;
using scanr::Rec;

struct OmenRow {
    const char* omen;      // threat name
    const char* source;    // device/network name ("" -> none)
    uint8_t     conf;      // 0 LOW, 1 MED, 2 HIGH
    uint32_t    agoS;
    bool        located;
    bool        test = false;
    const char* key = nullptr;
};

struct OmenScene {
    GnssFix  gps;
    int      nodes = 0;
    bool     hasCell = false; int battPct = 0;
    ui::PoiStatus poi = ui::PoiStatus::Ready;
    bool pmuOk = false, charging = false;
    ui::StorageState sd=ui::StorageState::Missing;
    bool bridgeOn = false;
    bool plain = false;   // label mode: false = on-brand (OMENS), true = plain (ALERTS)
    Rec      rec = Rec::Off;
    const OmenRow* rows = nullptr; int nRows = 0;
    int      total = 0;                         // everything recorded (>= nRows)
    uint8_t level = 1; // LOW/MED/HIGH/OFF
    uint8_t  theme = 0;
    uint32_t now = 0;
    const char* detail = nullptr;
    int detailPage = 0;
};

inline uint16_t confColor(uint8_t c) { return c >= 2 ? pal::RED : c == 1 ? pal::AMBER : pal::WITCH; }
inline void agoStr(char* out, int n, uint32_t s) {
    if (s < 60)        snprintf(out, n, "%uS", (unsigned)s);
    else if (s < 3600) snprintf(out, n, "%uM", (unsigned)(s / 60));
    else               snprintf(out, n, "%uH", (unsigned)(s / 3600));
}

// Fixed-width wrapping preserves every character, including long registered names.
inline int detailLines(const char* text,int cols,int first=0,int count=0,scanr::S* strip=nullptr,int x=0,int y=0,int scale=1){
    int line=0;const char* p=text?text:"";
    while(*p){char buf[80];int n=0,lastSpace=-1;
        while(p[n] && p[n]!='\n' && n<cols && n<79){if(p[n]==' ')lastSpace=n;++n;}
        if(p[n] && p[n]!='\n' && lastSpace>0)n=lastSpace;
        for(int i=0;i<n;i++)buf[i]=p[i];p+=n;
        if(*p=='\n'||*p==' ')++p;buf[n]=0;
        if(strip && line>=first && line<first+count)scanr::txt(*strip,x,y+(line-first)*(scale==2?18:11),buf,scale,pal::TEXT);
        ++line;
    }return line;
}
inline int detailPageCount(int W,int H,const char* text){const auto g=uieng::omens(W,H);int scale=W>=400?2:1;int per=(g.prev.y-g.body.y-24)/(scale==2?18:11);return (detailLines(text,(g.body.w-8)/(6*scale))+per-1)/per;}

inline void composeStrip(scanr::S& s, int W, int H, const OmenScene& sc) {
    using scanr::box; using scanr::txt; using scanr::centered;
    const uieng::LogLayout L = uieng::omens(W, H);
    // shared design-1 chrome (header + nav, no tab lit) + STOP/REC record control
    scanr::drawChrome(s, W, H, L.body, {sc.gps, sc.nodes, sc.hasCell, sc.battPct, sc.rec, sc.theme, sc.bridgeOn, sc.pmuOk, sc.charging, sc.poi, sc.sd}, -1);

    if(sc.detail){
        txt(s,L.title.x,L.title.y+2,sc.plain?"ALERT DETAIL":"OMEN DETAIL",2,pal::BRIGHT);
        int pages=detailPageCount(W,H,sc.detail);if(pages<1)pages=1;
        int page=sc.detailPage;if(page>=pages)page=pages-1;if(page<0)page=0;
        int scale=W>=400?2:1;int per=(L.prev.y-L.body.y-24)/(scale==2?18:11);
        detailLines(sc.detail,(L.body.w-8)/(6*scale),page*per,per,&s,L.body.x+4,L.body.y+24,scale);
        centered(s,L.prev.x+22,L.prev.y+15,"<",2,pal::TEXT);
        centered(s,L.next.x+22,L.next.y+15,">",2,pal::TEXT);
        centered(s,L.page.x+L.page.w/2,L.page.y+5,sc.plain?"BACK TO ALERTS":"BACK TO OMENS",1,pal::BRIGHT);
        char pageText[24];snprintf(pageText,sizeof pageText,"%d / %d",page+1,pages);
        centered(s,L.page.x+L.page.w/2,L.page.y+24,pageText,1,pal::DIM);return;
    }
    txt(s, L.title.x, L.title.y + 2, sc.plain?"ALERTS":"OMENS", 2, pal::BRIGHT);
    const char* levels[]={"LOW","MEDIUM","HIGH","OFF"};
    char level[24];snprintf(level,sizeof level,"LEVEL %s",levels[sc.level<4?sc.level:3]);
    txt(s,L.title.x+L.title.w-Font::width(level,1),L.title.y+4,level,1,sc.level==3?pal::AMBER:pal::DIM);
    scanr::frame(s,L.prev.x,L.prev.y,L.prev.w,L.prev.h,pal::EDGE);scanr::frame(s,L.next.x,L.next.y,L.next.w,L.next.h,pal::EDGE);scanr::frame(s,L.page.x,L.page.y,L.page.w,L.page.h,pal::EDGE);
    centered(s,L.prev.x+22,L.prev.y+15,"<",2,pal::TEXT);
    centered(s,L.next.x+22,L.next.y+15,">",2,pal::TEXT);
    centered(s,L.page.x+L.page.w/2,L.page.y+6,"TEST ALERT",1,pal::BRIGHT);
    centered(s,L.page.x+L.page.w/2,L.page.y+23,"SIMULATED / NO RF",1,pal::DIM);

    // column header
    txt(s, L.header.x + 4,  L.header.y + 2, "MATCH / SOURCE", 1, pal::EDGE_LIT);
    txt(s, L.header.x + L.header.w - Font::width("AGE",1), L.header.y + 2, "AGE", 1, pal::EDGE_LIT);

    box(s, L.list.x, L.list.y, L.list.w, L.list.h, ui::Canvas::mix(pal::VOID, pal::PANEL, 1, 3));
    if (sc.total == 0) {
        centered(s, L.list.x + L.list.w/2, L.list.y + L.list.h/2 - 6, sc.level==3?"ALERTS DISABLED":"NO MATCHES RECORDED", 1, pal::DIM);
        centered(s, L.list.x + L.list.w/2, L.list.y + L.list.h/2 + 4, sc.level==3?"CHANGE LEVEL IN SETTINGS":"THIS BOOT / FILTERED HISTORY", 1, pal::EDGE_LIT);
    } else {
        for (int i = 0; i < sc.nRows && i < L.nRows; i++) {
            const OmenRow& o = sc.rows[i];
            const int y = L.list.y + 2 + i * L.rowH;
            const uint16_t cc = o.test?pal::ICE:confColor(o.conf);
            box(s, L.list.x + 2, y + 1, 2, L.rowH - 4, cc);
            txt(s, L.list.x + 7, y + 1, o.omen ? o.omen : "", 1, cc);
            txt(s,L.list.x+7,y+16,o.source?o.source:"",1,pal::TEXT);
            char ab[12]; agoStr(ab,sizeof ab,o.agoS);
            txt(s,L.list.x+L.list.w-2-Font::width(ab,1),y+1,ab,1,pal::DIM);
            const char* confidence=o.test?"SIMULATED":o.conf>=2?"HIGH":o.conf==1?"MEDIUM":"LOW";
            txt(s,L.list.x+7,y+29,confidence,1,cc);
            if (i) box(s, L.list.x + 4, y, L.list.w - 8, 1, ui::Canvas::mix(pal::PANEL, pal::EDGE, 1, 2));
        }
    }
}

class Compositor {
public:
    static constexpr int MAXW = 480, STRIP_H = 16, BUF_BYTES = MAXW * STRIP_H * 2;
    void renderRows(int W, int H, const OmenScene& sc, int y0, int y1, Sink& sink) {
        if (y0 < 0) y0 = 0; if (y1 > H) y1 = H;
        for (int sy = y0; sy < y1; sy += STRIP_H) {
            const int sh = (sy + STRIP_H > y1) ? (y1 - sy) : STRIP_H;
            scanr::S s{ buf_, W, sy, sh }; composeStrip(s, W, H, sc); sink.strip(0, sy, W, sh, buf_);
        }
    }
    void renderFull(int W, int H, const OmenScene& sc, Sink& sink) { renderRows(W, H, sc, 0, H, sink); }
    void renderRegion(int W, int H, const OmenScene& sc, uieng::Rect r, Sink& sink) {
        renderRows(W, H, sc, (r.y / STRIP_H) * STRIP_H, r.y + r.h, sink);
    }
private:
    uint16_t buf_[MAXW * STRIP_H];
};

}  // namespace omnr
