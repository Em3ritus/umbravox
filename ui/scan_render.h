// scan_render.h — static SCAN compositor through a small RGB565 strip buffer.
//
// Regions come from uieng::scan(W,H); colors come from the existing theme model
// (pal::, set by pal::setTheme) -- this adds a renderer, it does NOT touch
// theme-pack loading/authoring. The screen is painted as horizontal strips: each
// strip is composed COMPLETELY from scene state (background first, then every
// element clipped into it), then handed to a Sink once. No full framebuffer, no
// animation. Host and device compilable (no Arduino deps).
#pragma once
#include "radar_geometry.h"
#include "layout_engine.h"
#include "palette.h"
#include "themepack.h"
#include "font5x7.h"
#include "gnss.h"
#include <cmath>
#include <cstdint>
#include <cstdio>

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
inline void emblemInto(S& s, const themepack::Emblem& e, int cx, int cy, int r) {   // same mask/scale as motif::drawEmblem
    if (r<=0 || !e.w || !e.h || !e.px) return;
    const int d=2*r;
    for (int y=std::max(cy-r,s.y0);y<=std::min(cy+r,s.y0+s.h-1);y++){ const int dy=y-cy;
        for (int x=cx-r;x<=cx+r;x++){ const int dx=x-cx; if(dx*dx+dy*dy>r*r) continue;
            int sx=(x-(cx-r))*e.w/d, sy=(y-(cy-r))*e.h/d;
            if(sx<0)sx=0; else if(sx>=e.w)sx=e.w-1; if(sy<0)sy=0; else if(sy>=e.h)sy=e.h-1;
            pt(s,x,y, e.pal[e.px[sy*e.w+sx]]); } }
}
// Honor the theme's artwork, same policy as motif::drawMotif: a bitmap emblem
// when the theme carries one, else the {7/3} heptagram.
inline void drawArtwork(S& s, uint8_t theme, int cx, int cy, int r) {
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
    const int hdrH = 30, specH = 40;               // header clears the readout (text max y+26)
    HeroSub h;
    h.field    = {hr.x+2, hr.y+hdrH, hr.w-4, hr.h-hdrH-specH};
    h.spectrum = {hr.x+4, hr.y+hr.h-specH, hr.w-8, specH};
    h.barBase  = hr.y+hr.h-11;
    h.barMax   = specH-16;                          // tallest fixture bar == barMax (24)
    return h;
}

