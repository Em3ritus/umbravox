// Host test for ui/mesh_render.h (static MESH compositor): strip tiling + no
// OOB both profiles; empty node list tiles cleanly; a pairing-window change
// repainted over the control row matches a full repaint; GPS cell region==full.
// Build:
//   g++ -std=c++17 -I. -Isim sim/test_mesh_render.cpp ui/palette.cpp \
//       ui/themepack.cpp ui/muffinseal.cpp ui/motif.cpp ui/canvas.cpp ui/font5x7.cpp -o /tmp/tmr1 -lm
#include "../ui/mesh_render.h"
#include "../ui/palette.h"
#include "mesh_fixture.h"
#include <cstdio>
#include <vector>
using namespace mshr;

struct FbSink : Sink {
    int W,H; std::vector<uint16_t> fb; std::vector<int> cnt; bool oob=false;
    FbSink(int w,int h):W(w),H(h),fb(w*h,0),cnt(w*h,0){}
    void strip(int x,int y,int w,int h,const uint16_t* px) override {
        for(int ry=0;ry<h;ry++) for(int rx=0;rx<w;rx++){ const int gx=x+rx,gy=y+ry;
            if(gx<0||gx>=W||gy<0||gy>=H){ oob=true; continue; } fb[gy*W+gx]=px[ry*w+rx]; ++cnt[gy*W+gx]; } }
};
static int fails=0, checks=0;
static void expect(bool c,const char* m){ ++checks; if(!c){++fails;printf("  FAIL %s\n",m);} else printf("  ok   %s\n",m); }

int main(){
    pal::setTheme(pal::TH_UMBRAVOX);
    Compositor comp;
    for (int P=0;P<2;++P){
        const int W=P?320:480, H=P?240:320; char m[96];
        const MeshScene sc = mshfix::demo(pal::TH_UMBRAVOX);
        FbSink fb(W,H); comp.renderFull(W,H,sc,fb);
        snprintf(m,sizeof m,"%dx%d no OOB",W,H); expect(!fb.oob,m);
        bool once=true; for(int i=0;i<W*H;i++) if(fb.cnt[i]!=1){ once=false; break; }
        snprintf(m,sizeof m,"%dx%d strips tile exactly once",W,H); expect(once,m);

        MeshScene empty = mshfix::demo(pal::TH_UMBRAVOX, true);
        FbSink ef(W,H); comp.renderFull(W,H,empty,ef);
        bool eonce=true; for(int i=0;i<W*H;i++) if(ef.cnt[i]!=1){ eonce=false; break; }
        snprintf(m,sizeof m,"%dx%d empty node list tiles cleanly",W,H); expect(eonce && !ef.oob,m);

        const uieng::MeshLayout L = uieng::mesh(W,H);
        MeshScene beforePair=sc;beforePair.page=uieng::MESH_NETWORK_PAGE;
        MeshScene pair = beforePair; pair.enrollSecs = 30;             // ENROLL -> PAIR 30S
        FbSink full(W,H); comp.renderFull(W,H,pair,full);
        FbSink reg(W,H);  comp.renderFull(W,H,beforePair,reg);
        comp.renderRegion(W,H,pair,L.enroll,reg);
        bool match=true; for(int i=0;i<W*H;i++) if(reg.fb[i]!=full.fb[i]){ match=false; break; }
        snprintf(m,sizeof m,"%dx%d pairing window: ENROLL region == full (old label erased)",W,H);
        expect(match,m);

        // Radio/Link page transitions and stale/unknown values erase old text.
        MeshNode node{{0,0,1},2,3,true};MeshScene radio=sc;radio.rows=&node;radio.nRows=1;radio.page=1;
        node.diag.known=true;node.diag.dwellMs=220;node.diag.assignedCount=1;node.diag.assignedChannels[0]=149;
        FbSink previous(W,H);comp.renderFull(W,H,radio,previous);
        for(int state=0;state<6;state++){
            if(state==0)radio.page=2;
            if(state==1){node.diag.ageS=11;node.diag.hasReport=true;node.diag.reportAgeS=12;}
            if(state==2)node.diag.known=false;
            if(state==3)radio.page=1;
            if(state==4)radio.page=uieng::MESH_NETWORK_PAGE;
            if(state==5)radio.page=0;
            FbSink expected(W,H);comp.renderFull(W,H,radio,expected);comp.renderRegion(W,H,radio,L.body,previous);
            expect(previous.fb==expected.fb && !previous.oob,"node page/state body repaint matches full frame");
        }
        const uieng::Rect gcell = L.statusCells[0];
        MeshScene stl = sc; stl.gps = {true,3,12,30000};
        FbSink gf(W,H); comp.renderFull(W,H,stl,gf);
        FbSink gr(W,H); comp.renderFull(W,H,sc,gr);
        comp.renderRegion(W,H,stl,gcell,gr);
        bool gmatch=true; for(int i=0;i<W*H;i++) if(gr.fb[i]!=gf.fb[i]){ gmatch=false; break; }
        snprintf(m,sizeof m,"%dx%d GPS fresh->stale: GPS cell region == full",W,H); expect(gmatch,m);
    }
    printf("\n%s  %d/%d checks passed\n", fails?"FAILED":"PASSED", checks-fails, checks);
    return fails?1:0;
}
