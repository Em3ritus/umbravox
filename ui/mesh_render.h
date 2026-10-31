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
    ui::NodeDiagnostics diag{};
};

struct MeshScene {
    GnssFix  gps;
    int      nodes = 0;                         // live node count (status chrome "N NODES")
    bool     hasCell = false; int battPct = 0;
    ui::PoiStatus poi = ui::PoiStatus::Ready;
    bool pmuOk = false, charging = false;
    ui::StorageState sd=ui::StorageState::Missing;
    bool bridgeOn = false;
    Rec      rec = Rec::Off;
    // header
    int      meshCoords = 0;                    // live coordinators (0 = Core runs the nodes)
    bool     coreCamping = false; int coreChan = 0;
    int      meshLossPct = 0;
    ui::LocalDiagnostics local;
    int page=0;
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
    // shared design-1 chrome (header + nav, no tab lit) + STOP/REC record control
    scanr::drawChrome(s, W, H, L.body, {sc.gps, sc.nodes, sc.hasCell, sc.battPct, sc.rec, sc.theme, sc.bridgeOn, sc.pmuOk, sc.charging, sc.poi, sc.sd}, -1);

    // title band + NET state
    txt(s, L.title.x, L.title.y + 2, "MESH", 2, pal::BRIGHT);
    char nb[24]; if (sc.netId) snprintf(nb, sizeof nb, "NET %d", sc.netId); else snprintf(nb, sizeof nb, "NET OPEN");
    txt(s, L.title.x + L.title.w - Font::width(nb,1), L.title.y + 4, nb, 1, sc.netId ? pal::GREEN : pal::DIM);

    // header: CORE/COORD + sweep, LOSS on the right
    char hd[24]; if (sc.meshCoords > 0) snprintf(hd, sizeof hd, "%d COORD", sc.meshCoords);
                 else                    snprintf(hd, sizeof hd, "CORE");
    char hd2[48];
    if (sc.coreCamping) snprintf(hd2, sizeof hd2, "%s CH%d", hd, sc.coreChan);
    else                snprintf(hd2, sizeof hd2, "%s SWEEP", hd);
    box(s, L.header.x, L.header.y + 2, 3, 6, pal::WITCH_HI);
    txt(s, L.header.x + 6, L.header.y + 2, hd2, 1, pal::BRIGHT);
    char lb[24]; snprintf(lb, sizeof lb, "LOSS %d%%", sc.meshLossPct);
    txt(s, L.header.x + L.header.w - Font::width(lb,1), L.header.y + 2, lb, 1, lossInk(sc.meshLossPct));
    box(s, L.header.x, L.header.y + L.header.h - 1, L.header.w, 1, pal::EDGE);