// Shared geometry for drawing and narrow animation invalidation.
inline uieng::Rect radarBounds(int W, int H) {
    const auto f = heroSub(uieng::scan(W,H).hero).field;
    const int cx = W>=400 ? 311 : f.x+f.w/2;
    const int cy = W>=400 ? 143 : f.y+f.h/2;
    const int r = W>=400 ? 69 : std::min(f.w,f.h)/2-6;
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
};
inline void drawChrome(S& s, int W, int H, const uieng::Rect& body,
                       const ChromeState& st, int activeTab) {
    const uieng::Chrome c = uieng::chrome(W, H);
    // lay the full VOID ground (every screen's body sits on this), then the
    // status/nav surfaces + design #1 inset dividers (body-relative)
    box(s, 0, 0, W, H, pal::VOID);
    box(s, 0, 0, W, 44, pal::PANEL);
    box(s, 0, H - 44, W, 44, pal::PANEL);
    box(s, body.x, 38, body.w, 1, pal::EDGE);
    box(s, body.x, H - 46, body.w, 1, pal::EDGE);

    // header: UMBRAVOX wordmark + single-line GPS / MESH / BAT. GPS is compacted
    // to "GPS 3D" here (so the shared gpsLabel is untouched); stale/acquiring
    // fall back to its text.
    txt(s, c.wordmark.x, c.wordmark.y, "UMBRAVOX", (W >= 400) ? 2 : 1, pal::BRIGHT);
    char hb[24];
    const GpsState gst = gpsState(st.gps);
    const uint16_t gcol = gst==GpsState::Fresh ? pal::GREEN : gst==GpsState::Stale ? pal::AMBER : pal::DIM;
    if (gst==GpsState::Fresh)
        snprintf(hb, sizeof hb, "GPS %s", st.gps.mode==3?"3D":st.gps.mode==2?"2D":"FIX");
    else snprintf(hb, sizeof hb, "%s", gst==GpsState::Stale ? "GPS STALE" : "GPS WAIT");
    txt(s, c.statusCells[0].x+2, 16, hb, 1, gcol);
    snprintf(hb, sizeof hb, "MESH %d", st.nodes);
    txt(s, c.statusCells[1].x+2, 16, hb, 1, pal::TEXT);
    if (st.hasCell) snprintf(hb, sizeof hb, "BAT %d%%", std::max(0,std::min(100,st.batt)));
    else snprintf(hb, sizeof hb, "BAT N/A");
    txt(s, c.statusCells[2].x+2, 16, hb, 1, st.hasCell ? pal::TEXT : pal::DIM);

    // nav: four tabs (activeTab lit + underlined) at the design-1 label positions
    static const char* const nav[4] = {"SCAN","DOMAIN","CODEX","MENU"};
    static const int navX480[4] = {16,112,226,328};
    for (int i=0;i<4;i++){
        const int ns = (W>=400) ? 2 : fitScale(nav[i], c.navCells[i].w-6, 2);
        const int lx = (W>=400) ? navX480[i] : c.navCells[i].x + 4;
        const bool on = (i==activeTab);
        txt(s, lx, H-30, nav[i], ns, on?pal::BRIGHT:pal::DIM);
        if (on){ const int lw=Font::width(nav[i],ns); box(s, lx, H-8, lw, 2, pal::WITCH_HI); }
    }
    // RECORD / STOP actions, SAVING feedback, and an unwritten-row warning. The
    // palette has no dedicated STOP role yet, so map to the theme's red (closest
    // theme-as-data, swaps with theme); a NO-SD state reads amber.
    const uieng::Rect stp = c.stop;
    const int bbw = stp.w < 56 ? stp.w : 56, bbh = 30;
    const int bx = stp.x + (stp.w - bbw)/2, by = stp.y + (stp.h - bbh)/2;
    ScanScene tmp; tmp.rec = st.rec;
    char rb2[16]; recLabel(tmp, rb2, sizeof rb2);     // action or saving/card status
    const uint16_t ink = (st.rec==Rec::NoCard || st.rec==Rec::Saving || st.rec==Rec::Incomplete) ? pal::AMBER : pal::RED;
    box(s, bx, by, bbw, bbh, pal::PANEL);
    frame(s, bx, by, bbw, bbh, ink);
    centered(s, bx + bbw/2 + (st.rec==Rec::On ? 3 : 0), by + (st.rec==Rec::Incomplete ? 5 : bbh/2 - 3), rb2, 1, ink);
    if (st.rec==Rec::Incomplete) centered(s, bx+bbw/2, by+18, "UNSAVED", 1, pal::AMBER);
    if (st.rec==Rec::On) box(s, bx+5, by+13, 3, 3, ink);
}

