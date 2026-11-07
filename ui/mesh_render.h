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
    int      coordNodes = 0;                    // nodes those coordinators carry (not listed: only counted)
    bool     coreCamping = false; int coreChan = 0;
    int      meshLossPct = 0;
    ui::LocalDiagnostics local;
    int page=0;
    uint16_t targetDwell=220;uint8_t targetProfile=0;bool targetActive=false;
    int      netId = 0;                         // 0 = open network
    int      enrollSecs = 0;                    // pairing window countdown (0 = closed)
    // node list (caller-owned, stable)
    const MeshNode* rows = nullptr; int nRows = 0;
    uint8_t  theme = 0;
    uint32_t now = 0;
};

inline int pageCount(int nodes){return 1+2*std::max(0,nodes);}
inline const char* timingStatus(const MeshScene& sc,const ui::NodeDiagnostics& d){
    if(!d.known)return "UNKNOWN";
    if(d.ageS>=10)return "STALE";
    return d.dwellMs==sc.targetDwell && d.profile==sc.targetProfile && d.active==sc.targetActive?"MATCH":"PENDING";
}
inline void channelLabel(const ui::NodeDiagnostics& d,char* out,size_t size){
    snprintf(out,size,"ASSIGNED");size_t used=strlen(out);
    if(!d.assignedCount){snprintf(out,size,"ASSIGNED UNKNOWN");return;}
    for(int i=0;i<std::min<int>(d.assignedCount,10) && used<size;i++){
        const int n=snprintf(out+used,size-used," %u",d.assignedChannels[i]);
        if(n<0 || size_t(n)>=size-used)break;used+=n;
    }
}
inline uint16_t lossInk(int pct) { return pct >= 20 ? pal::RED : pct >= 5 ? pal::AMBER : pal::GREEN; }
inline uint16_t ageMark(uint32_t s) { return s < 10 ? pal::GREEN : s < 20 ? pal::AMBER : pal::RED; }

// The subtitle of the THIS HANDHELD page. With a coordinator live and no nodes of its own the handheld lists nothing, so it
// says what it does know (how many coordinators, how many nodes they carry); otherwise the device pager text.
inline void handheldHint(const MeshScene& sc, char* out, size_t n) {
    if (sc.meshCoords > 0 && sc.nRows == 0) {
        if (sc.coordNodes > 0) snprintf(out, n, "%d COORD - %d NODE%s VIA COORD", sc.meshCoords, sc.coordNodes, sc.coordNodes == 1 ? "" : "S");
        else                   snprintf(out, n, "%d COORD - NO NODES COUNTED", sc.meshCoords);
    } else snprintf(out, n, "DEVICE 1/%d - LOCAL STATUS", sc.nRows + 1);
}

