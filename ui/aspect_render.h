// aspect_render.h -- static ASPECT (display) compositor + scene.
//
// What the instrument looks like: a themed dial preview beside THEME / SWEEP /
// ALERT steppers. The preview reuses scanr::drawArtwork (the theme's emblem or
// heptagram) rather than a second copy of the dial. Regions come from
// uieng::aspect(W,H); the caller supplies each row's value pre-formatted (theme
// name, sweep name, alert name) and whether ALERT is actionable. Reuses subr::
// chrome + stepBtn. Host + device compilable (no Arduino deps).
#pragma once
#include "scan_render.h"
#include "display_controls.h"
#include "sub_render.h"
#include "layout_engine.h"
#include "palette.h"
#include "font5x7.h"
#include "canvas.h"
#include <cstdint>
#include <cstdio>

namespace aspr {

using scanr::Sink;
using scanr::GnssFix;
using scanr::Rec;

struct AspectScene {
    bool controls=false; displayctl::State control;
    uint8_t brightness=80,idleIdx=2,idleMode=0;
    GnssFix  gps;
    int      nodes = 0;
    bool     hasCell = false; int battPct = 0;
    ui::PoiStatus poi = ui::PoiStatus::Ready;
    bool pmuOk = false, charging = false;
    ui::StorageState sd=ui::StorageState::Missing;
    bool bridgeOn = false;
    Rec      rec = Rec::Off;
    const char* themeName = "";
    const char* sweepName = "";
    const char* alertName = "";
    bool plain = false;
    bool     alertLive = false;     // ALERT row actionable (a theme with a choice)
    int      packsSkipped = 0;      // theme packs skipped at boot (status)
    uint8_t  theme = 0;             // drives the preview artwork
    uint32_t now = 0;
};

inline void drawControls(scanr::S& s,int W,int H,const AspectScene& sc){
 using namespace scanr;const auto l=displayctl::layout(W,H);const auto& v=sc.control;
 const char* titles[]={"BRIGHTNESS","IDLE AFTER","WHEN IDLE","ORIENTATION","TOUCH TEST","CALIBRATION","SENSITIVITY","WAKE TOUCH","THEME","SWEEP","ALERT STYLE","LABELS"};
 char brightness[12],idle[16],sens[12];snprintf(brightness,sizeof brightness,"%u%%",sc.brightness);
 if(sc.idleIdx==0)snprintf(idle,sizeof idle,"NEVER");else snprintf(idle,sizeof idle,"%u SEC",ui::IDLE_S[sc.idleIdx%ui::IDLE_N]);
 if(v.sensitivity<0)snprintf(sens,sizeof sens,"FACTORY");else snprintf(sens,sizeof sens,"%d",v.sensitivity);
 const char* values[]={brightness,idle,sc.idleMode==ui::IDLE_OFF?"OFF":sc.idleMode==ui::IDLE_ANIM?"ANIMATION":"DIM","LANDSCAPE","OPEN PAD","FACTORY MAP",sens,"WAKE ONLY",sc.themeName,sc.sweepName,sc.alertName,sc.plain?"PLAIN":"BRAND"};
 if(v.editor>=0){
  centered(s,W/2,53,titles[v.editor],1,pal::BRIGHT);
  if(v.editor==4){
   frame(s,l.pad.x,l.pad.y,l.pad.w,l.pad.h,pal::EDGE);
   for(int x:{l.pad.x+14,l.pad.x+l.pad.w-15})for(int y:{l.pad.y+14,l.pad.y+l.pad.h-15}){box(s,x-6,y,13,1,pal::WITCH_HI);box(s,x,y-6,1,13,pal::WITCH_HI);}
   for(int i=0;i<v.count;i++){const auto d=v.trail[i];box(s,d.x,d.y,2,2,pal::GREEN);}
   char status[48];snprintf(status,sizeof status,"X %d  Y %d  %s",v.x,v.y,v.down?"PRESSED":"RELEASED");centered(s,W/2,67,status,1,pal::TEXT);
  }else{
   centered(s,W/2,86,values[v.editor],fitScale(values[v.editor],W-24,3),pal::BRIGHT);
   centered(s,W/2,118,v.error?"WRITE FAILED - RETRY DONE":v.editor==6?"Lower = more sensitive. DONE applies.":"Changes preview live and save automatically",1,v.error?pal::AMBER:pal::DIM);
   subr::stepBtn(s,l.minus,"-",true);subr::stepBtn(s,l.plus,"+",true);
  }
  frame(s,l.reset.x,l.reset.y,l.reset.w,l.reset.h,pal::EDGE_LIT);centered(s,l.reset.x+l.reset.w/2,l.reset.y+17,v.editor==4?"CLEAR":"DEFAULT",1,pal::TEXT);
  frame(s,l.done.x,l.done.y,l.done.w,l.done.h,pal::EDGE_LIT);centered(s,l.done.x+l.done.w/2,l.done.y+17,"DONE",1,pal::BRIGHT);return;
 }
 const char* tabs[]={"DISPLAY","TOUCH","APPEARANCE"};
 for(int i=0;i<3;i++){auto r=l.tabs[i];box(s,r.x,r.y,r.w,r.h,pal::PANEL);centered(s,r.x+r.w/2,61,tabs[i],1,i==v.tab?pal::BRIGHT:pal::DIM);if(i==v.tab)box(s,r.x+10,84,r.w-20,2,pal::WITCH_HI);}
 for(int i=0;i<4;i++){auto r=l.cards[i];int id=v.tab*4+i;bool enabled=!((id==6&&v.sensitivity<0)||id==3||id==5||id==7||(id==10&&!sc.alertLive));
  box(s,r.x,r.y,r.w,r.h,pal::PANEL);frame(s,r.x,r.y,r.w,r.h,pal::EDGE);txt(s,r.x+8,r.y+7,titles[id],1,pal::DIM);
  txt(s,r.x+8,r.y+(r.h>=70?30:24),values[id],fitScale(values[id],r.w-16,2),enabled?pal::BRIGHT:pal::DIM);
  if(enabled)box(s,r.x+8,r.y+r.h-3,r.w-16,1,pal::WITCH_HI);
 }
}

inline void composeStrip(scanr::S& s, int W, int H, const AspectScene& sc) {
    using scanr::box; using scanr::txt; using scanr::frame; using scanr::centered;
    const uieng::AspectLayout L = uieng::aspect(W, H);
    // shared design-1 chrome (header + nav, no tab lit) + STOP/REC record control
    scanr::drawChrome(s, W, H, L.body, {sc.gps, sc.nodes, sc.hasCell, sc.battPct, sc.rec, sc.theme, sc.bridgeOn, sc.pmuOk, sc.charging, sc.poi, sc.sd}, -1);

    if(sc.controls){drawControls(s,W,H,sc);return;}

    // preview: the theme's own artwork, scaled into the preview box
    const uieng::Rect pv = L.preview;
    box(s, pv.x, pv.y, pv.w, pv.h, ui::Canvas::mix(pal::VOID, pal::PANEL, 1, 3));
    frame(s, pv.x, pv.y, pv.w, pv.h, pal::EDGE);
    const int pcx = pv.x + pv.w / 2, pcy = pv.y + pv.h / 2;
    int pr = (pv.w < pv.h ? pv.w : pv.h) / 2 - 8; if (pr < 0) pr = 0;
    if (pr > 6) scanr::drawArtwork(s, sc.theme, pcx, pcy, pr);
    if (sc.packsSkipped > 0) {
        char sk[16];
        if (sc.packsSkipped > 99) snprintf(sk, sizeof sk, "99+ SKIP");
        else snprintf(sk, sizeof sk, "%d SKIP", sc.packsSkipped);
        txt(s, pv.x + 4, pv.y + pv.h - 9, sk, 1, pal::AMBER);
    }

    frame(s,L.labels.x,L.labels.y,L.labels.w,L.labels.h,pal::EDGE);
    centered(s,L.labels.x+L.labels.w/2,L.labels.y+7,"LABELS",1,pal::DIM);
    centered(s,L.labels.x+L.labels.w/2,L.labels.y+23,sc.plain?"PLAIN":"BRAND",1,pal::BRIGHT);

    // rows: THEME / SWEEP / ALERT
    static const char* const lab[3] = {"THEME", "SWEEP", "ALERT"};
    const char* val[3] = { sc.themeName, sc.sweepName, sc.alertName };
    for (int i = 0; i < 3; i++) {
        const uieng::Rect r = L.rows[i];
        const bool live = (i < 2) || sc.alertLive;
        if (i) box(s, r.x, r.y, r.w, 1, ui::Canvas::mix(pal::PANEL, pal::EDGE, 1, 2));
        txt(s, r.x + 2, r.y + 6, lab[i], 1, pal::DIM);
        char value[32];snprintf(value,sizeof value,"%s",val[i]?val[i]:"");
        const int maxChars=(L.minus[i].x-r.x-8)/6;
        if(maxChars>=3 && int(strlen(value))>maxChars){value[maxChars]=0;value[maxChars-1]=value[maxChars-2]=value[maxChars-3]='.';}
        txt(s,r.x+2,r.y+24,value,1,live?pal::BRIGHT:pal::DIM);
        subr::stepBtn(s, L.minus[i], "-", live);
        subr::stepBtn(s, L.plus[i],  "+", live);
    }

}

class Compositor {
public:
    static constexpr int MAXW = 480, STRIP_H = 16, BUF_BYTES = MAXW * STRIP_H * 2;
    void renderRows(int W, int H, const AspectScene& sc, int y0, int y1, Sink& sink) {
        if (y0 < 0) y0 = 0; if (y1 > H) y1 = H;
        for (int sy = y0; sy < y1; sy += STRIP_H) {
            const int sh = (sy + STRIP_H > y1) ? (y1 - sy) : STRIP_H;
            scanr::S s{ buf_, W, sy, sh }; composeStrip(s, W, H, sc); sink.strip(0, sy, W, sh, buf_);
        }
    }
    void renderFull(int W, int H, const AspectScene& sc, Sink& sink) { renderRows(W, H, sc, 0, H, sink); }
    void renderRegion(int W, int H, const AspectScene& sc, uieng::Rect r, Sink& sink) {
        renderRows(W, H, sc, (r.y / STRIP_H) * STRIP_H, r.y + r.h, sink);
    }
private:
    uint16_t buf_[MAXW * STRIP_H];
};

}  // namespace aspr
