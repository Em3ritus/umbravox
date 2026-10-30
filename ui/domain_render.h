// domain_render.h -- static DOMAIN compositor + pure projections.
//
// DOMAIN presents recorded observation locations: a primary AP list and a
// secondary north-up overview with a range -/+ control. These are reception
// positions, not inferred transmitter locations.
// Regions come from uieng::domain(W,H); colors come from the existing theme
// model (pal::). Like SCAN, the screen is painted as horizontal strips: each
// strip is composed COMPLETELY from scene state, then handed to a Sink once --
// no full framebuffer, no animation. Host and device compilable (no Arduino).
//
// This reuses scan_render.h for the shared drawing primitives (S/box/txt/line/
// disc/frame/fitScale/centered) and, crucially, the GPS freshness/age/mode and
// recording projections (scanr::gpsLabel/recLabel) -- so the status-bar chrome
// is identical to SCAN and the fresh/stale/acquiring rule stays single-sourced.
#pragma once
#include "domain_range.h"
#include "scan_render.h"      // scanr:: primitives + GnssFix/gpsLabel/recLabel/Sink/S
#include "layout_engine.h"
#include "palette.h"
#include "font5x7.h"
#include "canvas.h"           // ui::Canvas::mix (static color blend; no instance needed)
#include <cstdint>
#include <cstdio>

