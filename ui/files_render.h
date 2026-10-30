// files_render.h -- static FILES compositor + scene.
//
// The SD session logs, newest first, and the one control that deletes. Log rows
// are 44px touch targets; a DELETE opens a YES/NO confirm modal that owns input
// while it is up. Regions come from uieng::files(W,H); the caller supplies each
// row's name/size pre-formatted and the bar status text + colour. Reuses subr::
// chrome. Host + device compilable (no Arduino deps).
#pragma once
#include "scan_render.h"
#include "sub_render.h"
#include "layout_engine.h"
#include "palette.h"
#include "font5x7.h"
#include "canvas.h"
#include <cstdint>
#include <cstdio>

namespace flsr {

using scanr::Sink;
using scanr::GnssFix;
using scanr::Rec;

struct FileRowV { const char* name; const char* size; bool active; };   // active = the logging session

struct FilesScene {
    GnssFix  gps;
    int      nodes = 0;
    bool     hasCell = false; int battPct = 0;
    ui::PoiStatus poi = ui::PoiStatus::Ready;
    bool pmuOk = false, charging = false;
    bool bridgeOn = false;
    Rec      rec = Rec::Off;
    bool     card = false;                       // SD present
    const FileRowV* rows = nullptr; int nRows = 0;  // visible window
    int      total = 0;                          // logs in the list (for the scrollbar)
    int      firstIndex = 0;                     // list index of rows[0] (scrollbar thumb)
    int      sel = -1;                           // visible index selected, -1 none
    bool     deletable = false;                  // sel is a real, non-active log
    const char* status = "";                     // bar status text
    uint16_t statusInk = pal::DIM;
    bool     confirm = false;                    // the YES/NO modal is up
    const char* confirmName = "";
    const char* confirmSize = "";
    uint8_t  theme = 0;
    uint32_t now = 0;
};

inline void button(scanr::S& s, const uieng::Rect& r, const char* label, uint16_t edge, uint16_t ink, uint16_t fill) {
    using scanr::box; using scanr::frame; using scanr::centered; using scanr::fitScale;
    box(s, r.x, r.y + 1, r.w, r.h - 2, fill);
    frame(s, r.x, r.y + 1, r.w, r.h - 2, edge);
    centered(s, r.x + r.w / 2, r.y + r.h / 2 - 5, label, fitScale(label, r.w - 8, 2), ink);
}

inline void composeStrip(scanr::S& s, int W, int H, const FilesScene& sc) {
    using scanr::box; using scanr::txt; using scanr::frame; using scanr::centered;
    const uieng::FilesLayout L = uieng::files(W, H);
    // shared design-1 chrome (header + nav, no tab lit) + STOP/REC record control
    scanr::drawChrome(s, W, H, L.body, {sc.gps, sc.nodes, sc.hasCell, sc.battPct, sc.rec, sc.theme, sc.bridgeOn, sc.pmuOk, sc.charging, sc.poi}, -1);

    // list region background
    box(s, L.list.x, L.list.y, L.list.w, L.list.h, ui::Canvas::mix(pal::VOID, pal::PANEL, 1, 3));

    if (sc.confirm) {
        // modal: it owns the list region; YES / NO sit in the bar band
        frame(s, L.list.x, L.list.y, L.list.w, L.list.h, pal::RED);
        const int cx = L.list.x + L.list.w / 2;
        centered(s, cx, L.list.y + 8, "DELETE THIS LOG?", 1, pal::RED);
        centered(s, cx, L.list.y + 22, sc.confirmName ? sc.confirmName : "", 1, pal::BRIGHT);
        char line[32]; snprintf(line, sizeof line, "%s - CANNOT UNDO", sc.confirmSize ? sc.confirmSize : "");
        centered(s, cx, L.list.y + 34, line, 1, pal::DIM);
        button(s, L.yes, "YES", pal::RED, pal::RED, ui::Canvas::mix(pal::PANEL, pal::RED, 1, 6));
        button(s, L.no,  "NO",  pal::WITCH_HI, pal::BRIGHT, ui::Canvas::mix(pal::PANEL, pal::WITCH, 1, 2));
        return;
    }

    if (!sc.card || sc.nRows == 0) {
        const char* msg = sc.card ? "NO LOGS" : "NO CARD";
        centered(s, L.list.x + L.list.w / 2, L.list.y + L.list.h / 2 - 3, msg, 1, sc.card ? pal::DIM : pal::RED);
    } else {
        for (int i = 0; i < sc.nRows && i < L.nRows; i++) {
            const uieng::Rect r = L.rows[i];
            const FileRowV& g = sc.rows[i];
            const bool sel = (i == sc.sel);
            if (sel) { box(s, r.x, r.y, r.w, r.h - 1, ui::Canvas::mix(pal::PANEL, pal::WITCH, 1, 3));
                       frame(s, r.x, r.y, r.w, r.h - 1, pal::WITCH_HI); }
            else if (i) box(s, r.x + 2, r.y, r.w - 4, 1, ui::Canvas::mix(pal::PANEL, pal::EDGE, 1, 2));
            txt(s, r.x + 6, r.y + 15, g.name ? g.name : "", 1, sel ? pal::BRIGHT : pal::TEXT);
            int rx = r.x + r.w - 6;
            if (g.size) { rx -= Font::width(g.size, 1); txt(s, rx, r.y + 15, g.size, 1, pal::DIM); }
            if (g.active) { const char* R = "REC"; rx -= Font::width(R, 1) + 8; txt(s, rx, r.y + 15, R, 1, pal::RED); }
        }
        // scrollbar when there are more logs than fit; thumb tracks firstIndex
        if (sc.total > L.nRows) {
            const int tx = L.list.x + L.list.w - 3, th = L.list.h;
            box(s, tx, L.list.y, 2, th, pal::EDGE);
            int hh = th * L.nRows / sc.total; if (hh < 6) hh = 6;
            const int span = sc.total - L.nRows;
            int fi = sc.firstIndex; if (fi < 0) fi = 0; if (fi > span) fi = span;
            const int ty = L.list.y + (span > 0 ? (th - hh) * fi / span : 0);
            box(s, tx, ty, 2, hh, pal::WITCH_HI);
        }
    }

    // action bar: status text + DELETE
    txt(s, L.bar.x + 4, L.bar.y + 16, sc.status ? sc.status : "", 1, sc.statusInk);
    if (sc.deletable)
        button(s, L.del, "DELETE", pal::RED, pal::RED, ui::Canvas::mix(pal::PANEL, pal::RED, 1, 6));
    else
        button(s, L.del, sc.sel >= 0 ? "IN USE" : "DELETE", pal::EDGE, pal::DIM,
               ui::Canvas::mix(pal::PANEL, pal::VOID, 1, 3));

}

class Compositor {
public:
    static constexpr int MAXW = 480, STRIP_H = 16, BUF_BYTES = MAXW * STRIP_H * 2;
    void renderRows(int W, int H, const FilesScene& sc, int y0, int y1, Sink& sink) {
        if (y0 < 0) y0 = 0; if (y1 > H) y1 = H;
        for (int sy = y0; sy < y1; sy += STRIP_H) {
            const int sh = (sy + STRIP_H > y1) ? (y1 - sy) : STRIP_H;
            scanr::S s{ buf_, W, sy, sh }; composeStrip(s, W, H, sc); sink.strip(0, sy, W, sh, buf_);
        }
    }
    void renderFull(int W, int H, const FilesScene& sc, Sink& sink) { renderRows(W, H, sc, 0, H, sink); }
    void renderRegion(int W, int H, const FilesScene& sc, uieng::Rect r, Sink& sink) {
        renderRows(W, H, sc, (r.y / STRIP_H) * STRIP_H, r.y + r.h, sink);
    }
private:
    uint16_t buf_[MAXW * STRIP_H];
};

}  // namespace flsr
