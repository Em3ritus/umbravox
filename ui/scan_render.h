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
#include "font5x7.h"
#include <cmath>
#include <cstdint>

namespace scanr {

// Fully-composed RGB565 strip output. Device pushes to the panel; host collects.
struct Sink {
    virtual void strip(int x, int y, int w, int h, const uint16_t* px) = 0;
    virtual ~Sink() {}
};

// What the static SCAN view shows. Strings are caller-owned.
struct ScanScene {
    const char* gps  = "3D FIX"; uint16_t gpsCol = 0;
    const char* mesh = "3 NODES";
    const char* batt = "78%";
    const char* log  = "STOP";   uint16_t logCol = 0;
    long wifi = 1665, g5 = 418, ble = 2903;
    const char* ch     = "CH 06";
    const char* chBand = "2.4 GHz";
};

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

// --- compose one strip completely from the scene ------------------------------
inline void composeStrip(S& s, int W, int H, const ScanScene& sc) {
    const uieng::ScanLayout L = uieng::scan(W, H);
    const uint16_t band[3] = { pal::MAGENTA, pal::ICE, pal::GREEN };
    box(s, 0, 0, W, H, pal::VOID);                                   // 1) background

    // 2) status bar
    static const char* const slab[4] = {"GPS","MESH","BATT","LOG"};
    const char* sval[4] = { sc.gps, sc.mesh, sc.batt, sc.log };
    const uint16_t scol[4] = { sc.gpsCol?sc.gpsCol:pal::GREEN, pal::TEXT, pal::TEXT,
                               sc.logCol?sc.logCol:pal::RED };
    for (int i=0;i<4;i++){ const uieng::Rect c=L.statusCells[i];
        txt(s, c.x+6, c.y+6,  slab[i], 1, pal::DIM);
        int vs = fitScale(sval[i], c.w-10, 2);
        txt(s, c.x+6, c.y+18, sval[i], vs, scol[i]); }
    box(s, 0, 43, W, 1, pal::EDGE);

    // 3) counts tiles
    static const char* const tlab[3] = {"WIFI","5 GHZ","BLE"};
    char num[16]; const long vals[3] = { sc.wifi, sc.g5, sc.ble };
    for (int i=0;i<3;i++){ const uieng::Rect t=L.tiles[i];
        box(s, t.x, t.y, t.w, t.h, pal::PANEL);
        box(s, t.x, t.y, 4, t.h, band[i]);
        txt(s, t.x+12, t.y+8, tlab[i], 1, pal::DIM);
        snprintf(num, sizeof num, "%ld", vals[i]);
        int ns = fitScale(num, t.w-14, t.h>=60?3:2);
        txt(s, t.x+12, t.y + (t.h>=60?30:22), num, ns, pal::BRIGHT); }

    // 4) hero: scanning readout + heptagram foundation + static spectrum
    const uieng::Rect hr = L.hero;
    txt(s, hr.x+6, hr.y+6,  "SCANNING", 1, pal::DIM);
    txt(s, hr.x+6, hr.y+16, sc.ch, 2, pal::BRIGHT);
    txt(s, hr.x+6+Font::width(sc.ch,2)+8, hr.y+22, sc.chBand, 1, pal::DIM);
    const int cx = hr.x + hr.w/2, cy = hr.y + hr.h/2 + 8;
    const int rad = (hr.w < hr.h ? hr.w : hr.h)/2 - 22;
    if (rad > 8) {
        int vx[7], vy[7];
        for (int i=0;i<7;i++){ double a=(-90.0 + i*360.0/7)*3.14159265/180.0;
            vx[i]=cx+(int)(rad*std::cos(a)); vy[i]=cy+(int)(rad*std::sin(a)); }
        for (int a=0;a<360;a+=5){ double r=a*3.14159265/180.0;
            pt(s, cx+(int)(rad*std::cos(r)), cy+(int)(rad*std::sin(r)), pal::GRID); }
        int k=0; for (int n=0;n<7;n++){ int nk=(k+3)%7; line(s, vx[k],vy[k], vx[nk],vy[nk], pal::EDGE_LIT); k=nk; }
    }
    // static spectrum along the hero's foot
    const int bw=8, gap=3, by=hr.y+hr.h-4;
    static const int ph[22]={6,12,20,34,16,9,14,24,8,18,28,11,21,13,26,10,22,7,19,12,24,9};
    for (int i=0, x=hr.x+4; i<22 && x+bw<=hr.x+hr.w-2; ++i, x+=bw+gap)
        box(s, x, by-ph[i], bw, ph[i], i<13 ? pal::MAGENTA : pal::ICE);

    // 5) nav bar
    box(s, 0, H-44, W, 1, pal::EDGE);
    static const char* const nav[4] = {"SCAN","DOMAIN","CODEX","MENU"};
    for (int i=0;i<4;i++){ const uieng::Rect c=L.navCells[i];
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
