// mesh_render.h -- static MESH compositor + scene.
//
// MESH is "who is listening, and to what": a Core/loss header, a node list
// (age mark + id + loss), and a network-control row (ENROLL / RESET). Regions
// come from uieng::mesh(W,H); painted as strips composed from scene state, same
// strategy as the other engine screens. Reuses scanr:: primitives +
// gpsLabel/recLabel. Host + device compilable (no Arduino deps).
#pragma once
#include "scan_render.h"
#include "sub_render.h"       // shared status + nav chrome for sub-screens
#include "layout_engine.h"
#include "palette.h"
#include "font5x7.h"
#include "canvas.h"
#include "model.h"
#include <cstdint>
#include <cstdio>

namespace mshr {

using scanr::Sink;
using scanr::GnssFix;
using scanr::Rec;

struct MeshNode {
    uint8_t  id[3];      // last three octets; the tail tells nodes apart
    uint32_t ageS;       // since last heard from
    uint8_t  lossPct;    // reports that never arrived
    bool     live;
};

struct MeshScene {
    GnssFix  gps;
    int      nodes = 0;                         // live node count (status chrome "N NODES")
    bool     hasCell = false; int battPct = 0;
    Rec      rec = Rec::Off;
    // header
    int      meshCoords = 0;                    // live coordinators (0 = Core runs the nodes)
    bool     coreCamping = false; int coreChan = 0;
    int      meshLossPct = 0;
    int      netId = 0;                         // 0 = open network
    int      enrollSecs = 0;                    // pairing window countdown (0 = closed)
    // node list (caller-owned, stable)
    const MeshNode* rows = nullptr; int nRows = 0;
    uint8_t  theme = 0;
    uint32_t now = 0;
};

inline uint16_t lossInk(int pct) { return pct >= 20 ? pal::RED : pct >= 5 ? pal::AMBER : pal::GREEN; }
inline uint16_t ageMark(uint32_t s) { return s < 10 ? pal::GREEN : s < 20 ? pal::AMBER : pal::RED; }

inline void composeStrip(scanr::S& s, int W, int H, const MeshScene& sc) {
    using scanr::box; using scanr::txt; using scanr::frame; using scanr::centered; using scanr::fitScale;
    const uieng::MeshLayout L = uieng::mesh(W, H);
    subr::panels(s, W, H);
    subr::status(s, L.statusCells, sc.gps, sc.nodes, sc.hasCell, sc.battPct, sc.rec);

    // title band + NET state
    txt(s, L.title.x, L.title.y + 2, "MESH", 2, pal::BRIGHT);
    char nb[16]; if (sc.netId) snprintf(nb, sizeof nb, "NET %d", sc.netId); else snprintf(nb, sizeof nb, "NET OPEN");
    txt(s, L.title.x + L.title.w - Font::width(nb,1), L.title.y + 4, nb, 1, sc.netId ? pal::GREEN : pal::DIM);

    // header: CORE/COORD + sweep, LOSS on the right
    char hd[24]; if (sc.meshCoords > 0) snprintf(hd, sizeof hd, "%d COORD", sc.meshCoords);
                 else                    snprintf(hd, sizeof hd, "CORE");
    char hd2[32];
    if (sc.coreCamping) snprintf(hd2, sizeof hd2, "%s CH%d", hd, sc.coreChan);
    else                snprintf(hd2, sizeof hd2, "%s SWEEP", hd);
    box(s, L.header.x, L.header.y + 2, 3, 6, pal::WITCH_HI);
    txt(s, L.header.x + 6, L.header.y + 2, hd2, 1, pal::BRIGHT);
    char lb[16]; snprintf(lb, sizeof lb, "LOSS %d%%", sc.meshLossPct);
    txt(s, L.header.x + L.header.w - Font::width(lb,1), L.header.y + 2, lb, 1, lossInk(sc.meshLossPct));
    box(s, L.header.x, L.header.y + L.header.h - 1, L.header.w, 1, pal::EDGE);

    // node list: two columns
    box(s, L.list.x, L.list.y, L.list.w, L.list.h, ui::Canvas::mix(pal::VOID, pal::PANEL, 1, 3));
    if (sc.nRows == 0) {
        centered(s, L.list.x + L.list.w / 2, L.list.y + L.list.h / 2 - 3,
                 sc.meshCoords > 0 ? "NO LOCAL NODES" : "NO NODES", 1, pal::DIM);
    } else {
        const int colRows = 7, rowHt = 10;
        const int colW = (L.list.w - 8) / 2, colX[2] = { L.list.x + 4, L.list.x + 4 + colW };
        for (int i = 0; i < sc.nRows && i < 2 * colRows; i++) {
            const MeshNode& nd = sc.rows[i];
            const int x = colX[i / colRows], y = L.list.y + 4 + (i % colRows) * rowHt;
            box(s, x, y + 1, 3, 6, ageMark(nd.ageS));
            char id[8]; snprintf(id, sizeof id, "%02X:%02X", nd.id[1], nd.id[2]);
            txt(s, x + 6, y, id, 1, nd.live ? pal::TEXT : pal::DIM);
            char ls[8]; snprintf(ls, sizeof ls, "%d%%", nd.lossPct);
            txt(s, x + colW - 2 - Font::width(ls,1), y, ls, 1, lossInk(nd.lossPct));
        }
    }

    // control row: ENROLL (or PAIR countdown) + RESET
    char eb[16];
    if (sc.enrollSecs) snprintf(eb, sizeof eb, "PAIR %dS", sc.enrollSecs); else snprintf(eb, sizeof eb, "ENROLL");
    const uint16_t eInk = sc.enrollSecs ? pal::GREEN : pal::WITCH_HI;
    box(s, L.enroll.x, L.enroll.y, L.enroll.w, 44, ui::Canvas::mix(pal::PANEL, pal::VOID, 1, 3));
    frame(s, L.enroll.x, L.enroll.y, L.enroll.w, 44, ui::Canvas::mix(eInk, pal::EDGE, 1, 2));
    centered(s, L.enroll.x + L.enroll.w / 2, L.enroll.y + 15, eb, fitScale(eb, L.enroll.w - 8, 2), eInk);
    const uint16_t rInk = sc.netId ? pal::WITCH_HI : pal::DIM;
    box(s, L.reset.x, L.reset.y, L.reset.w, 44, ui::Canvas::mix(pal::PANEL, pal::VOID, 1, 3));
    frame(s, L.reset.x, L.reset.y, L.reset.w, 44, ui::Canvas::mix(rInk, pal::EDGE, 1, 2));
    centered(s, L.reset.x + L.reset.w / 2, L.reset.y + 15, "RESET", fitScale("RESET", L.reset.w - 8, 2), rInk);

    subr::nav(s, L.navCells);
}

class Compositor {
public:
    static constexpr int MAXW = 480, STRIP_H = 16, BUF_BYTES = MAXW * STRIP_H * 2;
    void renderRows(int W, int H, const MeshScene& sc, int y0, int y1, Sink& sink) {
        if (y0 < 0) y0 = 0; if (y1 > H) y1 = H;
        for (int sy = y0; sy < y1; sy += STRIP_H) {
            const int sh = (sy + STRIP_H > y1) ? (y1 - sy) : STRIP_H;
            scanr::S s{ buf_, W, sy, sh };
            composeStrip(s, W, H, sc);
            sink.strip(0, sy, W, sh, buf_);
        }
    }
    void renderFull(int W, int H, const MeshScene& sc, Sink& sink) { renderRows(W, H, sc, 0, H, sink); }
    void renderRegion(int W, int H, const MeshScene& sc, uieng::Rect r, Sink& sink) {
        renderRows(W, H, sc, (r.y / STRIP_H) * STRIP_H, r.y + r.h, sink);
    }
private:
    uint16_t buf_[MAXW * STRIP_H];
};

}  // namespace mshr
