// scan_render.h — static SCAN compositor through a small RGB565 strip buffer.
//
// Regions come from uieng::scan(W,H); colors come from the existing theme model
// (pal::, set by pal::setTheme) -- this adds a renderer, it does NOT touch
// theme-pack loading/authoring. The screen is painted as horizontal strips: each
// strip is composed COMPLETELY from scene state (background first, then every
// element clipped into it), then handed to a Sink once. No full framebuffer, no
// animation. Host and device compilable (no Arduino deps).
#pragma once
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
enum class Rec : uint8_t { Off, On, NoCard };

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
        case Rec::On:     snprintf(buf,n,"REC");   return pal::RED;
        case Rec::NoCard: snprintf(buf,n,"NO SD"); return pal::AMBER;
        default:          snprintf(buf,n,"STOP");  return pal::DIM;
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
    for (int y=cy-r;y<=cy+r;y++) for (int x=cx-r;x<=cx+r;x++)
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
    for (int y=cy-r;y<=cy+r;y++){ const int dy=y-cy;
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
static constexpr int kChannels = 21, kCh24 = 13;   // 13 x 2.4GHz + 8 x 5GHz
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

// --- compose one strip completely from the scene ------------------------------
inline void composeStrip(S& s, int W, int H, const ScanScene& sc) {
    const uieng::ScanLayout L = uieng::scan(W, H);
    const uint16_t band[3] = { pal::MAGENTA, pal::ICE, pal::GREEN };
    box(s, 0, 0, W, H, pal::VOID);                                   // body

    // chrome surfaces: status + nav get their own fill + divider
    box(s, 0, 0, W, 44, pal::PANEL);   box(s, 0, 43, W, 1, pal::EDGE);
    box(s, 0, H-44, W, 44, pal::PANEL); box(s, 0, H-44, W, 1, pal::EDGE);

    // status bar: quiet label over an emphasised value, from scene state
    static const char* const slab[4] = {"GPS","MESH","BATT","LOG"};
    char gbuf[16], rbuf[16], mbuf[16], bbuf[16];
    const uint16_t gcol = gpsLabel(sc.gps, gbuf, sizeof gbuf);
    const uint16_t rcol = recLabel(sc, rbuf, sizeof rbuf);
    snprintf(mbuf, sizeof mbuf, "%d NODES", sc.nodes);
    if (sc.hasCell) snprintf(bbuf, sizeof bbuf, "%d%%", sc.battPct); else snprintf(bbuf, sizeof bbuf, "N/A");
    const char* sval[4] = { gbuf, mbuf, bbuf, rbuf };
    const uint16_t scol[4] = { gcol, pal::TEXT, sc.hasCell?pal::TEXT:pal::DIM, rcol };
    for (int i=0;i<4;i++){ const uieng::Rect c=L.statusCells[i];
        if (i) box(s, c.x, c.y+9, 1, 26, pal::EDGE);                  // cell divider
        txt(s, c.x+8, c.y+7,  slab[i], 1, pal::DIM);
        int vs = fitScale(sval[i], c.w-12, 2);
        txt(s, c.x+8, c.y+19, sval[i], vs, scol[i]); }

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

    const HeroSub hs = heroSub(hr);
    const uieng::Rect fld = hs.field;
    const int fcx=fld.x+fld.w/2, fcy=fld.y+fld.h/2;
    const int fr=((fld.w<fld.h?fld.w:fld.h)/2) - 6;
    frame(s, fld.x, fld.y, fld.w, fld.h, pal::EDGE);
    if (fr>6) {
        drawArtwork(s, sc.theme, fcx, fcy, fr);
        // design #1: a static sweep ray (centre -> 0.8r, -0.35r) and a bright
        // centre cross (+/-5px) over the seal.
        line(s, fcx, fcy, fcx + fr*4/5, fcy - fr*7/20, pal::WITCH);
        box(s, fcx-5, fcy, 11, 1, pal::BRIGHT);
        box(s, fcx, fcy-5, 1, 11, pal::BRIGHT);
        for (int i=0;i<sc.nBlips;i++){                      // pre-projected contacts -> field markers
            const FieldBlip& b = sc.blips[i];
            const Marker m = placeMarker(b.bearing, b.dist, b.band, sc.now - b.lastSeen, fr);
            if (m.shown && m.band>=0 && m.band<3) {    // proposal: small SQUARE markers, not round dots
                const int sq = m.sizePx; box(s, fcx+m.dx-sq/2, fcy+m.dy-sq/2, sq, sq, band[m.band]);
            }
        }
    }

    // spectrum: caller-supplied channel activity on a FIXED channel plan (count
    // is size-independent); bar width/pitch adapt. Bars stay in the spectrum band.
    const int sx0=hs.spectrum.x, availW=hs.spectrum.w;
    // Proposal mockup: THIN bars with a clear gap -- about half the pitch, not
    // pitch-2. Bar starts at the same column (sx0 + i*pitch) so the fixed plan
    // and the oracle-aligned spectrum band are unchanged.
    const int pitch=availW/kChannels, bw=pitch>=4 ? pitch/2 : (pitch>1?pitch-1:1);
    if (sc.channels && sc.nChannels==kChannels)
        for (int i=0;i<kChannels;i++){ int a=sc.channels[i]; a=a<0?0:(a>100?100:a);
            const int x=sx0+i*pitch, hgt=a*hs.barMax/100;
            box(s, x, hs.barBase-hgt, bw, hgt, i<kCh24 ? pal::MAGENTA : pal::ICE); }
    txt(s, sx0, hr.y+hr.h-8, "2.4 GHz", 1, pal::DIM);
    txt(s, sx0+availW-Font::width("5 GHz",1), hr.y+hr.h-8, "5 GHz", 1, pal::DIM);

    // nav bar
    static const char* const nav[4] = {"SCAN","DOMAIN","CODEX","MENU"};
    for (int i=0;i<4;i++){ const uieng::Rect c=L.navCells[i];
        if (i) box(s, c.x, c.y+11, 1, 22, pal::EDGE);
        int ns = fitScale(nav[i], c.w-8, 2);
        centered(s, c.x+c.w/2, c.y+14, nav[i], ns, i==0?pal::BRIGHT:pal::DIM);
        if (i==0){ int lw=Font::width(nav[i],ns); box(s, c.x+c.w/2-lw/2, H-8, lw, 2, pal::WITCH_HI); } }
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