namespace domr {

using scanr::Sink;
using scanr::GnssFix;
using scanr::Rec;

// --- band color (same legend the whole UI shares) ----------------------------
inline uint16_t bandColor(uint8_t band) {
    switch (band) {
        case 0:  return pal::MAGENTA;   // 2.4 GHz
        case 1:  return pal::ICE;       // 5 GHz
        default: return pal::GREEN;     // BLE
    }
}

// --- list ordering (open decision #5): strongest signal first ----------------
// Policy: order located contacts by RSSI DESCENDING, ties broken by ascending
// stable key. Deterministic -- the same set always yields the same order, so a
// row never reshuffles on its own; the FrozenOrder in domain_present.h holds the
// order steady across a touch so nothing slides under the finger either.
struct LocRec { const char* key; int rssi; };
inline bool locBefore(const LocRec& a, const LocRec& b) {
    if (a.rssi != b.rssi) return a.rssi > b.rssi;                  // stronger first
    const char* x = a.key ? a.key : ""; const char* y = b.key ? b.key : "";
    return __builtin_strcmp(x, y) < 0;                             // then stable key asc
}
// Fill idx[0..n) with a stable (insertion) sort of the records by the policy.
inline void orderLocated(const LocRec* in, int n, int* idx) {
    for (int i = 0; i < n; i++) idx[i] = i;
    for (int i = 1; i < n; i++) {                                  // stable insertion sort
        const int v = idx[i]; int j = i - 1;
        while (j >= 0 && locBefore(in[v], in[idx[j]])) { idx[j + 1] = idx[j]; --j; }
        idx[j + 1] = v;
    }
}

// --- pagination: fixed pages sized to the visible data-row capacity ----------
// `anchor` is the list index we want on screen (selection/scroll); the page is
// the fixed block of `perPage` that contains it. lo/hi/total are 1-based for the
// "lo-hi / total" header label; an empty list reads "0-0 / 0".
struct Page { int first, count, lo, hi, total; };
inline Page paginate(int total, int perPage, int anchor) {
    if (perPage < 1) perPage = 1;
    if (total < 0) total = 0;
    if (anchor < 0) anchor = 0; else if (anchor >= total && total) anchor = total - 1;
    const int first = total ? (anchor / perPage) * perPage : 0;
    int count = total - first; if (count > perPage) count = perPage; if (count < 0) count = 0;
    return Page{ first, count, total ? first + 1 : 0, first + count, total };
}

// --- the drawable scene (caller-owned, stable for the whole sliced redraw) ----
// Map dots and track points arrive PRE-PROJECTED as pixel offsets from the
// radar centre (+y south) -- the device's plotproj.h does the trig, exactly as
// ui::Plot already carries dx,dy. The located ROWS are already ORDERED and
// PAGINATED by the caller (the policy above); the scene carries only the window.
struct MapDot  { int dx, dy; uint8_t band; bool threat; };
struct TrackDot{ int dx, dy; };
struct DomRow { const char* key; const char* label; uint8_t band; int rssi=0; int chan=0; const char* mac=""; };

struct DomainScene {
    // status chrome (projected by scanr::gpsLabel / recLabel, same as SCAN)
    GnssFix  gps;
    int      nodes = 0;
    bool     hasCell = false; int battPct = 0;
    Rec      rec = Rec::Off;
    // map / radar
    const MapDot*   dots  = nullptr; int nDots  = 0;    // located, within range
    const TrackDot* track = nullptr; int nTrack = 0;
    int      plotShown = 0;        // located within range (== nDots on the device)
    int      plotTotal = 0;        // located APs that exist
    int      plotBeyond = 0;       // located, but outside the range
    int      rangeM = 250;
    // located list window at both sizes; header opens the full located CODEX
    const DomRow* rows = nullptr; int nRows = 0;        // this page's rows, in order
    int      pageLo = 0, pageHi = 0, listTotal = 0;     // 1-based "lo-hi / total"
    uint8_t  theme = 0;
    uint32_t now = 0;
};

// --- radar geometry inside the rectangular map (exposed for tests) -----------
// Center the overview in its own region; reserve top/bottom labels.
struct RadarGeom { int cx, cy, r; };
inline RadarGeom radarGeom(uieng::Rect map) {
    const int d = std::min(map.w, map.h - 28);
    return {map.x + map.w/2, map.y + 14 + (map.h-28)/2, std::max(0,d/2-5)};
}

// --- small helpers reusing scanr's strip writer ------------------------------
inline void dottedRing(scanr::S& s, int cx, int cy, int r, uint16_t c) {
    for (int a = 0; a < 360; a += 8) {                  // denser dots: the dial must read even when empty
        const double t = a * 3.14159265 / 180.0;
        scanr::pt(s, cx + (int)(r * std::cos(t)), cy + (int)(r * std::sin(t)), c);
    }
}

// Wrap names at a space when both lines still fit; otherwise preserve all bytes
// with a hard split (e.g. a 32-byte name containing no spaces).
inline int nameBreak(const char* name,int cols) {
    const int n=(int)__builtin_strlen(name);
    if(n<=cols)return n;
    for(int i=cols;i>0;--i)if(name[i]==' ' && n-i-1<=cols)return i;
    return cols;
}

// --- compose one strip completely from the scene -----------------------------
inline void composeStrip(scanr::S& s, int W, int H, const DomainScene& sc) {
    using scanr::box; using scanr::txt; using scanr::frame; using scanr::line;
    using scanr::disc; using scanr::centered; using scanr::fitScale; using scanr::pt;
    const uieng::DomainLayout L = uieng::domain(W, H);

    box(s, 0, 0, W, H, pal::VOID);                                   // body ground

    // shared design-1 chrome: header (wordmark + GPS/MESH/BAT) + nav (DOMAIN lit,
    // index 1) + the STOP/REC record control. Single-sourced in scanr::drawChrome.
    scanr::drawChrome(s, W, H, L.body, {sc.gps, sc.nodes, sc.hasCell, sc.battPct, sc.rec}, 1);

    // --- the map: panel, lattice, N-up radar, track, dots, you, readouts ------
    const uieng::Rect m = L.map;
    box(s, m.x, m.y, m.w, m.h, pal::PANEL);
    frame(s, m.x, m.y, m.w, m.h, pal::EDGE);
    for (int y = m.y + 2; y < m.y + m.h - 2; y++)                    // ground lattice
        for (int x = m.x + 2; x < m.x + m.w - 2; x++)
            if (x % 8 == 0 && y % 8 == 0) pt(s, x, y, pal::GRID);

    const RadarGeom g = radarGeom(m);
    const bool fresh=scanr::gpsState(sc.gps)==scanr::GpsState::Fresh;
    // the dial must read even at 0 contacts; EDGE on the dark map is too faint,
    // so the rings + crosshair use the brighter EDGE_LIT seal tone (geometry
    // unchanged -- rings still at g.r*k/3 = 30/60/91 @480).
    for (int k = 1; k <= 3; k++) dottedRing(s, g.cx, g.cy, g.r * k / 3, pal::EDGE_LIT);
    for (int i = -g.r; i <= g.r; i += 4) {                          // crosshair axes
        pt(s, g.cx + i, g.cy, pal::EDGE_LIT); pt(s, g.cx, g.cy + i, pal::EDGE_LIT);
    }
    txt(s, g.cx - 2, m.y + 16, "N", 1, pal::DIM);

    for (int i = 0; fresh && i < sc.nTrack; i++) {                           // your track, older fades
        if(sc.track[i].dx*sc.track[i].dx+sc.track[i].dy*sc.track[i].dy>g.r*g.r)continue;
        const int x = g.cx + sc.track[i].dx, y = g.cy + sc.track[i].dy;
        const int lum = 70 + (185 * i) / (sc.nTrack > 1 ? sc.nTrack - 1 : 1);
        pt(s, x, y, ui::Canvas::mix(pal::VOID, pal::WITCH, lum, 255));
    }
    for (int i = 0; fresh && i < sc.nDots; i++) {                            // located sightings
        const MapDot& p = sc.dots[i];
        if(p.dx*p.dx+p.dy*p.dy>g.r*g.r)continue;
        const int x = g.cx + p.dx, y = g.cy + p.dy;
        box(s, x - 2, y - 2, 4, 4, bandColor(p.band));              // design #1: 4x4 square markers
        if (p.threat) {                                             // signature match -> ring
            for (int a = 0; a < 360; a += 30) { const double t = a * 3.14159265 / 180.0;
                pt(s, x + (int)(4 * std::cos(t)), y + (int)(4 * std::sin(t)), pal::RED); }
        }
    }
    box(s, g.cx - 3, g.cy, 7, 1, pal::WITCH_HI);                    // you, last and brightest
    box(s, g.cx, g.cy - 3, 1, 7, pal::WITCH_HI);
    pt(s, g.cx, g.cy, pal::BRIGHT);

    char buf[40];
    snprintf(buf, sizeof buf, "%dM", sc.rangeM);

    if(fresh)snprintf(buf,sizeof buf,"%d SHOWN",sc.plotShown);
    else snprintf(buf,sizeof buf,"%s",scanr::gpsState(sc.gps)==scanr::GpsState::Stale?"GPS STALE":"GPS WAIT");
    if(!fresh) centered(s,g.cx,g.cy+12,buf,1,pal::AMBER);
    if(fresh){snprintf(buf,sizeof buf,"%d/%d PTS",sc.plotShown,std::max(0,sc.plotTotal-sc.plotBeyond));
        txt(s,m.x+4,m.y+3,buf,1,pal::DIM);}
    if(fresh && sc.plotTotal==0)centered(s,g.cx,g.cy+14,"NO LOCATIONS",1,pal::DIM);

    // Located observations remain inspectable even with a stale/currently absent fix.
    const uieng::Rect cc = L.contacts;
    box(s,cc.x,cc.y,cc.w,cc.h,pal::PANEL);
    frame(s,cc.x,cc.y,cc.w,cc.h,pal::EDGE);
    txt(s,cc.x+8,cc.y+5,"LOCATED APs",2,pal::BRIGHT);
    snprintf(buf,sizeof buf,"%d APs  /  BROWSE ALL >",sc.listTotal);
    txt(s,cc.x+8,cc.y+23,buf,1,pal::TEXT);
    txt(s,cc.x+8,cc.y+35,"Tap an AP for full details",1,pal::DIM);
    for(int b=0;b<sc.nRows && b<uieng::domainRowBands(L)-1;++b) {
        const auto rr=uieng::domainRow(L,b);
        const int ry=rr.y,rh=rr.h;
        const auto& row=sc.rows[b];
        frame(s,cc.x,ry,cc.w,rh,pal::EDGE);
        box(s,cc.x,ry,3,rh,bandColor(row.band));
        const char* name=row.label?row.label:"<HIDDEN>";
        const int scale=L.wide?2:1, cols=(cc.w-16)/(6*scale);
        const int split=nameBreak(name,cols);
        char title[40];snprintf(title,sizeof title,"%.*s",split,name);
        txt(s,cc.x+8,ry+4,title,scale,pal::BRIGHT);
        if(name[split])
            txt(s,cc.x+8,ry+4+7*scale+2,name+split+(name[split]==' '),scale,pal::BRIGHT);
        txt(s,cc.x+8,ry+rh-20,row.mac?row.mac:"",1,pal::DIM);
        snprintf(buf,sizeof buf,"%s CH%d  %d dBm",row.band==1?"5G":"2.4G",row.chan,row.rssi);
        txt(s,cc.x+8,ry+rh-10,buf,1,bandColor(row.band));
    }
    if(!sc.nRows) {
        txt(s,cc.x+8,cc.y+58,"NO LOCATED APs",1,pal::TEXT);
        txt(s,cc.x+8,cc.y+72,"Capture with a GPS fix.",1,pal::DIM);
        txt(s,cc.x+8,cc.y+86,"All discoveries: CODEX",1,pal::DIM);
    }


    // --- range -/+ control at the column foot --------------------------------
    box(s, L.range.x, L.range.y, L.range.w, 44, pal::PANEL);
    frame(s, L.range.x, L.range.y, L.range.w, 44, pal::EDGE);
    frame(s, L.minus.x, L.minus.y, 44, 44, pal::EDGE);
    frame(s, L.plus.x,  L.plus.y,  44, 44, pal::EDGE);
    centered(s, L.minus.x + 22, L.minus.y + 15, "IN", 2, pal::BRIGHT);
    centered(s, L.plus.x  + 22, L.plus.y  + 15, "OUT", 2, pal::BRIGHT);
    snprintf(buf, sizeof buf, "%dM", sc.rangeM);
    centered(s, L.map.x + L.map.w / 2, L.map.y + L.map.h - 10, buf,
             1, pal::TEXT);
    // nav bar + STOP/REC drawn by scanr::drawChrome above (shared design-1 chrome).
}

// --- strip compositor: small buffer, compose-then-emit (mirrors scanr) --------
class Compositor {
public:
    static constexpr int MAXW    = 480;
    static constexpr int STRIP_H = 16;
    static constexpr int BUF_BYTES = MAXW * STRIP_H * 2;

    void renderRows(int W, int H, const DomainScene& sc, int y0, int y1, Sink& sink) {
        if (y0 < 0) y0 = 0; if (y1 > H) y1 = H;
        for (int sy = y0; sy < y1; sy += STRIP_H) {
            const int sh = (sy + STRIP_H > y1) ? (y1 - sy) : STRIP_H;
            scanr::S s{ buf_, W, sy, sh };
            composeStrip(s, W, H, sc);
            sink.strip(0, sy, W, sh, buf_);
        }
    }
    void renderFull(int W, int H, const DomainScene& sc, Sink& sink) { renderRows(W, H, sc, 0, H, sink); }
    void renderRegion(int W, int H, const DomainScene& sc, uieng::Rect r, Sink& sink) {
        renderRows(W, H, sc, (r.y / STRIP_H) * STRIP_H, r.y + r.h, sink);
    }
private:
    uint16_t buf_[MAXW * STRIP_H];
};

}  // namespace domr