inline void composeStrip(scanr::S& s, int W, int H, const MeshScene& sc) {
    using scanr::box; using scanr::txt; using scanr::frame; using scanr::centered; using scanr::fitScale;
    const uieng::MeshLayout L = uieng::mesh(W, H);
    // shared design-1 chrome (header + nav, no tab lit) + STOP/REC record control
    scanr::drawChrome(s, W, H, L.body, {sc.gps, sc.nodes, sc.hasCell, sc.battPct, sc.rec, sc.theme, sc.bridgeOn, sc.pmuOk, sc.charging, sc.poi, sc.sd}, -1);

    const int selected=uieng::meshPage(sc.page,sc.nRows);
    const bool network=selected==uieng::MESH_NETWORK_PAGE;
    auto button=[&](uieng::Rect r,const char* label,bool active=false){
        box(s,r.x,r.y,r.w,r.h,pal::PANEL);
        centered(s,r.x+r.w/2,r.y+16,label,fitScale(label,r.w-4, W>=400?2:1),active?pal::BRIGHT:pal::TEXT);
        if(active)box(s,r.x+4,r.y+r.h-3,r.w-8,2,pal::WITCH_HI);
    };
    button(L.previous,"<");button(L.pager,">");
    char name[48],hint[48];
    if(network){snprintf(name,sizeof name,"NETWORK");if(sc.netId)snprintf(hint,sizeof hint,"NET %d",sc.netId);else snprintf(hint,sizeof hint,"NET OPEN");}
    else if(selected==0){snprintf(name,sizeof name,"THIS HANDHELD");handheldHint(sc,hint,sizeof hint);}
    else{const auto& n=sc.rows[(selected-1)/2];snprintf(name,sizeof name,"NODE %02X:%02X:%02X",n.id[0],n.id[1],n.id[2]);snprintf(hint,sizeof hint,"DEVICE %d/%d - %s",(selected+1)/2+1,sc.nRows+1,selected%2?"SCANNING":"CONNECTION");}
    centered(s,L.title.x+L.title.w/2,L.title.y+5,name,fitScale(name,L.title.w,2),pal::BRIGHT);
    centered(s,L.title.x+L.title.w/2,L.title.y+28,hint,1,pal::DIM);
    box(s,L.list.x,L.list.y,L.list.w,L.list.h,ui::Canvas::mix(pal::VOID,pal::PANEL,1,3));
    if(network){
        char enroll[24];if(sc.enrollSecs)snprintf(enroll,sizeof enroll,"PAIR %dS",sc.enrollSecs);else snprintf(enroll,sizeof enroll,"ENROLL NODES");
        button(L.enroll,enroll,sc.enrollSecs>0);button(L.reset,"RESET NETWORK");
        if(L.list.h>=70)centered(s,L.list.x+L.list.w/2,L.list.y+58,"ENROLL ADOPTS NODES / RESET OPENS THE NETWORK",1,pal::DIM);
    }else     if(selected==0){
        const auto& d=sc.local;char lines[5][96]{};
        if(d.valid){
            snprintf(lines[0],96,"LOCAL %s %uMS",d.active?"ACTIVE":"PASSIVE",d.dwellMs);
            snprintf(lines[1],96,"SCANS %lu DONE %lu",(unsigned long)d.started,(unsigned long)d.completed);
            snprintf(lines[2],96,"SCANFAIL %lu OMIT %lu",(unsigned long)d.failed,(unsigned long)d.omitted);
            snprintf(lines[3],96,"BRIDGE %s %s Q%u",d.bridgeMode==1?"WDGO":d.bridgeMode==2?"BISCUIT":"OFF",d.streaming?"STREAM":d.connected?"LINK":"",d.bridgeQueued);
            snprintf(lines[4],96,"RXDROP %lu TXRETRY %lu",(unsigned long)d.rxDrops,(unsigned long)d.txRetries);
        }else snprintf(lines[0],96,"LOCAL DIAGNOSTICS UNAVAILABLE");
        const int scale=W>=400?2:1,step=W>=400?22:8;
        for(int i=0;i<5;i++)txt(s,L.list.x+4,L.list.y+2+i*step,lines[i],fitScale(lines[i],L.list.w-8,scale),i==0?pal::BRIGHT:pal::TEXT);
    }else{
        const MeshNode& nd=sc.rows[(selected-1)/2];
        const auto& d=nd.diag;
        char lines[5][96]{};
        const bool link=(selected-1)%2;
        snprintf(lines[0],96,"%s",link?"NODE CONNECTION HEALTH":"NODE SCANNING SETTINGS");
        if(!link){
            channelLabel(d,lines[1],96);
            if(d.known)snprintf(lines[2],96,"APPLIED %s %uMS %s",d.active?"ACTIVE":"PASSIVE",d.dwellMs,d.camp?"CAMP":"SWEEP");
            else snprintf(lines[2],96,"APPLIED UNKNOWN / NO TELEMETRY");
            snprintf(lines[3],96,"TARGET %s %uMS %s",sc.targetActive?"ACTIVE":"PASSIVE",sc.targetDwell,sc.targetProfile?"DISC":"TRACK");
            snprintf(lines[4],96,"%s / %s / STATUS %luS",timingStatus(sc,d),d.known?(d.profile?"DISC":"TRACK"):"?",(unsigned long)d.ageS);
            if(!d.known)snprintf(lines[4],96,"UNKNOWN / UPDATE NODE FIRMWARE");
        }else{
            if(d.hasReport)snprintf(lines[1],96,"REPORT %luS / HEARD %luS",(unsigned long)d.reportAgeS,(unsigned long)nd.ageS);
            else snprintf(lines[1],96,"REPORT NONE / HEARD %luS",(unsigned long)nd.ageS);
            if(d.known){
                snprintf(lines[2],96,"Q %u/%u PEAK %u DROP %lu",d.depth,d.capacity,d.peak,(unsigned long)d.drops);
                snprintf(lines[3],96,"DELIVERY FAIL %lu API REJECT %lu",(unsigned long)d.sendFailed,(unsigned long)d.reportRejected);
                snprintf(lines[4],96,"SCANFAIL %lu LOSS %u%% %s",(unsigned long)d.scanFailed,nd.lossPct,d.ageS>=10?"STALE":"");
            }else{snprintf(lines[2],96,"QUEUE / DELIVERY UNKNOWN");snprintf(lines[3],96,"UPDATE NODE FIRMWARE");snprintf(lines[4],96,"OBSERVED REPORT LOSS %u%%",nd.lossPct);}
        }
        const int scale=W>=400?2:1;
        for(int i=1;i<5;i++)txt(s,L.list.x+4,L.list.y+3+(i-1)*(W>=400?26:10),lines[i],fitScale(lines[i],L.list.w-8,scale),(!d.known || d.ageS>=10)?pal::AMBER:pal::TEXT);

    }

    if(!network && selected>0){button(L.scanning,"SCANNING",selected%2);button(L.connection,"CONNECTION",!(selected%2));}
    else if(!network){auto overview=L.scanning;overview.w=L.connection.x+L.connection.w-overview.x;button(overview,"OVERVIEW",true);}
    box(s,L.tune.x,L.tune.y,L.tune.w,L.tune.h,pal::PANEL);
    centered(s,L.tune.x+L.tune.w/2,L.tune.y+9,"FLEET",1,pal::TEXT);
    centered(s,L.tune.x+L.tune.w/2,L.tune.y+25,"SETTINGS",1,pal::TEXT);
    button(L.network,"NETWORK",network);

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
