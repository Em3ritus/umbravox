// codex_render.h -- static CODEX compositor + scene.
//
// CODEX is the register of found devices: a filter bar (WIFI / 5 GHZ / BLE /
// LOCATED) over a scrolling list, one 44px row per device. Regions come from
// uieng::codex(W,H); the screen is painted as horizontal strips composed
// completely from scene state, same strategy as SCAN/DOMAIN. Reuses scanr::
// drawing primitives and scanr::gpsLabel/recLabel (single-sourced GPS/rec
// chrome), and domr::bandColor for the shared band legend. Host + device
// compilable (no Arduino deps).
#pragma once
#include "scan_render.h"      // scanr:: primitives + GnssFix/gpsLabel/recLabel/Sink/S
#include "domain_render.h"    // domr::bandColor (shared legend)
#include "layout_engine.h"
#include "palette.h"
#include "font5x7.h"
#include "canvas.h"
#include <cstdint>
#include <cstdio>

namespace codr {

using scanr::Sink;
using scanr::GnssFix;
using scanr::Rec;

// Which filter cell is active. 0..2 are the band filters; 3 is the located
// scope. Matches uieng::codexHits payloads (CodexFilter 0/1/2, LOCATED).
enum Filter : uint8_t { F_WIFI = 0, F_5GHZ = 1, F_BLE = 2, F_LOCATED = 3 };

// One list row, already ORDERED + PAGINATED by the caller (decision #5:
// strongest signal first; domr::orderLocated + domr::paginate). label is the
// SSID ("" for a hidden network); chan 0 + band BLE draws "BLE"; located/threat
// are glyph hints only.
struct CodexRow {
    const char* key;      // stable identity (type+addr); the row binds to it
    const char* label;    // SSID, or "" for hidden
    uint8_t band;         // 0=2.4,1=5,2=BLE
    int     rssi;         // dBm, for the signal meter
    uint8_t chan;         // channel (Wi-Fi); ignored for BLE
    bool    threat;       // signature match -> ring hint
};

struct CodexScene {
    // status chrome (projected by scanr::gpsLabel / recLabel)
    GnssFix  gps;
    int      nodes = 0;
    bool     hasCell = false; int battPct = 0;
    Rec      rec = Rec::Off;
    // list
    uint8_t  filter = F_WIFI;                       // active filter cell
    const CodexRow* rows = nullptr; int nRows = 0;  // this page's rows, in order
    int      pageLo = 0, pageHi = 0, listTotal = 0; // 1-based "lo-hi / total"
    uint8_t  theme = 0;
    uint32_t now = 0;
};

// --- signal meter: four stepped blocks from RSSI (mirrors screen_codex) ------
inline int codexBars(int rssi) {
    if (rssi >= -55) return 4;
    if (rssi >= -67) return 3;
    if (rssi >= -78) return 2;
    if (rssi >= -88) return 1;
    return 0;
}

// --- compose one strip completely from the scene -----------------------------
inline void composeStrip(scanr::S& s, int W, int H, const CodexScene& sc) {
    using scanr::box; using scanr::txt; using scanr::frame; using scanr::centered; using scanr::fitScale;
    const uieng::CodexLayout L = uieng::codex(W, H);

    box(s, 0, 0, W, H, pal::VOID);
    // shared design-1 chrome: header + nav (CODEX lit, index 2) + STOP/REC control
    scanr::drawChrome(s, W, H, L.body, {sc.gps, sc.nodes, sc.hasCell, sc.battPct, sc.rec}, 2);

    // filter bar: four cells, the active one lit
    static const char* const flab[4] = {"WIFI", "5 GHZ", "BLE", "LOCATED"};
    for (int i = 0; i < 4; i++) { const uieng::Rect f = L.filters[i];
        const bool on = (sc.filter == i);
        box(s, f.x, f.y, f.w, f.h, on ? pal::PANEL : ui::Canvas::mix(pal::VOID, pal::PANEL, 1, 2));
        frame(s, f.x, f.y, f.w, f.h, on ? pal::WITCH_HI : pal::EDGE);
        centered(s, f.x + f.w / 2, f.y + 15, flab[i], fitScale(flab[i], f.w - 8, 2), on ? pal::BRIGHT : pal::DIM);
    }

    // list body + rows
    const uieng::Rect lst = L.list;
    box(s, lst.x, lst.y, lst.w, lst.h, ui::Canvas::mix(pal::VOID, pal::PANEL, 1, 3));
    if (sc.listTotal == 0) {
        centered(s, lst.x + lst.w / 2, lst.y + lst.h / 2 - 3, "NOTHING YET", 1, pal::DIM);
    } else {
        for (int b = 0; b < sc.nRows && b < L.nRows; b++) {
            const uieng::Rect r = L.rows[b];
            const CodexRow& g = sc.rows[b];
            const uint16_t bc = domr::bandColor(g.band);
            if (b) box(s, r.x + 4, r.y, r.w - 8, 1, pal::EDGE);     // row divider
            box(s, r.x, r.y + 2, 3, r.h - 4, bc);                  // band swatch
            if (g.label && g.label[0]) txt(s, r.x + 10, r.y + 6, g.label, fitScale(g.label, r.w - 100, 2), pal::TEXT);
            else                       txt(s, r.x + 10, r.y + 6, "<HIDDEN>", 1, pal::DIM);
            // stepped signal meter
            const int lit = codexBars(g.rssi), bw = 5, gap = 1, bx0 = r.x + r.w - 44;
            for (int i = 0; i < 4; i++) { const int bh = 4 + i * 4;
                box(s, bx0 + i * (bw + gap), r.y + r.h - 6 - bh, bw, bh, i < lit ? bc : pal::EDGE); }
            char ch[16];
            if (g.band == 2) snprintf(ch, sizeof ch, "BLE"); else snprintf(ch, sizeof ch, "CH%d", g.chan);
            txt(s, r.x + r.w - 4 - Font::width(ch, 1), r.y + 6, ch, 1, pal::DIM);
            if (g.threat) frame(s, r.x + 1, r.y + 1, r.w - 2, r.h - 2, pal::RED);
        }
        // pager "lo-hi / total" in the list's bottom slack, right-aligned
        char pg[40]; snprintf(pg, sizeof pg, "%d-%d / %d", sc.pageLo, sc.pageHi, sc.listTotal);
        txt(s, lst.x + lst.w - 2 - Font::width(pg, 1), lst.y + lst.h - 8, pg, 1, pal::DIM);
    }

    // nav bar + STOP/REC drawn by scanr::drawChrome above (shared design-1 chrome).
}

// --- strip compositor: small buffer, compose-then-emit (mirrors scanr/domr) ---
class Compositor {
public:
    static constexpr int MAXW    = 480;
    static constexpr int STRIP_H = 16;
    static constexpr int BUF_BYTES = MAXW * STRIP_H * 2;

    void renderRows(int W, int H, const CodexScene& sc, int y0, int y1, Sink& sink) {
        if (y0 < 0) y0 = 0; if (y1 > H) y1 = H;
        for (int sy = y0; sy < y1; sy += STRIP_H) {
            const int sh = (sy + STRIP_H > y1) ? (y1 - sy) : STRIP_H;
            scanr::S s{ buf_, W, sy, sh };
            composeStrip(s, W, H, sc);
            sink.strip(0, sy, W, sh, buf_);
        }
    }
    void renderFull(int W, int H, const CodexScene& sc, Sink& sink) { renderRows(W, H, sc, 0, H, sink); }
    void renderRegion(int W, int H, const CodexScene& sc, uieng::Rect r, Sink& sink) {
        renderRows(W, H, sc, (r.y / STRIP_H) * STRIP_H, r.y + r.h, sink);
    }
private:
    uint16_t buf_[MAXW * STRIP_H];
};

}  // namespace codr