    // node list: two columns
    box(s, L.list.x, L.list.y, L.list.w, L.list.h, ui::Canvas::mix(pal::VOID, pal::PANEL, 1, 3));
    const int selected=(sc.page<0?0:sc.page)%(sc.nRows+1);
    if(selected==0){
        const auto& d=sc.local;char lines[5][96]{};
        if(d.valid){
            snprintf(lines[0],96,"LOCAL %s %uMS",d.active?"ACTIVE":"PASSIVE",d.dwellMs);
            snprintf(lines[1],96,"SCANS %lu DONE %lu",(unsigned long)d.started,(unsigned long)d.completed);
            snprintf(lines[2],96,"SCANFAIL %lu OMIT %lu",(unsigned long)d.failed,(unsigned long)d.omitted);
            snprintf(lines[3],96,"BRIDGE %s %s Q%u",d.bridgeMode==1?"WDGO":d.bridgeMode==2?"BISCUIT":"OFF",d.streaming?"STREAM":d.connected?"LINK":"",d.bridgeQueued);
            snprintf(lines[4],96,"RXDROP %lu TXRETRY %lu",(unsigned long)d.rxDrops,(unsigned long)d.txRetries);
        }else snprintf(lines[0],96,"LOCAL DIAGNOSTICS UNAVAILABLE");
        const int scale=W>=400?2:1,step=W>=400?22:8;
        for(int i=0;i<5;i++)txt(s,L.list.x+4,L.list.y+4+i*step,lines[i],fitScale(lines[i],L.list.w-8,scale),i==0?pal::BRIGHT:pal::TEXT);
    }else{
        const MeshNode& nd=sc.rows[selected-1];
        const auto& d=nd.diag;
        char lines[5][96]{};
        if(d.known){
            snprintf(lines[0],96,"%02X:%02X:%02X  %s %uMS",nd.id[0],nd.id[1],nd.id[2],d.active?"ACTIVE":"PASSIVE",d.dwellMs);
            snprintf(lines[1],96,"%s %s / %s %luS",d.profile?"DISCOVERY":"TRACKING",d.camp?"CAMP":"SWEEP",d.ageS>=10?"STALE":"AGE",(unsigned long)d.ageS);
            snprintf(lines[2],96,"Q %u/%u  PEAK %u",d.depth,d.capacity,d.peak);
            snprintf(lines[3],96,"DROP %lu  LINKFAIL %lu",(unsigned long)d.drops,(unsigned long)d.sendFailed);
            snprintf(lines[4],96,"API %lu SCANFAIL %lu LOSS %u%%",(unsigned long)d.reportRejected,(unsigned long)d.scanFailed,nd.lossPct);
        }else{
            snprintf(lines[0],96,"%02X:%02X:%02X / LOSS %u%%",nd.id[0],nd.id[1],nd.id[2],nd.lossPct);
            snprintf(lines[1],96,"NO TELEMETRY");
            snprintf(lines[2],96,"APPLIED SETTINGS UNKNOWN");
            snprintf(lines[3],96,"NODE FIRMWARE MUST SUPPORT STATUS");
            snprintf(lines[4],96,"LAST HEARD %luS",(unsigned long)nd.ageS);
        }
        const int scale=W>=400?2:1,step=W>=400?22:8;
        for(int i=0;i<5;i++)txt(s,L.list.x+4,L.list.y+4+i*step,lines[i],fitScale(lines[i],L.list.w-8,scale),i==0?pal::BRIGHT:(!d.known || d.ageS>=10)?pal::AMBER:pal::TEXT);

    }

    // control row: ENROLL (or PAIR countdown) + RESET
    char eb[24];
    if (sc.enrollSecs) snprintf(eb, sizeof eb, "PAIR %dS", sc.enrollSecs); else snprintf(eb, sizeof eb, "ENROLL");
    const uint16_t eInk = sc.enrollSecs ? pal::GREEN : pal::WITCH_HI;
    box(s, L.enroll.x, L.enroll.y, L.enroll.w, 44, ui::Canvas::mix(pal::PANEL, pal::VOID, 1, 3));
    frame(s, L.enroll.x, L.enroll.y, L.enroll.w, 44, ui::Canvas::mix(eInk, pal::EDGE, 1, 2));
    centered(s, L.enroll.x + L.enroll.w / 2, L.enroll.y + 15, eb, fitScale(eb, L.enroll.w - 8, 2), eInk);
    char page[32];snprintf(page,sizeof page,"%d/%d >",selected+1,sc.nRows+1);
    frame(s,L.pager.x,L.pager.y,L.pager.w,44,pal::EDGE_LIT);
    centered(s,L.pager.x+L.pager.w/2,L.pager.y+15,page,fitScale(page,L.pager.w-8,2),pal::TEXT);
    const uint16_t rInk = sc.netId ? pal::WITCH_HI : pal::DIM;
    box(s, L.reset.x, L.reset.y, L.reset.w, 44, ui::Canvas::mix(pal::PANEL, pal::VOID, 1, 3));
    frame(s, L.reset.x, L.reset.y, L.reset.w, 44, ui::Canvas::mix(rInk, pal::EDGE, 1, 2));
    centered(s, L.reset.x + L.reset.w / 2, L.reset.y + 15, "RESET", fitScale("RESET", L.reset.w - 8, 2), rInk);

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
