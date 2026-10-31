// scan_render.h — static SCAN compositor through a small RGB565 strip buffer.
//
// Regions come from uieng::scan(W,H); colors come from the existing theme model
// (pal::, set by pal::setTheme) -- this adds a renderer, it does NOT touch
// theme-pack loading/authoring. The screen is painted as horizontal strips: each
// strip is composed COMPLETELY from scene state (background first, then every
// element clipped into it), then handed to a Sink once. No full framebuffer, no
// animation. Host and device compilable (no Arduino deps).
#pragma once
#include "storage_status.h"
#include "poi_state.h"
#include "radar_geometry.h"
#include "crew_art.h"
#include "layout_engine.h"
#include "palette.h"
#include "themepack.h"
#include "font5x7.h"
#include "gnss.h"
#include "canvas.h"
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace scanr {

// Fully-composed RGB565 strip output. Device pushes to the panel; host collects.
struct Sink {
    virtual void strip(int x, int y, int w, int h, const uint16_t* px) = 0;
    virtual ~Sink() {}
};

// --- presentation inputs: caller-supplied scene data (no embedded fixtures) ---
enum class GpsState : uint8_t { Acquiring, Fresh, Stale };   // never render Stale as Fresh
enum class Rec : uint8_t { Off, On, NoCard, Saving, Incomplete };

struct GnssFix {                 // raw receiver state; projected to a label below
    bool     valid = false;      // a position has been fixed (ui::Gnss::Fix::valid)
    uint8_t  mode  = 0;          // GSA fix mode: 1=none, 2=2D, 3=3D (0=no GSA yet)
    int      sats  = 0;          // satellites used
    uint32_t ageMs = 0;          // ms since the fix landed (now - atMs)
};

struct FieldContact {            // a raw sighting (host/fixture convenience)
    const char* key;             // identity, for a stable SYNTHETIC bearing
    int         rssi;            // dBm
    int         band;            // 0=2.4GHz, 1=5GHz, 2=BLE
    uint32_t    lastSeen;        // ms; age = now - lastSeen
};

// What the compositor actually draws: a contact already reduced to its field
// placement. The device supplies the REAL on-dial bearing and distance from
// capture (no identity is invented); host fixtures derive one from a raw
// FieldContact via toBlip(). Pre-projected geometry, not a fabricated key.
struct FieldBlip {
    uint16_t bearing;            // 0..359 degrees on the field
    uint8_t  dist;               // 0..100; 0 = on top of us (centre), 100 = rim
    uint8_t  band;               // 0=2.4GHz, 1=5GHz, 2=BLE
    uint32_t lastSeen;           // ms; age = now - lastSeen
};

struct ScanScene {
    bool bridgeOn = false;
    // status
    GnssFix  gps;                 // GNSS receiver state (projected by gpsLabel)
    int      nodes = 0;
    bool     hasCell = false; int battPct = 0;
    ui::PoiStatus poi = ui::PoiStatus::Ready;
    bool pmuOk = false, charging = false;
    ui::StorageState sd=ui::StorageState::Missing;
    Rec      rec = Rec::Off;
    // counts
    long     wifi = 0, g5 = 0, ble = 0;
    // hero
    const char* ch = "CH --"; const char* chBand = "2.4 GHz";
    const FieldBlip* blips = nullptr; int nBlips = 0;           // caller-owned, pre-projected
    const int*       channels = nullptr; int nChannels = 0;    // 0..100 each; kChannels entries
    uint32_t            now = 0;                                 // clock for contact age
    // theme
    uint8_t  theme = 0;          // pal::ThemeId; drives artwork selection
    bool animateSweep = false; // caller opt-in; static fixtures retain their exact pixels
};

// --- projections (pure, host-testable): scene data -> draw-ready values --------
static constexpr uint32_t kContactExpireMs = 60000;   // matches capture's unseen-drop

struct Marker { int dx, dy, band, sizePx; bool shown; };   // dx,dy relative to field centre
inline unsigned keyHash(const char* k) {                   // stable synthetic angle source
    unsigned h = 2166136261u; while (k && *k) h = (h ^ (unsigned char)*k++) * 16777619u; return h;
}
// Place a contact already reduced to polar form: a bearing (degrees) and a
// 0..100 distance (0 = on top of us -> field centre, 100 = rim). This is the
// one source of truth for field placement -- expiry, the band guard, the
// radius and the age fade. The device's capture field is already polar and
// feeds this directly; host/raw contacts are reduced to it by projectContact.
inline Marker placeMarker(int bearingDeg, int dist, int band, uint32_t ageMs, int fr) {
    Marker m{0, 0, band, 3, false};
    if (band < 0 || band > 2) return m;                    // invalid band -> rejected, not shown
    if (ageMs > kContactExpireMs) return m;                // expired -> not shown
    m.shown = true;
    if (dist < 0) dist = 0; else if (dist > 100) dist = 100;
    const int rr = fr * dist / 100;                        // 0=centre, 100=rim
    const double a = (bearingDeg % 360) * 3.14159265 / 180.0;   // synthetic bearing, not geographic
    m.dx = (int)(rr * std::cos(a)); m.dy = (int)(rr * std::sin(a));
    m.sizePx = ageMs < 20000u ? 4 : ageMs < 40000u ? 3 : 2;    // fade with age
    return m;
}
// Reduce a raw sighting to a drawable blip: a SYNTHETIC bearing from its key
// and a distance from its signal. The device does not use this -- it supplies
// the real on-dial bearing/distance from capture -- but host fixtures and the
// projection tests do.
inline FieldBlip toBlip(const FieldContact& c) {
    const int rssi = c.rssi < -90 ? -90 : (c.rssi > -30 ? -30 : c.rssi);
    return FieldBlip{ (uint16_t)(keyHash(c.key) % 360),
                      (uint8_t)((-(rssi + 30)) * 100 / 60),   // strong(-30)=0, weak(-90)=100
                      (uint8_t)c.band, c.lastSeen };
}
inline Marker projectContact(const FieldContact& c, uint32_t now, int fr) {
    if (c.band < 0 || c.band > 2) return Marker{0, 0, c.band, 3, false};   // guard before cast
    const FieldBlip b = toBlip(c);
    return placeMarker(b.bearing, b.dist, b.band, now - b.lastSeen, fr);
}
// Freshness mirrors ui::Gnss::fresh(now) exactly: a valid position no older than
// FRESH_MS (strict <). It does NOT depend on GSA mode -- a valid-but-aged
// position reads STALE, never ACQUIRING. Mode only chooses the label's
// dimensionality; a fresh fix with no GSA yet is shown without claiming one.
static constexpr uint32_t kGpsFreshMs = ui::Gnss::FRESH_MS;   // single source of truth
inline GpsState gpsState(const GnssFix& f) {
    if (!f.valid) return GpsState::Acquiring;             // no valid position
    return f.ageMs < kGpsFreshMs ? GpsState::Fresh : GpsState::Stale;   // strict 5s boundary
}
inline uint16_t gpsLabel(const GnssFix& f, char* buf, int n) {     // text -> buf, returns colour
    switch (gpsState(f)) {
        case GpsState::Fresh:                                      // dimensionality only when GSA says so
            if      (f.mode == 3) snprintf(buf,n,"3D %dSV",  f.sats);
            else if (f.mode == 2) snprintf(buf,n,"2D %dSV",  f.sats);
            else                  snprintf(buf,n,"FIX %dSV", f.sats);   // valid, dimension unknown
            return pal::GREEN;                                    // colour tracks freshness, not mode
        case GpsState::Stale:
            snprintf(buf,n,"STALE %us",(unsigned)(f.ageMs/1000));  return pal::AMBER;
        default:
            snprintf(buf,n,"ACQUIRING");                           return pal::DIM;
    }
}
inline uint16_t recLabel(const ScanScene& s, char* buf, int n) {
    switch (s.rec) {
        case Rec::On:     snprintf(buf,n,"STOP");   return pal::RED;
        case Rec::Incomplete: snprintf(buf,n,"RECORD"); return pal::AMBER;
        case Rec::Saving: snprintf(buf,n,"SAVING"); return pal::AMBER;
        case Rec::NoCard: snprintf(buf,n,"NO SD"); return pal::AMBER;
        default:          snprintf(buf,n,"RECORD");  return pal::DIM;
    }
}

// --- strip drawing (all coords global; writes clipped to the strip) -----------
struct S { uint16_t* px; int W, y0, h; };
inline void pt(S& s, int x, int y, uint16_t c) {
    if (x < 0 || x >= s.W || y < s.y0 || y >= s.y0 + s.h) return;
    s.px[(y - s.y0) * s.W + x] = c;
}
inline void box(S& s, int rx, int ry, int rw, int rh, uint16_t c) {
    int x0 = rx<0?0:rx, x1 = rx+rw>s.W?s.W:rx+rw;
    int y0 = ry<s.y0?s.y0:ry, y1 = (ry+rh>s.y0+s.h)?s.y0+s.h:ry+rh;
    for (int y=y0; y<y1; y++){ uint16_t* r=s.px+(y-s.y0)*s.W; for (int x=x0;x<x1;x++) r[x]=c; }
}
inline void txt(S& s, int gx, int gy, const char* t, int sc, uint16_t fg) {
    if (gy >= s.y0+s.h || gy+Font::H*sc <= s.y0) return;
    for (int i=0;t[i];i++) for (int ry=0;ry<Font::H;ry++){ uint8_t b=Font::row(t[i],ry);
        for (int cx=0;cx<Font::W;cx++){ if(!(b&(0x10>>cx))) continue;
            for (int ky=0;ky<sc;ky++) for (int kx=0;kx<sc;kx++)
                pt(s, gx+i*Font::ADV*sc+cx*sc+kx, gy+ry*sc+ky, fg); } }
}
inline void line(S& s, int x0,int y0,int x1,int y1, uint16_t c) {
    int dx=std::abs(x1-x0), dy=-std::abs(y1-y0), sx=x0<x1?1:-1, sy=y0<y1?1:-1, e=dx+dy;
    for(;;){ pt(s,x0,y0,c); if(x0==x1&&y0==y1) break; int e2=2*e;
             if(e2>=dy){e+=dy;x0+=sx;} if(e2<=dx){e+=dx;y0+=sy;} }
}
inline int  fitScale(const char* t, int maxW, int hi) {         // largest scale <= hi that fits
    for (int s=hi; s>1; --s) if (Font::width(t,s) <= maxW) return s; return 1;
}
inline void centered(S& s, int cx, int gy, const char* t, int sc, uint16_t c) {
    txt(s, cx - Font::width(t,sc)/2, gy, t, sc, c);
}

// --- field artwork (follows the theme's selection) ----------------------------
inline void disc(S& s, int cx, int cy, int r, uint16_t c) {
    for (int y=std::max(cy-r,s.y0);y<=std::min(cy+r,s.y0+s.h-1);y++) for (int x=cx-r;x<=cx+r;x++)
        if ((x-cx)*(x-cx)+(y-cy)*(y-cy) <= r*r) pt(s,x,y,c);
}
inline void frame(S& s, int x, int y, int w, int h, uint16_t c) {
    box(s,x,y,w,1,c); box(s,x,y+h-1,w,1,c); box(s,x,y,1,h,c); box(s,x+w-1,y,1,h,c);
}
inline void heptaInto(S& s, int cx, int cy, int r, uint16_t ring, uint16_t star) {
    for (int a=0;a<360;a+=5){ double t=a*3.14159265/180.0; pt(s, cx+(int)(r*std::cos(t)), cy+(int)(r*std::sin(t)), ring); }
    int vx[7], vy[7];
    for (int i=0;i<7;i++){ double t=(-90.0+i*360.0/7)*3.14159265/180.0; vx[i]=cx+(int)(r*std::cos(t)); vy[i]=cy+(int)(r*std::sin(t)); }
    int k=0; for (int n=0;n<7;n++){ int nk=(k+3)%7; line(s,vx[k],vy[k],vx[nk],vy[nk],star); k=nk; }
}
// Procedural badge: no bitmap or heap allocation.
inline void diamondInto(S& s,int cx,int cy,int r,uint16_t color){
    line(s,cx,cy-r,cx+r,cy,color);line(s,cx+r,cy,cx,cy+r,color);
    line(s,cx,cy+r,cx-r,cy,color);line(s,cx-r,cy,cx,cy-r,color);
}
// 45-degree fading trail. Integer cross-products inside the current strip;
// no atan2, sqrt, allocations, or framebuffer-sized scratch work per pixel.
inline void radarTrail(S& s,int cx,int cy,int r,uint32_t phase){
    if(r<1 || cy+r<s.y0 || cy-r>=s.y0+s.h)return;
    const double a=(int(phase%72)*5-90)*3.14159265/180.0;
    const int lx=int(cos(a)*1024),ly=int(sin(a)*1024);
    const int tx=int(cos(a-3.14159265/4)*1024),ty=int(sin(a-3.14159265/4)*1024);
    uint16_t fade[8];for(int k=0;k<8;k++)fade[k]=ui::Canvas::mix(pal::VOID,pal::SWEEP,k+1,32);
    for(int y=std::max(cy-r,s.y0);y<=std::min(cy+r,s.y0+s.h-1);y++)for(int x=std::max(0,cx-r);x<=std::min(s.W-1,cx+r);x++){
        int dx=x-cx,dy=y-cy;if(dx*dx+dy*dy>r*r)continue;
        int c0=tx*dy-ty*dx,c1=dx*ly-dy*lx;
        if(c0>=0 && c1>=0 && c0+c1>0)pt(s,x,y,fade[std::min(7,c0*8/(c0+c1))]);
    }
    line(s,cx,cy,cx+r*lx/1024,cy+r*ly/1024,pal::SWEEP_HI);
}

inline void emblemInto(S& s, const themepack::Emblem& e, int cx, int cy, int r, bool transparentBlack=false, int opacity=100, bool circular=true) {   // same mask/scale as motif::drawEmblem
    if (r<=0 || !e.w || !e.h || !e.px || !e.ncol || e.ncol>16) return;
    const int d=2*r;
    uint16_t colors[16];for(int i=0;i<e.ncol;i++)colors[i]=ui::Canvas::mix(pal::VOID,e.pal[i],opacity,100);
    for (int y=std::max(cy-r,s.y0);y<=std::min(cy+r,s.y0+s.h-1);y++){ const int dy=y-cy;
        for (int x=cx-r;x<=cx+r;x++){ const int dx=x-cx; if(circular && dx*dx+dy*dy>r*r) continue;
            int sx=(x-(cx-r))*e.w/d, sy=(y-(cy-r))*e.h/d;
            if(sx<0)sx=0; else if(sx>=e.w)sx=e.w-1; if(sy<0)sy=0; else if(sy>=e.h)sy=e.h-1;
            const uint16_t color=e.pal[e.px[sy*e.w+sx]];
            if(!transparentBlack || color)pt(s,x,y,colors[e.px[sy*e.w+sx]]); } }
}
// Precomputed artwork: clipped spans, no heap use and no trigonometry.
inline void crewArt(S& s,int cx,int cy,int radius,int style){
    const bool small=radius<60;
    const crewart::Span* runs=nullptr;const uint16_t* colors=nullptr;size_t count=0;
    if(style==0 && small){runs=crewart::mascot39;colors=crewart::mascot39Colors;count=sizeof crewart::mascot39/sizeof(*runs);}
    else if(style==0){runs=crewart::mascot85;colors=crewart::mascot85Colors;count=sizeof crewart::mascot85/sizeof(*runs);}
    else if(style==1 && small){runs=crewart::cvlt39;colors=crewart::cvlt39Colors;count=sizeof crewart::cvlt39/sizeof(*runs);}
    else if(style==1){runs=crewart::cvlt85;colors=crewart::cvlt85Colors;count=sizeof crewart::cvlt85/sizeof(*runs);}
    else if(style==2 && small){runs=crewart::orbit39;colors=crewart::orbit39Colors;count=sizeof crewart::orbit39/sizeof(*runs);}
    else if(style==2){runs=crewart::orbit85;colors=crewart::orbit85Colors;count=sizeof crewart::orbit85/sizeof(*runs);}
    else if(style==3 && small){runs=crewart::sigil39;colors=crewart::sigil39Colors;count=sizeof crewart::sigil39/sizeof(*runs);}
    else if(style==3){runs=crewart::sigil85;colors=crewart::sigil85Colors;count=sizeof crewart::sigil85/sizeof(*runs);}
    if(!runs)return;
    const int base=small?39:85;
    for(size_t i=0;i<count;i++){const auto& a=runs[i];int y=cy+a.y*radius/base;if(y<s.y0)continue;if(y>=s.y0+s.h)break;
      int x=cx+a.x*radius/base;int end=cx+(a.x+a.n)*radius/base;box(s,x,y,std::max(1,end-x),1,colors[a.c]);}
}
// Honor the theme's artwork, same policy as motif::drawMotif: a bitmap emblem
// when the theme carries one, else the {7/3} heptagram.
inline void drawArtwork(S& s, uint8_t theme, int cx, int cy, int r) {
    if(pal::centrepiece(theme)==pal::CP_RADAR)return;
    const themepack::Emblem* e = pal::emblem(theme);
    if (e && pal::centrepiece(theme) == pal::CP_EMBLEM) emblemInto(s, *e, cx, cy, r);
    else heptaInto(s, cx, cy, r, pal::GRID, pal::EDGE_LIT);
}

// Hero sub-layout (framed field + spectrum band). Exposed so the header/field/
// spectrum separation and no-intrusion invariants are host-testable. The channel
// plan is FIXED (count is size-independent); bar width/pitch adapt.
static constexpr int kChannels = 21, kCh24 = 13;   // 13 x 2.4GHz + 8 x 5GHz; one spectrum bar each
struct HeroSub { uieng::Rect field, spectrum; int barBase, barMax; };
inline HeroSub heroSub(uieng::Rect hr) {
    const int hdrH = 30, specH = 44;               // header clears the readout (text max y+26)
    HeroSub h;
    h.field    = {hr.x+2, hr.y+hdrH, hr.w-4, hr.h-hdrH-specH};
    const int sw=std::min(168,hr.w-8);
    h.spectrum = {hr.x+(hr.w-sw)/2, hr.y+hr.h-specH, sw, specH};
    h.barBase  = hr.y+hr.h-11;
    h.barMax   = 16;                          // 16px amplitude; every channel retained
    return h;
}

// Shared geometry for drawing and narrow animation invalidation.
inline uieng::Rect radarBounds(int W, int H) {
    const auto hr=uieng::scan(W,H).hero;
    const auto hs=heroSub(hr);
    const auto f=hs.field;
    const int cx = W>=400 ? 311 : f.x+f.w/2;
    const int cy = W>=400 ? 137 : f.y+f.h/2;
    const int r = W>=400 ? 85 : std::min({f.w/2-4,cy-(hr.y+22),hs.barBase-hs.barMax-cy-4});
    return {cx-r-2,cy-r-2,2*r+5,2*r+5}; // include the largest marker half-size
}
inline uint32_t sweepPhase(uint32_t now) { return (now / 50u) % 72u; } // 20 Hz, 3.6 s turn

// --- shared design-1 chrome (header + nav): composed by EVERY engine screen ---
// The UMBRAVOX wordmark + single-line GPS/MESH/BAT readouts, the four nav tabs
// (activeTab lit + underlined; -1 lights none) and the STOP/REC record control
// at the far right. Regions come from uieng::chrome(W,H); `body` (passed per
// screen) drives ONLY the inset dividers, so SCAN keeps its 16px body margin
// while the sub-screens keep 8px. Single-sourced so a chrome fix lands once.
struct ChromeState {
    GnssFix gps;
    int     nodes   = 0;
    bool    hasCell = false;
    int     batt    = 0;
    Rec     rec     = Rec::Off;
    uint8_t theme = 0;
    bool bridgeOn = false, pmuOk = false, charging = false;
    ui::PoiStatus poi = ui::PoiStatus::Ready;
    ui::StorageState sd=ui::StorageState::Missing;
};
inline void drawChrome(S& s, int W, int H, const uieng::Rect& body,
                       const ChromeState& st, int activeTab) {
    const uieng::Chrome c = uieng::chrome(W, H);
    // lay the full VOID ground (every screen's body sits on this), then the
    // status/nav surfaces + design #1 inset dividers (body-relative)
    box(s, 0, 0, W, H, pal::VOID);
    box(s, 0, 0, W, 44, pal::PANEL);
    box(s, 0, H - 44, W, 44, pal::PANEL);
    box(s, 8, 43, W-16, 1, pal::EDGE);
    box(s, body.x, H - 46, body.w, 1, pal::EDGE);

    for(int y:{8,13,18})box(s,14,y,16,2,activeTab==3?pal::BRIGHT:pal::TEXT);
    centered(s,22,28,"MENU",1,activeTab==3?pal::BRIGHT:pal::TEXT);
    if(activeTab==3)box(s,10,39,24,2,pal::WITCH_HI);
    // Fixed title box: theme names fit without moving any controls.
    const char* name=pal::themeName(st.theme);
    const char* second=nullptr;
    const char* first=name;
    if (!strcmp(name,"CVLT OF BAPH0MAU5")){first="CVLT OF";second="BAPH0MAU5";}
    if (!strcmp(name,"COSMIC MUFFIN")){first="COSMIC";second="MUFFIN";}
    if(second){centered(s,c.wordmark.x+c.wordmark.w/2,11,first,1,pal::BRIGHT);centered(s,c.wordmark.x+c.wordmark.w/2,25,second,1,pal::BRIGHT);}
    else {
      const int badge=pal::diamondBadge(st.theme)?22:0;
      char clipped[64];snprintf(clipped,sizeof clipped,"%s",name);
      const int scale=fitScale(name,c.wordmark.w-8-badge,2);
      const int limit=(c.wordmark.w-8-badge)/(6*scale);
      if(limit>=3 && int(strlen(clipped))>limit){clipped[limit]=0;clipped[limit-1]=clipped[limit-2]=clipped[limit-3]='.';}
      name=clipped;
      const int x=c.wordmark.x+(c.wordmark.w-Font::width(name,scale)-badge)/2;
      if(badge)diamondInto(s,x+7,21,7,pal::WITCH);
      txt(s,x+badge,(44-7*scale)/2,name,scale,pal::BRIGHT);
    }
    char hb[24];
    const GpsState gst = gpsState(st.gps);
    const uint16_t gcol = gst==GpsState::Fresh ? pal::GREEN : gst==GpsState::Stale ? pal::AMBER : pal::DIM;
    const char* fix=gst==GpsState::Fresh ? (st.gps.mode==3?"3D":st.gps.mode==2?"2D":"FIX") : gst==GpsState::Stale?"STALE":"ACQ";
    centered(s,c.statusCells[0].x+c.statusCells[0].w/2,10,"GPS",1,pal::DIM);
    centered(s,c.statusCells[0].x+c.statusCells[0].w/2,26,fix,1,gcol);
    if(c.statusCells[1].w) {
        const int nc=c.statusCells[1].x+c.statusCells[1].w/2;
        centered(s,nc,10,"NODES",1,pal::DIM);
        snprintf(hb,sizeof hb,"%d",st.nodes);
        centered(s,c.statusCells[1].x+c.statusCells[1].w/2,26,hb,1,pal::TEXT);
    }
    const auto bt=c.bridge;
    centered(s,bt.x+bt.w/2,10,"BRIDGE",1,st.bridgeOn?pal::BRIGHT:pal::DIM);
    centered(s,bt.x+bt.w/2,26,st.bridgeOn?"ON":"OFF",1,st.bridgeOn?pal::BRIGHT:pal::DIM);
    if(st.bridgeOn) {
        const int width=Font::width("BRIDGE",1);
        box(s,bt.x+(bt.w-width)/2,37,width,2,pal::WITCH_HI);
    }
    // Read-only storage icon; adjacent battery keeps its own >=44px hit.
    const uint16_t sdInk=st.sd==ui::StorageState::Ready?pal::GREEN:st.sd==ui::StorageState::Error?pal::AMBER:pal::DIM;
    // Distribute free space equally before, between, and after the icons.
    if(W>=400){
    const int iconGap=(W-c.sd.x-14-24)/3;
    const int sx=c.sd.x+iconGap,sy=13;
    box(s,sx,sy,10,1,sdInk);box(s,sx,sy,1,17,sdInk);box(s,sx,sy+16,14,1,sdInk);
    box(s,sx+13,sy+4,1,13,sdInk);
    for(int i=0;i<4;i++)box(s,sx+10+i,sy+i,1,1,sdInk);
    if(st.sd==ui::StorageState::Error){box(s,sx+6,sy+4,2,6,sdInk);box(s,sx+6,sy+12,2,2,sdInk);}
    else{
        for(int i=0;i<3;i++)box(s,sx+3+i*3,sy+3,1,4,sdInk);
        if(st.sd==ui::StorageState::Missing)for(int i=0;i<14;i++)box(s,sx+i,sy+14-i,1,2,sdInk);
    }
    const bool cell=st.pmuOk&&st.hasCell,level=cell&&st.batt>=0&&st.batt<=100;
    const int bx=sx+14+iconGap,by=cell?8:16;
    const uint16_t bi=cell?(st.charging?pal::GREEN:level&&st.batt<=20?pal::AMBER:pal::TEXT):pal::DIM;
    frame(s,bx,by,22,11,bi);box(s,bx+22,by+3,2,5,bi);
    if(level&&st.batt>0)box(s,bx+2,by+2,std::max(1,18*st.batt/100),7,bi);
    if(!st.pmuOk)centered(s,bx+11,by+2,"?",1,pal::DIM);
    else if(!st.hasCell)for(int i=0;i<20;i++)box(s,bx+1+i,by+10-i/2,1,1,pal::DIM);
    if(cell){
        if(level)snprintf(hb,sizeof hb,"%d%%",st.batt);else snprintf(hb,sizeof hb,"?");
        centered(s,bx+12,27,hb,1,bi);
        // A plus beside the battery identifies charging independently of color.
        if(st.charging){box(s,bx-5,by+5,3,1,bi);box(s,bx-4,by+4,1,3,bi);}
    }
    }else{
    const bool compact=W<400;
    const int sw=compact?12:14,sh=compact?15:17,bw=compact?18:22,bh=compact?9:11;
    const int iconGap=(W-c.sd.x-sw-(bw+2))/3;
    const int sx=c.sd.x+iconGap,sy=(44-sh)/2;
    box(s,sx,sy,sw-4,1,sdInk);box(s,sx,sy,1,sh,sdInk);box(s,sx,sy+sh-1,sw,1,sdInk);
    box(s,sx+sw-1,sy+4,1,sh-4,sdInk);
    for(int i=0;i<4;i++)box(s,sx+sw-4+i,sy+i,1,1,sdInk);
    if(st.sd==ui::StorageState::Error){box(s,sx+sw/2,sy+4,1,5,sdInk);box(s,sx+sw/2,sy+11,1,1,sdInk);}
    else{
        for(int i=0;i<3;i++)box(s,sx+3+i*(compact?2:3),sy+3,1,4,sdInk);
        if(st.sd==ui::StorageState::Missing)for(int i=0;i<sw;i++)box(s,sx+i,sy+sh-3-i*(sh-4)/(sw-1),1,2,sdInk);
    }
    const bool cell=st.pmuOk&&st.hasCell,level=cell&&st.batt>=0&&st.batt<=100;
    const int bx=sx+sw+iconGap,by=cell?8:(44-bh)/2;
    const uint16_t bi=cell?(st.charging?pal::GREEN:level&&st.batt<=20?pal::AMBER:pal::TEXT):pal::DIM;
    frame(s,bx,by,bw,bh,bi);box(s,bx+bw,by+3,2,bh-6,bi);
    if(level&&st.batt>0)box(s,bx+2,by+2,std::max(1,(bw-4)*st.batt/100),bh-4,bi);
    if(!st.pmuOk)centered(s,bx+bw/2,by+1,"?",1,pal::DIM);
    else if(!st.hasCell)for(int i=0;i<bw-2;i++)box(s,bx+1+i,by+bh-2-i*(bh-3)/(bw-3),1,1,pal::DIM);
    if(cell){
        if(level)snprintf(hb,sizeof hb,"%d%%",st.batt);else snprintf(hb,sizeof hb,"?");
        centered(s,bx+(bw+2)/2,27,hb,1,bi);
        if(st.charging){box(s,bx-5,by+bh/2,3,1,bi);box(s,bx-4,by+bh/2-1,1,3,bi);}
    }
    }

    // Three destinations, POI action, recording action. Header MENU opens the hub.
    static const char* const nav[3]={"SCAN","DOMAIN","CODEX"};
    for(int i=0;i<3;++i){
        const auto r=c.navCells[i];
        const int ns=fitScale(nav[i],r.w-8,2), width=Font::width(nav[i],ns);
        const int lx=r.x+(r.w-width)/2;
        const bool on=activeTab==i;
        txt(s,lx,H-44+(44-7*ns)/2,nav[i],ns,on?pal::BRIGHT:pal::TEXT);
        if(on)box(s,lx,H-8,width,2,pal::WITCH_HI);
    }
    const auto mark=c.navCells[3];
    const bool feedback=st.poi!=ui::PoiStatus::Ready;
    const uint16_t poiInk=st.poi==ui::PoiStatus::Saved?pal::GREEN:pal::AMBER;
    centered(s,mark.x+mark.w/2,H-(feedback?36:30),"POI",2,feedback?poiInk:pal::TEXT);
    if(feedback)centered(s,mark.x+mark.w/2,H-16,ui::poiLabel(st.poi),1,poiInk);

    const auto record=c.stop;
    ScanScene tmp;tmp.rec=st.rec;
    char label[16];recLabel(tmp,label,sizeof label);
    if(st.rec==Rec::Off)snprintf(label,sizeof label,"REC");
    const bool on=st.rec==Rec::On;
    const bool warning=st.rec==Rec::NoCard || st.rec==Rec::Saving || st.rec==Rec::Incomplete;
    const uint16_t ink=on?pal::RED:warning?pal::AMBER:pal::DIM;
    const int scale=fitScale(label,record.w-8,2),width=Font::width(label,scale);
    const int x=record.x+(record.w-width)/2;
    txt(s,x,H-30,label,scale,ink);
    if(on || st.rec==Rec::Saving)box(s,x,H-8,width,2,ink);
    if(st.rec==Rec::Incomplete)centered(s,record.x+record.w/2,H-12,"UNSAVED",1,ink);

}

// --- compose one strip completely from the scene ------------------------------
inline void composeStrip(S& s, int W, int H, const ScanScene& sc) {
    const uieng::ScanLayout L = uieng::scan(W, H);
    const uint16_t band[3] = { pal::MAGENTA, pal::ICE, pal::GREEN };
    box(s, 0, 0, W, H, pal::VOID);                                   // body

    // shared design-1 chrome: header (wordmark + GPS/MESH/BAT) + nav (SCAN lit)
    // + the STOP/REC record control. SCAN passes its own 16px body for the dividers.
    drawChrome(s, W, H, L.body, {sc.gps, sc.nodes, sc.hasCell, sc.battPct, sc.rec, sc.theme, sc.bridgeOn, sc.pmuOk, sc.charging, sc.poi, sc.sd}, 0);

    // count tiles
    static const char* const tlab[3] = {"WIFI","5 GHZ","BLE"};
    char num[16]; const long vals[3] = { sc.wifi, sc.g5, sc.ble };
    for (int i=0;i<3;i++){ const uieng::Rect t=L.tiles[i];
        box(s, t.x, t.y, t.w, t.h, pal::PANEL);
        box(s, t.x, t.y, 3, t.h, band[i]);
        frame(s, t.x, t.y, t.w, t.h, pal::EDGE);
        txt(s, t.x+12, t.y+9, tlab[i], 1, pal::DIM);
        snprintf(num, sizeof num, "%ld", vals[i]);
        int ns = fitScale(num, t.w-16, t.h>=60?3:2);
        txt(s, t.x+12, t.y + (t.h>=60?28:22), num, ns, pal::BRIGHT); }

    // hero: readout / framed field (theme artwork + contacts) / labelled spectrum.
    // Channel readout and theme identity above the radar.
    const uieng::Rect hr = L.hero;
    // Theme identity occupies the former field heading, independently of radar clipping.
    if (pal::centrepiece(sc.theme)==pal::CP_RADAR_EMBLEM || sc.theme==pal::TH_COSMICMUFFIN) {
        const auto* headingEmblem=pal::emblem(sc.theme);
        const int badgeR=W>=400?27:24;
        if(headingEmblem)emblemInto(s,*headingEmblem,hr.x+4+badgeR,hr.y+4+badgeR,badgeR,true,pal::artOpacity(sc.theme),sc.theme==pal::TH_COSMICMUFFIN);
    }
    txt(s, hr.x + hr.w - 4 - Font::width(sc.ch,2), hr.y+4, sc.ch, 2, pal::BRIGHT);  // channel, right

    const HeroSub hs = heroSub(hr);
    const uieng::Rect fld = hs.field;
    // Approved expanded field: centre (311,137), radius 85 at 480; compact derives.
    const int fcx = (W>=400) ? 311 : (fld.x+fld.w/2);
    const int fcy = (W>=400) ? 137 : (fld.y+fld.h/2);
    const int fr = (W>=400) ? 85 : std::min({fld.w/2-4,fcy-(hr.y+22),hs.barBase-hs.barMax-fcy-4});
    // design #1: a CIRCULAR radar -- NO bounding box. Three thin 1px rings at
    // radii 23/46/69 (scaled to fr) in the chrome tone, a DELICATE heptagram in
    // the seal tone (EDGE_LIT = #574465) at 0.72r with verts from east, a sweep
    // ray, and a bright centre cross.
    if (fr>6 && fcy+fr >= s.y0 && fcy-fr < s.y0+s.h) {
        const bool cvlt=sc.theme==pal::TH_BAPH0MAU5;
        const bool mascot=pal::centrepiece(sc.theme)==pal::CP_MASCOT;
        const bool orbit=sc.theme==pal::TH_COSMICMUFFIN;
        const bool sigil=pal::centrepiece(sc.theme)==pal::CP_SIGIL;
        const bool logo=pal::centrepiece(sc.theme)==pal::CP_RADAR_EMBLEM;
        const bool clean=pal::centrepiece(sc.theme)==pal::CP_RADAR || cvlt || mascot || orbit || sigil || logo;
        if(clean)radarTrail(s,fcx,fcy,fr,sc.animateSweep?sweepPhase(sc.now):27);
        for (int k=1;k<=3;k++){ const int rr=fr*(k*23)/69;
            int count=0;
            const auto* ring=radargeometry::ring(rr,count);
            if (ring) {
                for (int i=0;i<count;++i) {
                    if (fcy+ring[i].y < s.y0) continue;
                    if (fcy+ring[i].y >= s.y0+s.h) break; // sorted rows
                    pt(s,fcx+ring[i].x,fcy+ring[i].y,pal::EDGE);
                }
            } else { // other logical sizes retain the original geometry
                for (int a=0;a<1440;a++){ const double t=a*3.14159265/720.0;
                    pt(s, fcx+(int)(rr*std::cos(t)+0.5), fcy+(int)(rr*std::sin(t)+0.5), pal::EDGE); }
            }
        }
        if(cvlt || mascot || orbit || sigil)crewArt(s,fcx,fcy,fr,cvlt?1:orbit?2:sigil?3:0);
        const themepack::Emblem* e = pal::emblem(sc.theme);
        if (e && pal::centrepiece(sc.theme) == pal::CP_EMBLEM && !orbit) {
            emblemInto(s, *e, fcx, fcy, fr*72/100);         // bitmap themes keep their emblem
        } else if(!clean) {
            const int h7=fr*72/100; int vx[7], vy[7];       // delicate heptagram, verts from east
            const auto* star = h7==61 ? radargeometry::star61 : h7==49 ? radargeometry::star49 : h7==19 ? radargeometry::star19 : nullptr;
            for (int i=0;i<7;i++) {
                if (star) { vx[i]=fcx+star[i].x; vy[i]=fcy+star[i].y; }
                else { const double t=i*2.0*3.14159265/7.0;
                    vx[i]=fcx+(int)(h7*std::cos(t)); vy[i]=fcy+(int)(h7*std::sin(t)); }
            }
            int kk=0; for (int n=0;n<7;n++){ const int nk=(kk+3)%7; line(s,vx[kk],vy[kk],vx[nk],vy[nk],pal::EDGE_LIT); kk=nk; }
        }
        if (!clean && sc.animateSweep) {
            const double angle = ((int)sweepPhase(sc.now)*5-90)*3.14159265/180.0;
            const int length = fr*4/5;
            line(s,fcx,fcy,fcx+(int)(length*std::cos(angle)),fcy+(int)(length*std::sin(angle)),pal::WITCH);
        } else if(!clean) line(s, fcx, fcy, fcx + fr*4/5, fcy - fr*7/20, pal::WITCH);
        if(!cvlt && !mascot && !sigil && !logo){box(s, fcx-5, fcy, 11, 1, pal::BRIGHT); box(s, fcx, fcy-5, 1, 11, pal::BRIGHT);}  // centre cross
        for (int i=0;i<sc.nBlips;i++){                      // pre-projected contacts -> field markers
            const FieldBlip& b = sc.blips[i];
            const Marker m = placeMarker(b.bearing, b.dist, b.band, sc.now - b.lastSeen, fr);
            if (m.shown && m.band>=0 && m.band<3) {    // small SQUARE markers, not round dots
                const int sq = m.sizePx; if(cvlt || mascot || orbit || sigil || logo)box(s,fcx+m.dx-sq/2-1,fcy+m.dy-sq/2-1,sq+2,sq+2,pal::VOID); box(s, fcx+m.dx-sq/2, fcy+m.dy-sq/2, sq, sq, band[m.band]);
            }
        }
    }

    // spectrum (design #1, readable): ONE bar per channel, 1:1 with the fixed
    // 21-channel plan (bar i = channels[i], no resampling/clumping). The colour
    // split is the TRUE band boundary -- bars 0..kCh24-1 (13) are 2.4 GHz
    // (MAGENTA), kCh24..20 (8) are 5 GHz (ICE), so the divider lands at bar 13.
    // Width/pitch derive from the spectrum band so it fits 480 and 320 alike; a
    // thin divider + a label under each group read it as data, not vibe.
    const int sx0   = hs.spectrum.x;
    const int pitch = (hs.spectrum.w / kChannels >= 2) ? hs.spectrum.w / kChannels : 2;
    const int bw    = (pitch > 2) ? pitch - 2 : 1;             // readable bar, small gap (don't merge)
    if (sc.channels && sc.nChannels == kChannels)
        for (int i = 0; i < kChannels; i++) {
            int a = sc.channels[i]; a = a < 0 ? 0 : (a > 100 ? 100 : a);
            const int x = sx0 + i * pitch, hgt = a * hs.barMax / 100;
            box(s, x, hs.barBase - hgt, bw, hgt, i < kCh24 ? pal::MAGENTA : pal::ICE);
        }
    const int boundX = sx0 + kCh24 * pitch;                   // 2.4 | 5 GHz split (start of bar 13)
    box(s, boundX - 1, hs.barBase - hs.barMax, 1, hs.barMax, pal::EDGE);   // thin band divider
    const int labY = hr.y + hr.h - 8;
    txt(s, sx0,        labY, "2.4 GHZ", 1, pal::DIM);          // under the 2.4 GHz (pink) group
    txt(s, boundX + 2, labY, "5 GHZ",   1, pal::DIM);          // under the 5 GHz (cyan) group
    // nav bar + STOP/REC drawn by drawChrome above (shared design-1 chrome).
}

// --- strip compositor: small buffer, compose-then-emit ------------------------
class Compositor {
public:
    static constexpr int MAXW    = 480;
    static constexpr int STRIP_H = 16;
    static constexpr int BUF_BYTES = MAXW * STRIP_H * 2;   // 15,360

    void renderRows(int W, int H, const ScanScene& sc, int y0, int y1, Sink& sink) {
        if (y0 < 0) y0 = 0; if (y1 > H) y1 = H;
        for (int sy = y0; sy < y1; sy += STRIP_H) {
            const int sh = (sy + STRIP_H > y1) ? (y1 - sy) : STRIP_H;
            S s{buf_, W, sy, sh};
            composeStrip(s, W, H, sc);        // compose the whole strip from scene
            sink.strip(0, sy, W, sh, buf_);   // then hand it off, once
        }
    }
    void renderFull(int W, int H, const ScanScene& sc, Sink& sink) { renderRows(W,H,sc,0,H,sink); }
    void renderRegion(int W, int H, const ScanScene& sc, uieng::Rect r, Sink& sink) {
        renderRows(W, H, sc, (r.y/STRIP_H)*STRIP_H, r.y + r.h, sink);   // snap to strip grid
    }
private:
    uint16_t buf_[MAXW * STRIP_H];
};

}  // namespace scanr
