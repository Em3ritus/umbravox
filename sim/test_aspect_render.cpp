// Host test for ui/aspect_render.h: strip tiling + no OOB both profiles; a
// sweep-name change repainted over its row matches a full repaint; GPS cell
// region==full; a not-live ALERT still tiles cleanly.
// Build:
//   g++ -std=c++17 -I. -Isim sim/test_aspect_render.cpp ui/palette.cpp \
//       ui/themepack.cpp ui/muffinseal.cpp ui/motif.cpp ui/canvas.cpp ui/font5x7.cpp -o /tmp/tasr1 -lm
#include "../ui/aspect_render.h"
#include "../ui/palette.h"
#include "aspect_fixture.h"
#include <cstdio>
#include <vector>
using namespace aspr;

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
        const AspectScene sc = aspfix::demo(pal::TH_UMBRAVOX);
        FbSink fb(W,H); comp.renderFull(W,H,sc,fb);
        snprintf(m,sizeof m,"%dx%d no OOB",W,H); expect(!fb.oob,m);
        bool once=true; for(int i=0;i<W*H;i++) if(fb.cnt[i]!=1){ once=false; break; }
        snprintf(m,sizeof m,"%dx%d strips tile exactly once",W,H); expect(once,m);

        AspectScene nl = aspfix::demo(pal::TH_UMBRAVOX, false);   // ALERT not live
        FbSink nf(W,H); comp.renderFull(W,H,nl,nf);
        bool nonce=true; for(int i=0;i<W*H;i++) if(nf.cnt[i]!=1){ nonce=false; break; }
        snprintf(m,sizeof m,"%dx%d not-live ALERT tiles cleanly",W,H); expect(nonce && !nf.oob,m);

        const uieng::AspectLayout L = uieng::aspect(W,H);
        AspectScene chg = sc; chg.sweepName = "PULSE";
        FbSink full(W,H); comp.renderFull(W,H,chg,full);
        FbSink reg(W,H);  comp.renderFull(W,H,sc,reg);
        comp.renderRegion(W,H,chg,L.rows[1],reg);
        bool match=true; for(int i=0;i<W*H;i++) if(reg.fb[i]!=full.fb[i]){ match=false; break; }
        snprintf(m,sizeof m,"%dx%d sweep change: row region == full",W,H); expect(match,m);

        AspectScene labels=sc;labels.plain=!sc.plain;
        FbSink lf(W,H),lp(W,H);comp.renderFull(W,H,labels,lf);comp.renderFull(W,H,sc,lp);
        comp.renderRegion(W,H,labels,L.labels,lp);
        expect(lp.fb==lf.fb && lf.fb!=fb.fb,"Labels toggle erases previous value; region equals full repaint");
        const uieng::Rect gcell = L.statusCells[0];
        AspectScene stl = sc; stl.gps = {true,3,12,30000};
        FbSink gf(W,H); comp.renderFull(W,H,stl,gf);
        FbSink gr(W,H); comp.renderFull(W,H,sc,gr);
        comp.renderRegion(W,H,stl,gcell,gr);
        bool gmatch=true; for(int i=0;i<W*H;i++) if(gr.fb[i]!=gf.fb[i]){ gmatch=false; break; }
        snprintf(m,sizeof m,"%dx%d GPS fresh->stale: GPS cell region == full",W,H); expect(gmatch,m);
    }
    printf("\n%s  %d/%d checks passed\n", fails?"FAILED":"PASSED", checks-fails, checks);
    return fails?1:0;
}
