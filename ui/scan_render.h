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
// A strip surface: rows [y0,y0+h) of a W-wide frame. By default it is the full width. A column window
// [lo,hi) makes the buffer hold ONLY those columns (packed, stride hi-lo) while every primitive keeps
// using absolute frame coordinates, so the same drawing code redraws just one region of the screen.
struct S {
    uint16_t* px; int W, y0, h;
    int lo, hi, stride;
    S(uint16_t* p, int w, int y, int hh, int xlo = 0, int xhi = -1)
        : px(p), W(w), y0(y), h(hh), lo(xlo), hi(xhi < 0 ? w : xhi), stride(hi - lo) {}
};
inline void pt(S& s, int x, int y, uint16_t c) {
    if (x < s.lo || x >= s.hi || y < s.y0 || y >= s.y0 + s.h) return;
    s.px[(y - s.y0) * s.stride + (x - s.lo)] = c;
}
inline void box(S& s, int rx, int ry, int rw, int rh, uint16_t c) {
    int x0 = rx<s.lo?s.lo:rx, x1 = rx+rw>s.hi?s.hi:rx+rw;
    int y0 = ry<s.y0?s.y0:ry, y1 = (ry+rh>s.y0+s.h)?s.y0+s.h:ry+rh;
    for (int y=y0; y<y1; y++){ uint16_t* r=s.px+(y-s.y0)*s.stride; for (int x=x0;x<x1;x++) r[x-s.lo]=c; }
}
inline void txt(S& s, int gx, int gy, const char* t, int sc, uint16_t fg) {
    if (gy >= s.y0+s.h || gy+Font::H*sc <= s.y0) return;
    if (s.lo > 0 || s.hi < s.W) { int n = 0; while (t[n]) n++; if (gx >= s.hi || gx + n*Font::ADV*sc <= s.lo) return; }   // a window skips whole labels
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
// A count for a tile: the full number up to 99,999, then 106K, 1.2M (never wider than 5 glyphs).
inline void fmtCount(long v, char* out, int n) {
    if (v < 0) v = 0;
    if (v < 100000) snprintf(out, n, "%ld", v);
    else if (v < 1000000) snprintf(out, n, "%ldK", v / 1000);
    else { const long t = v / 100000; snprintf(out, n, "%ld.%ldM", t / 10, t % 10); }
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
// --- sweep geometry -----------------------------------------------------------
// The head angle is continuous (1/1024 turn, 0 = up, clockwise) and driven by the
// millisecond clock, so the sweep moves as smoothly as frames arrive. The tail is
// a 90-degree wedge behind the head whose brightness falls off with angular
// distance. Integer cross-products only inside the strip: no atan2, sqrt,
// allocations or framebuffer-sized scratch per pixel.
static constexpr uint32_t kSweepPeriodMs = 3000;
static constexpr int      kTailLevels    = 32;
inline uint16_t sweepAngle(uint32_t now) { return (uint16_t)((now % kSweepPeriodMs) * 1024u / kSweepPeriodMs); }
struct SweepDir { int lx, ly, tx, ty; };   // head and tail-end directions, x1024
inline SweepDir sweepDir(uint16_t a) {
    const double t = a * 2.0 * 3.14159265358979 / 1024.0 - 3.14159265358979 / 2.0;
    return { int(std::cos(t) * 1024), int(std::sin(t) * 1024), int(std::sin(t) * 1024), int(-std::cos(t) * 1024) };
}
// 0 = outside the tail, else 1..levels (levels = at the head).
inline int trailLevel(const SweepDir& d, int dx, int dy, int levels) {
    const int c0 = d.tx * dy - d.ty * dx, c1 = dx * d.ly - dy * d.lx;
    if (c0 < 0 || c1 < 0 || c0 + c1 <= 0) return 0;
    const int f = c0 * 1024 / (c0 + c1);                              // 0 at the tail end, 1024 at the head (32-bit: the C5 has no 64-bit divide)
    return 1 + ((f * f >> 10) * (levels - 1) >> 10);                  // quadratic: bright head, long soft tail
}
// Bounding box (relative to the centre) of the tail wedge and needle: the centre, both edge tips and any
// compass point the arc passes through.
inline void wedgeBox(const SweepDir& d, int r, int& bx0, int& by0, int& bx1, int& by1) {
    bx0 = by0 = bx1 = by1 = 0;
    auto grow = [&](int x, int y) { if (x < bx0) bx0 = x; if (x > bx1) bx1 = x; if (y < by0) by0 = y; if (y > by1) by1 = y; };
    grow(r * d.lx / 1024, r * d.ly / 1024); grow(r * d.tx / 1024, r * d.ty / 1024);
    static const int card[4][2] = {{1, 0}, {0, 1}, {-1, 0}, {0, -1}};
    for (const auto& c : card) if (trailLevel(d, c[0] * r, c[1] * r, 2)) grow(c[0] * r, c[1] * r);
}
// Every pixel the sweep (tail + needle) can touch at this head angle, in frame coordinates.
inline uieng::Rect sweepBounds(int cx, int cy, int r, uint16_t angle) {
    int bx0, by0, bx1, by1; wedgeBox(sweepDir(angle), r, bx0, by0, bx1, by1);
    return { cx + bx0 - 8, cy + by0 - 8, bx1 - bx0 + 17, by1 - by0 + 17 };   // 8 px margin: a flaring contact's halo
}
// Live (animated) contact look: larger than the static marker, and lit by the sweep. `flare` is the tail
// level over the contact (1..kTailLevels while the sweep has just passed it, 0 otherwise).
struct BlipLook { int size; int flare; };
inline BlipLook blipLook(const Marker& m, const SweepDir& d, int W) {
    return { m.sizePx + (W >= 400 ? 2 : 1), trailLevel(d, m.dx, m.dy, kTailLevels) };
}
inline void radarTrail(S& s, int cx, int cy, int r, uint16_t angle) {
    if (r < 1 || cy + r < s.y0 || cy - r >= s.y0 + s.h) return;
    const SweepDir d = sweepDir(angle);
    uint16_t fade[kTailLevels + 1]; fade[0] = pal::VOID;
    for (int k = 1; k <= kTailLevels; k++) fade[k] = ui::Canvas::mix(pal::VOID, pal::SWEEP, k, kTailLevels + 8);
    // The wedge covers about a quarter of the circle: scan only its bounding box, not the whole disc.
    int bx0, by0, bx1, by1; wedgeBox(d, r, bx0, by0, bx1, by1);
    for (int y = std::max(cy + by0 - 2, s.y0); y <= std::min(cy + by1 + 2, std::min(cy + r, s.y0 + s.h - 1)); y++)
        for (int x = std::max(s.lo, std::max(cx - r, cx + bx0 - 2)); x <= std::min(s.hi - 1, std::min(cx + r, cx + bx1 + 2)); x++) {
            const int dx = x - cx, dy = y - cy;
            if (dx * dx + dy * dy > r * r) continue;
            const int lv = trailLevel(d, dx, dy, kTailLevels);
            if (lv) pt(s, x, y, fade[lv]);
        }
}
// The needle: a 3-line (2 px) head drawn AFTER the artwork, so it shows over any emblem, seal or star.
inline void radarNeedle(S& s, int cx, int cy, int r, uint16_t angle) {
    if (r < 1 || cy + r < s.y0 || cy - r >= s.y0 + s.h) return;
    const SweepDir d = sweepDir(angle);
    const int ex = cx + r * d.lx / 1024, ey = cy + r * d.ly / 1024;
    line(s, cx, cy, ex, ey, pal::SWEEP_HI);
    line(s, cx + 1, cy, ex + 1, ey, pal::SWEEP_HI);
    line(s, cx, cy + 1, ex, ey + 1, pal::SWEEP_HI);
}

inline void emblemInto(S& s, const themepack::Emblem& e, int cx, int cy, int r, bool transparentBlack=false, int opacity=100, bool circular=true, bool smooth=false) {   // same mask/scale as motif::drawEmblem
    if (r<=0 || !e.w || !e.h || !e.px || !e.ncol || e.ncol>16) return;
    if (cx + r < s.lo || cx - r >= s.hi) return;      // outside the column window
    const int d=2*r;
    uint16_t colors[16];for(int i=0;i<e.ncol;i++)colors[i]=ui::Canvas::mix(pal::VOID,e.pal[i],opacity,100);
    for (int y=std::max(cy-r,s.y0);y<=std::min(cy+r,s.y0+s.h-1);y++){ const int dy=y-cy;
        for (int x=cx-r;x<=cx+r;x++){ const int dx=x-cx; if(circular && dx*dx+dy*dy>r*r) continue;
            int sx=(x-(cx-r))*e.w/d, sy=(y-(cy-r))*e.h/d;
            if(sx<0)sx=0; else if(sx>=e.w)sx=e.w-1; if(sy<0)sy=0; else if(sy>=e.h)sy=e.h-1;
            if(smooth && d<e.w){
                // Four area samples per destination pixel; only the small badge,
                // never the radar/labels. Transparent samples use the background.
                uint16_t samples[4];int n=0;bool visible=false;
                for(int oy:{1,3})for(int ox:{1,3}){
                    const int ax=std::min(int(e.w)-1,((x-cx+r)*4+ox)*e.w/(d*4));
                    const int ay=std::min(int(e.h)-1,((y-cy+r)*4+oy)*e.h/(d*4));
                    const int index=themepack::pixel(e,ay*e.w+ax);
                    visible|=bool(e.pal[index]);
                    samples[n++]=transparentBlack && !e.pal[index]?pal::VOID:colors[index];
                }
                if(!transparentBlack || visible)pt(s,x,y,ui::Canvas::mix(
                    ui::Canvas::mix(samples[0],samples[1],1,2),
                    ui::Canvas::mix(samples[2],samples[3],1,2),1,2));
            }else{
                const int index=themepack::pixel(e,sy*e.w+sx);
                if(!transparentBlack || e.pal[index])pt(s,x,y,colors[index]);
            } } }
}
// Precomputed artwork: clipped spans, no heap use and no trigonometry.
inline void crewArt(S& s,int cx,int cy,int radius,int style,int opacity=100){
    const bool small=radius<=39; // never enlarge the 39px source when the 85px asset is available
    const crewart::Span* runs=nullptr;const uint16_t* colors=nullptr;size_t count=0;
    if(style==0 && small){runs=crewart::mascot39;colors=crewart::mascot39Colors;count=sizeof crewart::mascot39/sizeof(*runs);}
    else if(style==0){runs=crewart::mascot85;colors=crewart::mascot85Colors;count=sizeof crewart::mascot85/sizeof(*runs);}
    else if(style==1 && small){runs=crewart::cvlt39;colors=crewart::cvlt39Colors;count=sizeof crewart::cvlt39/sizeof(*runs);}
    else if(style==1){runs=crewart::cvlt85;colors=crewart::cvlt85Colors;count=sizeof crewart::cvlt85/sizeof(*runs);}
    else if(style==2 && small){runs=crewart::orbit39;colors=crewart::orbit39Colors;count=sizeof crewart::orbit39/sizeof(*runs);}
    else if(style==2){runs=crewart::orbit85;colors=crewart::orbit85Colors;count=sizeof crewart::orbit85/sizeof(*runs);}
    else if(style==3 && small){runs=crewart::sigil39;colors=crewart::sigil39Colors;count=sizeof crewart::sigil39/sizeof(*runs);}
    else if(style==3){runs=crewart::sigil85;colors=crewart::sigil85Colors;count=sizeof crewart::sigil85/sizeof(*runs);}
    // Native 55px artwork avoids shrinking away one-pixel orbit/sigil strokes.
    if(radius==55){
      switch(style){
        case 0:runs=crewart::mascot55;colors=crewart::mascot55Colors;count=sizeof crewart::mascot55/sizeof(*runs);break;
        case 1:runs=crewart::cvlt55;colors=crewart::cvlt55Colors;count=sizeof crewart::cvlt55/sizeof(*runs);break;
        case 2:runs=crewart::orbit55;colors=crewart::orbit55Colors;count=sizeof crewart::orbit55/sizeof(*runs);break;
        case 3:runs=crewart::sigil55;colors=crewart::sigil55Colors;count=sizeof crewart::sigil55/sizeof(*runs);break;
      }
    }
    if(!runs)return;
    const int base=radius==55?55:small?39:85;
    for(size_t i=0;i<count;i++){
      const auto& a=runs[i];
      // Map row edges, not only row origins. This fills enlarged rows and
      // samples a single source row on reduction, instead of stacking rows.
      const int y=cy-radius+(a.y+base)*radius/base;
      const int endY=cy-radius+(a.y+base+1)*radius/base;
      if(endY<=s.y0 || endY<=y)continue;
      if(y>=s.y0+s.h)break;
      const int x=cx-radius+(a.x+base)*radius/base;
      const int end=cx-radius+(a.x+base+a.n())*radius/base;
      if(end>x)box(s,x,y,end-x,endY-y,opacity==100?colors[a.c()]:ui::Canvas::mix(pal::VOID,colors[a.c()],opacity,100));
    }
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
// Hero sub-layout. Wide panels (layout A) keep the radar big in the middle and put the channel readout and
// the spectrum in a right-hand column (`side`); compact panels keep the spectrum under the radar.
struct HeroSub { uieng::Rect field, spectrum, side; int barBase, barMax; };
inline HeroSub heroSub(uieng::Rect hr) {
    const bool compact = hr.w < 280;
    const int hdrH = compact ? 16 : 30, specH = compact ? 26 : 44;               // compact uses a 1x channel heading; wide keeps the 2x readout
    HeroSub h{};
    h.field    = {hr.x+2, hr.y+hdrH, hr.w-4, hr.h-hdrH-specH};
    if (!compact) {
        h.side     = {hr.x+hr.w-66, hr.y, 66, hr.h};
        h.barMax   = 56;                                      // a real equaliser, 3 px per channel
        h.barBase  = hr.y+hr.h-14;
        h.spectrum = {h.side.x+1, h.barBase-h.barMax-4, 63, h.barMax+4+14};
        return h;
    }
    const int sw=std::min(140,hr.w-8);
    h.spectrum = {hr.x+(hr.w-sw)/2, hr.y+hr.h-specH, sw, specH};
    h.barBase  = hr.y+hr.h-11;
    h.barMax   = 8;                                           // compact half-height amplitude; every channel retained
    return h;
}

// Shared geometry for drawing and narrow animation invalidation.
inline uieng::Rect radarBounds(int W, int H) {
    const auto hr=uieng::scan(W,H).hero;
    if (W>=400) {                                   // layout A: a big radar, left of the right-hand column
        const int r=102, cx=hr.x+168, cy=hr.y+hr.h/2;
        return {cx-r-2,cy-r-2,2*r+5,2*r+5};
    }
    const auto hs=heroSub(hr);
    const auto f=hs.field;
    const int cx = f.x+f.w/2;
    const int top = hr.y+4, bottom = hs.barBase-hs.barMax-2;
    // Compact panels retain the accepted optical lift.
    const int cy = (top+bottom)/2 - 2;
    const int r = std::min(f.w/2-4,(bottom-top)/2);
    return {cx-r-2,cy-r-2,2*r+5,2*r+5}; // include the largest marker half-size
}
inline uint32_t sweepPhase(uint32_t now) { return uint32_t(sweepAngle(now)) * 72u / 1024u; }   // coarse, kept for callers that want a step count

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
inline void drawChrome(S& s, int W, int H, const uieng::Rect& /*body*/,
                       const ChromeState& st, int activeTab) {
    const uieng::Chrome c = uieng::chrome(W, H);
    // lay the full VOID ground (every screen's body sits on this), then the
    // status/nav surfaces and full-width one-pixel dividers.
    box(s, 0, 0, W, H, pal::VOID);
    box(s, 0, 0, W, 44, pal::PANEL);
    box(s, 0, H - 44, W, 44, pal::PANEL);
    box(s, 0, 43, W, 1, pal::EDGE);
    box(s, 0, H - 46, W, 1, pal::EDGE);

    for(int y:{8,13,18})box(s,14,y,16,2,activeTab==3?pal::BRIGHT:pal::TEXT);
    centered(s,22,28,"MENU",1,activeTab==3?pal::BRIGHT:pal::TEXT);
    if(activeTab==3)box(s,10,39,24,2,pal::WITCH_HI);
    // Fixed title box: theme names fit without moving any controls.
    const char* name=pal::header(st.theme);
    const char* second=nullptr;
    const char* first=name;
    if (!strcmp(name,"CVLT OF BAPH0MAU5")){first="CVLT OF";second="BAPH0MAU5";}
    if (!strcmp(name,"COSMIC MUFFIN") || !strcmp(name,"COSMIC MVFFIN")){first="COSMIC";second="MUFFIN";}
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
        fmtCount(vals[i], num, sizeof num);
        int ns = fitScale(num, t.w-16, t.h>=60?3:2);
        txt(s, t.x+12, t.y + (t.h>=60?28:22), num, ns, pal::BRIGHT); }

    // hero: readout / framed field (theme artwork + contacts) / labelled spectrum.
    // Channel readout and theme identity above the radar.
    const uieng::Rect hr = L.hero;
    // Theme identity occupies the former field heading, independently of radar clipping.
    if (pal::centrepiece(sc.theme)==pal::CP_RADAR_EMBLEM || sc.theme==pal::TH_COSMICMUFFIN) {
        const auto* headingEmblem=pal::emblem(sc.theme);
        const int badgeR=W>=400?27:22;
        if(headingEmblem)emblemInto(s,*headingEmblem,hr.x+4+badgeR,hr.y+4+badgeR,badgeR,true,pal::artOpacity(sc.theme),false,true);
    }
    const int channelScale = W>=400 ? 2 : 1;
    txt(s, hr.x + hr.w - 4 - Font::width(sc.ch,channelScale), hr.y+4, sc.ch, channelScale, pal::BRIGHT);  // channel, right

    const HeroSub hs = heroSub(hr);
    // Center between the top inset and spectrum; retain the approved radar sizes.
    const auto radar = radarBounds(W,H);
    const int fr = (radar.w-5)/2;
    const int fcx = radar.x+fr+2;
    const int fcy = radar.y+fr+2;
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
        if(clean||sc.animateSweep)radarTrail(s,fcx,fcy,fr,sc.animateSweep?sweepAngle(sc.now):uint16_t(384));   // every theme sweeps with a tail when animated
        for (int k=1;k<=3;k++){
            if(cvlt && k==1)continue; // two range rings leave the quiet seal uncluttered
            const int rr=fr*(k*23)/69;
            int count=0;
            const auto* ring=radargeometry::ring(rr,count);
            if (ring) {
                const bool thick = W>=400;       // layout A: 2 px rings (each table point also paints the pixel inward)
                auto plot=[&](int x,int y,int cov){
                    if(y<s.y0 || y>=s.y0+s.h || x<s.lo || x>=s.hi)return;
                    auto& dst=s.px[(y-s.y0)*s.stride+(x-s.lo)];
                    dst=ui::Canvas::mix(dst,pal::EDGE,cov,16);
                };
                for (int i=0;i<count;++i) {
                    const int ry=ring[i].y, rx=ring[i].x;
                    if (fcy+ry < s.y0-1) continue;
                    if (fcy+ry >= s.y0+s.h+1) break; // sorted rows (+1: the inward pixel sits one row closer)
                    plot(fcx+rx, fcy+ry, ring[i].coverage);
                    if (thick) {
                        const int ax = rx<0?-rx:rx, ay = ry<0?-ry:ry;
                        const int ix = rx - (ax>=ay ? (rx>0?1:(rx<0?-1:0)) : 0), iy = ry - (ax<ay ? (ry>0?1:(ry<0?-1:0)) : 0);
                        plot(fcx+ix, fcy+iy, ring[i].coverage);
                    }
                }
            } else { // other logical sizes retain the original geometry
                for (int a=0;a<1440;a++){ const double t=a*3.14159265/720.0;
                    pt(s, fcx+(int)(rr*std::cos(t)+0.5), fcy+(int)(rr*std::sin(t)+0.5), pal::EDGE); }
            }
        }
        if(cvlt || mascot || orbit || sigil)crewArt(s,fcx,fcy,fr,cvlt?1:orbit?2:sigil?3:0,100);
        const themepack::Emblem* e = pal::emblem(sc.theme);
        if (e && pal::centrepiece(sc.theme) == pal::CP_EMBLEM && !orbit) {
            emblemInto(s, *e, fcx, fcy, fr*72/100);         // bitmap themes keep their emblem
        } else if(!clean) {
            const int h7=fr*72/100; int vx[7], vy[7];       // delicate heptagram, verts from east
            const auto* star = h7==73 ? radargeometry::star73 : h7==61 ? radargeometry::star61 : h7==49 ? radargeometry::star49 : h7==19 ? radargeometry::star19 : nullptr;
            for (int i=0;i<7;i++) {
                if (star) { vx[i]=fcx+star[i].x; vy[i]=fcy+star[i].y; }
                else { const double t=i*2.0*3.14159265/7.0;
                    vx[i]=fcx+(int)(h7*std::cos(t)); vy[i]=fcy+(int)(h7*std::sin(t)); }
            }
            int kk=0; for (int n=0;n<7;n++){ const int nk=(kk+3)%7; line(s,vx[kk],vy[kk],vx[nk],vy[nk],pal::EDGE_LIT); kk=nk; }
        }
        if(!clean && !sc.animateSweep) line(s, fcx, fcy, fcx + fr*4/5, fcy - fr*7/20, pal::WITCH);   // static ray
        if(clean||sc.animateSweep)radarNeedle(s,fcx,fcy,fr,sc.animateSweep?sweepAngle(sc.now):uint16_t(384));   // over the artwork
        if(cvlt){box(s,fcx-2,fcy,5,1,pal::DIM);box(s,fcx,fcy-2,1,5,pal::DIM);}
        if(!cvlt && !mascot && !sigil && !logo){box(s, fcx-5, fcy, 11, 1, pal::BRIGHT); box(s, fcx, fcy-5, 1, 11, pal::BRIGHT);}  // centre cross
        const SweepDir liveDir = sweepDir(sc.animateSweep ? sweepAngle(sc.now) : uint16_t(0));
        for (int i=0;i<sc.nBlips;i++){                      // pre-projected contacts -> field markers
            const FieldBlip& b = sc.blips[i];
            const Marker m = placeMarker(b.bearing, b.dist, b.band, sc.now - b.lastSeen, fr);
            if (m.shown && m.band>=0 && m.band<3) {    // small SQUARE markers, not round dots
                if (sc.animateSweep) {                  // live: bigger, and flaring while the sweep is on them
                    const BlipLook lk = blipLook(m, liveDir, W);
                    const int sq = lk.size, px = fcx + m.dx, py = fcy + m.dy;
                    uint16_t ink = band[m.band];
                    if (lk.flare) {
                        const uint16_t halo = ui::Canvas::mix(pal::VOID, ink, lk.flare, kTailLevels * 2);
                        box(s, px - sq/2 - 2, py - sq/2, sq + 4, sq, halo);       // plus-shaped glow, soft corners
                        box(s, px - sq/2, py - sq/2 - 2, sq, sq + 4, halo);
                        ink = ui::Canvas::mix(ink, pal::BRIGHT, lk.flare, kTailLevels);
                    }
                    if (cvlt || mascot || orbit || sigil || logo) box(s, px - sq/2 - 1, py - sq/2 - 1, sq + 2, sq + 2, pal::VOID);
                    box(s, px - sq/2, py - sq/2, sq, sq, ink);
                } else {
                    const int sq = m.sizePx; if(cvlt || mascot || orbit || sigil || logo)box(s,fcx+m.dx-sq/2-1,fcy+m.dy-sq/2-1,sq+2,sq+2,pal::VOID); box(s, fcx+m.dx-sq/2, fcy+m.dy-sq/2, sq, sq, band[m.band]);
                }
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
    const int bw    = (pitch > 3) ? pitch - 2 : (pitch > 2 ? pitch - 1 : 1);   // readable bar, small gap (don't merge)
    if (sc.channels && sc.nChannels == kChannels)
        for (int i = 0; i < kChannels; i++) {
            int a = sc.channels[i]; a = a < 0 ? 0 : (a > 100 ? 100 : a);
            const int x = sx0 + i * pitch, hgt = a * hs.barMax / 100;
            box(s, x, hs.barBase - hgt, bw, hgt, i < kCh24 ? pal::MAGENTA : pal::ICE);
        }
    const int boundX = sx0 + kCh24 * pitch;                   // 2.4 | 5 GHz split (start of bar 13)
    box(s, boundX - 1, hs.barBase - hs.barMax, 1, hs.barMax, pal::EDGE);   // thin band divider
    const int labY = hr.y + hr.h - 8;
    const bool shortLab = pitch < 4;                           // the narrow right-hand column
    txt(s, sx0,        labY, shortLab ? "2.4G" : "2.4 GHZ", 1, pal::DIM);   // under the 2.4 GHz (pink) group
    txt(s, boundX + 2, labY, shortLab ? "5G" : "5 GHZ", 1, pal::DIM);   // under the 5 GHz (cyan) group
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
