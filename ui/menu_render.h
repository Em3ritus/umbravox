// menu_render.h -- static MENU (hub) compositor + scene.
//
// MENU is the secondary hub: six launchers reflow from 3x2 to 2x3.
// Primary destinations live in the footer; Labels lives in Display.
// Regions come from uieng::menu(W,H); the screen is painted as strips
// composed completely from scene state, same strategy as the other engine
// screens. Reuses scanr:: primitives + gpsLabel/recLabel. 5x7 plumbing (runes
// and final typography are the separate visual-fidelity milestone). Host +
// device compilable (no Arduino deps).
#pragma once
#include "poi_state.h"
#include "scan_render.h"      // scanr:: primitives + GnssFix/gpsLabel/recLabel/Sink/S
#include "layout_engine.h"
#include "palette.h"
#include "font5x7.h"
#include "canvas.h"
#include <cstdint>
#include <cstdio>

namespace menr {

using scanr::Sink;
using scanr::GnssFix;
using scanr::Rec;

// The hub's launcher tiles, in grid order (matches uieng::menuHits). Each
// carries both label modes; the LABELS toggle is on Display.
struct MenuEntry { const char* brand; const char* plain; };
static const MenuEntry kMenuEntries[6] = {
    {"ECHOES","PROBES"},{"NODES","NODES"},{"OMENS","ALERTS"},
    {"FILES","FILES"},{"ATTUNE","SETTINGS"},{"ASPECT","DISPLAY"}
};
static const char* const kMenuDescriptions[]={"REQUESTED NETWORKS","NODES + DIAGNOSTICS","SIGNATURE MATCHES","RECORDINGS","RADIO + CAPTURE","THEMES + LABELS"};

struct MenuScene {
    // status chrome (projected by scanr::gpsLabel / recLabel)
    GnssFix  gps;
    int      nodes = 0;
    bool     hasCell = false; int battPct = 0;
    ui::PoiStatus poi = ui::PoiStatus::Ready;
    bool pmuOk = false, charging = false;
    ui::StorageState sd=ui::StorageState::Missing;
    bool bridgeOn = false;
    Rec      rec = Rec::Off;
    // hub state
    bool     plain = false;      // label mode: false = on-brand, true = plain
    uint8_t  theme = 0;
    uint32_t now = 0;
};

// --- compose one strip completely from the scene -----------------------------
inline void composeStrip(scanr::S& s, int W, int H, const MenuScene& sc) {
    using scanr::box; using scanr::txt; using scanr::frame; using scanr::centered; using scanr::fitScale;
    const uieng::MenuLayout L = uieng::menu(W, H);

    box(s, 0, 0, W, H, pal::VOID);
    // shared design-1 chrome: header + nav (MENU lit, index 3) + STOP/REC control
    scanr::drawChrome(s, W, H, L.body, {sc.gps, sc.nodes, sc.hasCell, sc.battPct, sc.rec, sc.theme, sc.bridgeOn, sc.pmuOk, sc.charging, sc.poi, sc.sd}, 3);

    for(int i=0;i<L.nCells;i++){
        const auto t=L.cells[i];
        box(s,t.x,t.y,t.w,t.h,pal::PANEL);frame(s,t.x,t.y,t.w,t.h,pal::EDGE);
        const char* label=sc.plain?kMenuEntries[i].plain:kMenuEntries[i].brand;
        const int scale=fitScale(label,t.w-12,2);
        // Center the complete label/subtitle group, not just the heading.
        const bool wide=W>=400;
        const int groupH=7*scale+(wide?17:0),y=t.y+(t.h-groupH)/2;
        centered(s,t.x+t.w/2,y,label,scale,pal::TEXT);
        if(wide)centered(s,t.x+t.w/2,y+7*scale+10,kMenuDescriptions[i],1,pal::DIM);
    }

}

// --- strip compositor: small buffer, compose-then-emit (mirrors the others) ---
class Compositor {
public:
    static constexpr int MAXW    = 480;
    static constexpr int STRIP_H = 16;
    static constexpr int BUF_BYTES = MAXW * STRIP_H * 2;

    void renderRows(int W, int H, const MenuScene& sc, int y0, int y1, Sink& sink) {
        if (y0 < 0) y0 = 0; if (y1 > H) y1 = H;
        for (int sy = y0; sy < y1; sy += STRIP_H) {
            const int sh = (sy + STRIP_H > y1) ? (y1 - sy) : STRIP_H;
            scanr::S s{ buf_, W, sy, sh };
            composeStrip(s, W, H, sc);
            sink.strip(0, sy, W, sh, buf_);
        }
    }
    void renderFull(int W, int H, const MenuScene& sc, Sink& sink) { renderRows(W, H, sc, 0, H, sink); }
    void renderRegion(int W, int H, const MenuScene& sc, uieng::Rect r, Sink& sink) {
        renderRows(W, H, sc, (r.y / STRIP_H) * STRIP_H, r.y + r.h, sink);
    }
private:
    uint16_t buf_[MAXW * STRIP_H];
};

}  // namespace menr
