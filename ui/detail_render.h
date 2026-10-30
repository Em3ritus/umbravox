// detail_render.h -- static DETAIL (one device) compositor + scene.
//
// Everything known about one device: identity, signature verdict, an RSSI
// history chart, and first/last-seen. The chart is the reason the screen earns
// its place -- a climbing line means you are driving toward whatever it is.
// Regions come from uieng::detail(W,H); the caller supplies the identity/
// signature strings pre-formatted and the raw RSSI history for the chart.
// Reuses subr:: chrome + domr::bandColor. Host + device compilable.
#pragma once
#include "scan_render.h"
#include "sub_render.h"
#include "domain_render.h"   // domr::bandColor (shared legend)
#include "layout_engine.h"
#include "palette.h"
#include "font5x7.h"
#include "canvas.h"
#include "model.h"           // ui::HIST_N (chart sample cap)
#include <cstdint>
#include <cstdio>

namespace detr {

using scanr::Sink;
using scanr::GnssFix;
using scanr::Rec;

struct DetailScene {
    GnssFix  gps;
    int      nodes = 0;
    bool     hasCell = false; int battPct = 0;
    Rec      rec = Rec::Off;
    // the device (invalid -> the selection is gone; render "GONE")
    bool     valid = false;
    const char* name = "";       // SSID, or "" for hidden
    const char* mac = "";        // "AA:BB:CC:DD:EE:FF"
    const char* bandChan = "";   // "2.4GHZ CH6" / "BLE"
    uint8_t  band = 0;           // for the chart ink
    int      rssi = 0;           // dBm
    const char* auth = "";       // "WPA2" / "OPEN" / "" (BLE)
    bool     authOpen = false;   // open network -> amber
    const char* threat = "";     // threat name, "" if none
    uint8_t  conf = 0;           // 0 LOW 1 MED 2 HIGH
    const char* why = "";        // signature reason
    const char* firstSeen = "";  // "3H"
    const char* lastSeen = "";   // "45S"
    const int8_t* hist = nullptr; int histN = 0;   // RSSI samples (oldest..newest)
    uint8_t  theme = 0;
    uint32_t now = 0;
};

inline uint16_t confInk(uint8_t c) { return c >= 2 ? pal::RED : c == 1 ? pal::AMBER : pal::WITCH; }

// RSSI -> chart y within [top,bot] px rows, clamped to the -30..-95 dBm band.
inline int rssiToY(int rssi, int top, int h) {
    const int RT = -30, RB = -95;
    if (rssi > RT) rssi = RT; if (rssi < RB) rssi = RB;
    return top + h - 1 - (rssi - RB) * (h - 1) / (RT - RB);
}

inline void composeStrip(scanr::S& s, int W, int H, const DetailScene& sc) {
    using scanr::box; using scanr::txt; using scanr::frame; using scanr::centered;
    const uieng::DetailLayout L = uieng::detail(W, H);
    subr::panels(s, W, H);
    subr::status(s, L.statusCells, sc.gps, sc.nodes, sc.hasCell, sc.battPct, sc.rec);

    // BACK button (always present)
    box(s, L.back.x, L.back.y + 1, 44, 42, ui::Canvas::mix(pal::PANEL, pal::VOID, 1, 3));
    frame(s, L.back.x, L.back.y + 1, 44, 42, pal::EDGE_LIT);
    centered(s, L.back.x + 22, L.back.y + 15, "<", 2, pal::WITCH_HI);

    if (!sc.valid) {
        centered(s, L.body.x + L.body.w / 2, L.body.y + L.body.h / 2 - 3, "GONE", 1, pal::DIM);
        subr::nav(s, L.navCells);
        return;
    }

    const uint16_t bc = domr::bandColor(sc.band);
    // identity header (right of BACK): name, mac, band/chan + rssi
    const uieng::Rect hd = L.header;
    txt(s, hd.x + 4, hd.y + 2, (sc.name && sc.name[0]) ? sc.name : "<HIDDEN>", 1,
        (sc.name && sc.name[0]) ? pal::BRIGHT : pal::DIM);
    txt(s, hd.x + 4, hd.y + 12, sc.mac ? sc.mac : "", 1, pal::TEXT);
    txt(s, hd.x + 4, hd.y + 24, sc.bandChan ? sc.bandChan : "", 1, bc);
    char rb[12]; snprintf(rb, sizeof rb, "%dDBM", sc.rssi);
    txt(s, hd.x + hd.w - 4 - Font::width(rb, 1), hd.y + 24, rb, 1, pal::BRIGHT);
    if (sc.auth && sc.auth[0])
        txt(s, hd.x + hd.w - 4 - Font::width(sc.auth, 1), hd.y + 2, sc.auth, 1, sc.authOpen ? pal::AMBER : pal::DIM);

    // stat block: signature verdict + first/last seen
    const uieng::Rect st = L.stats;
    if (sc.threat && sc.threat[0]) {
        const uint16_t ec = confInk(sc.conf);
        box(s, st.x, st.y + 4, 2, 11, ec);
        txt(s, st.x + 6, st.y + 5, sc.threat, 1, ec);
        const char* cf = sc.conf >= 2 ? "HIGH" : sc.conf == 1 ? "MED" : "LOW";
        txt(s, st.x + st.w - 4 - Font::width(cf, 1), st.y + 5, cf, 1, ec);
        if (sc.why && sc.why[0]) txt(s, st.x + 6, st.y + 16, sc.why, 1, pal::DIM);
    } else {
        txt(s, st.x + 6, st.y + 5, "NO SIGNATURE MATCH", 1, pal::DIM);
    }
    char sb[24];
    snprintf(sb, sizeof sb, "SEEN %s AGO", sc.lastSeen ? sc.lastSeen : "?");
    txt(s, st.x + 6, st.y + st.h - 10, sb, 1, pal::DIM);
    snprintf(sb, sizeof sb, "FIRST %s", sc.firstSeen ? sc.firstSeen : "?");
    txt(s, st.x + st.w - 4 - Font::width(sb, 1), st.y + st.h - 10, sb, 1, pal::DIM);

    // RSSI history chart: filled bars, reference lines at -50 and -75
    const uieng::Rect ch = L.spark;
    box(s, ch.x, ch.y, ch.w, ch.h, ui::Canvas::mix(pal::VOID, pal::PANEL, 1, 2));
    frame(s, ch.x, ch.y, ch.w, ch.h, pal::EDGE);
    const int top = ch.y + 2, hgt = ch.h - 4;
    for (int k = 0; k < 2; k++) { const int ry = rssiToY(k ? -75 : -50, top, hgt);
        for (int x = ch.x + 2; x < ch.x + ch.w - 2; x += 3) scanr::pt(s, x, ry, pal::EDGE); }
    if (sc.histN == 0 || !sc.hist) {
        centered(s, ch.x + ch.w / 2, ch.y + ch.h / 2 - 3, "NO HISTORY", 1, pal::DIM);
    } else {
        const int span = ch.w - 6, step = (sc.histN > 1) ? span / (sc.histN - 1 > 0 ? sc.histN : 1) : span;
        const uint16_t fill = ui::Canvas::mix(pal::VOID, bc, 1, 3);
        for (int i = 0; i < sc.histN && i < ui::HIST_N; i++) {
            const int x = ch.x + 3 + (step ? i * step : 0);
            const int y = rssiToY(sc.hist[i], top, hgt);
            box(s, x, y, 1, (top + hgt - 1) - y, fill);
            scanr::pt(s, x, y, bc);
            if (i == sc.histN - 1) { scanr::pt(s, x, y, pal::BRIGHT); scanr::pt(s, x, y - 1, pal::BRIGHT); }
        }
    }

    subr::nav(s, L.navCells);
}

class Compositor {
public:
    static constexpr int MAXW = 480, STRIP_H = 16, BUF_BYTES = MAXW * STRIP_H * 2;
    void renderRows(int W, int H, const DetailScene& sc, int y0, int y1, Sink& sink) {
        if (y0 < 0) y0 = 0; if (y1 > H) y1 = H;
        for (int sy = y0; sy < y1; sy += STRIP_H) {
            const int sh = (sy + STRIP_H > y1) ? (y1 - sy) : STRIP_H;
            scanr::S s{ buf_, W, sy, sh }; composeStrip(s, W, H, sc); sink.strip(0, sy, W, sh, buf_);
        }
    }
    void renderFull(int W, int H, const DetailScene& sc, Sink& sink) { renderRows(W, H, sc, 0, H, sink); }
    void renderRegion(int W, int H, const DetailScene& sc, uieng::Rect r, Sink& sink) {
        renderRows(W, H, sc, (r.y / STRIP_H) * STRIP_H, r.y + r.h, sink);
    }
private:
    uint16_t buf_[MAXW * STRIP_H];
};

}  // namespace detr