// --- compose one strip completely from the scene ------------------------------
inline void composeStrip(S& s, int W, int H, const ScanScene& sc) {
    const uieng::ScanLayout L = uieng::scan(W, H);
    const uint16_t band[3] = { pal::MAGENTA, pal::ICE, pal::GREEN };
    box(s, 0, 0, W, H, pal::VOID);                                   // body

    // shared design-1 chrome: header (wordmark + GPS/MESH/BAT) + nav (SCAN lit)
    // + the STOP/REC record control. SCAN passes its own 16px body for the dividers.
    drawChrome(s, W, H, L.body, {sc.gps, sc.nodes, sc.hasCell, sc.battPct, sc.rec}, 0);

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
    // Proposal mockup (scan page): "LIVE FIELD" at the left, the channel at the
    // right -- no "SCANNING" / band line. "LIVE FIELD" is the contract rev-3 name
    // for this view.
    const uieng::Rect hr = L.hero;
    txt(s, hr.x+4, hr.y+6, "LIVE FIELD", 1, pal::DIM);
    txt(s, hr.x + hr.w - 4 - Font::width(sc.ch,2), hr.y+4, sc.ch, 2, pal::BRIGHT);  // channel, right

    const auto bt=L.bridge;
    box(s,bt.x,bt.y,bt.w,bt.h,pal::PANEL);
    frame(s,bt.x,bt.y,bt.w,bt.h,sc.bridgeOn?pal::WITCH_HI:pal::EDGE_LIT);
    centered(s,bt.x+bt.w/2,bt.y+8,"BRIDGE",1,pal::DIM);
    centered(s,bt.x+bt.w/2,bt.y+24,sc.bridgeOn?"ON":"OFF",1,sc.bridgeOn?pal::WITCH_HI:pal::BRIGHT);

    const HeroSub hs = heroSub(hr);
    const uieng::Rect fld = hs.field;
    // design #1 radar is centred (311,143) r69 at 480 (pixel-exact); 320 derives.
    const int fcx = (W>=400) ? 311 : (fld.x+fld.w/2);
    const int fcy = (W>=400) ? 143 : (fld.y+fld.h/2);
    const int fr  = (W>=400) ? 69  : (((fld.w<fld.h?fld.w:fld.h)/2) - 6);
    // design #1: a CIRCULAR radar -- NO bounding box. Three thin 1px rings at
    // radii 23/46/69 (scaled to fr) in the chrome tone, a DELICATE heptagram in
    // the seal tone (EDGE_LIT = #574465) at 0.72r with verts from east, a sweep
    // ray, and a bright centre cross.
    if (fr>6 && fcy+fr >= s.y0 && fcy-fr < s.y0+s.h) {
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
        const themepack::Emblem* e = pal::emblem(sc.theme);
        if (e && pal::centrepiece(sc.theme) == pal::CP_EMBLEM) {
            emblemInto(s, *e, fcx, fcy, fr*72/100);         // bitmap themes keep their emblem
        } else {
            const int h7=fr*72/100; int vx[7], vy[7];       // delicate heptagram, verts from east
            const auto* star = h7==49 ? radargeometry::star49 : h7==19 ? radargeometry::star19 : nullptr;
            for (int i=0;i<7;i++) {
                if (star) { vx[i]=fcx+star[i].x; vy[i]=fcy+star[i].y; }
                else { const double t=i*2.0*3.14159265/7.0;
                    vx[i]=fcx+(int)(h7*std::cos(t)); vy[i]=fcy+(int)(h7*std::sin(t)); }
            }
            int kk=0; for (int n=0;n<7;n++){ const int nk=(kk+3)%7; line(s,vx[kk],vy[kk],vx[nk],vy[nk],pal::EDGE_LIT); kk=nk; }
        }
        if (sc.animateSweep) {
            const double angle = ((int)sweepPhase(sc.now)*5-90)*3.14159265/180.0;
            const int length = fr*4/5;
            line(s,fcx,fcy,fcx+(int)(length*std::cos(angle)),fcy+(int)(length*std::sin(angle)),pal::WITCH);
        } else line(s, fcx, fcy, fcx + fr*4/5, fcy - fr*7/20, pal::WITCH);
        box(s, fcx-5, fcy, 11, 1, pal::BRIGHT); box(s, fcx, fcy-5, 1, 11, pal::BRIGHT);  // centre cross
        for (int i=0;i<sc.nBlips;i++){                      // pre-projected contacts -> field markers
            const FieldBlip& b = sc.blips[i];
            const Marker m = placeMarker(b.bearing, b.dist, b.band, sc.now - b.lastSeen, fr);
            if (m.shown && m.band>=0 && m.band<3) {    // small SQUARE markers, not round dots
                const int sq = m.sizePx; box(s, fcx+m.dx-sq/2, fcy+m.dy-sq/2, sq, sq, band[m.band]);
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
