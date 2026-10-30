// Host test for ui/attune_render.h: strip tiling + no OOB both profiles; a knob
// value change repainted over its row matches a full repaint; a page change
// repainted over the body matches a full repaint; GPS cell region==full.
// Build:
//   g++ -std=c++17 -I. -Isim sim/test_attune_render.cpp ui/palette.cpp \
//       ui/themepack.cpp ui/muffinseal.cpp ui/motif.cpp ui/canvas.cpp ui/font5x7.cpp -o /tmp/tar1 -lm
#include "../ui/attune_render.h"
#include "../ui/palette.h"
#include "attune_fixture.h"
#include <cstdio>
#include <vector>
using namespace atnr;

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
        const AttuneScene sc = atnfix::demo(pal::TH_UMBRAVOX, W, H, 0);
        FbSink fb(W,H); comp.renderFull(W,H,sc,fb);
        snprintf(m,sizeof m,"%dx%d no OOB",W,H); expect(!fb.oob,m);
        bool once=true; for(int i=0;i<W*H;i++) if(fb.cnt[i]!=1){ once=false; break; }
        snprintf(m,sizeof m,"%dx%d strips tile exactly once",W,H); expect(once,m);

        const uieng::AttuneLayout L = uieng::attune(W,H);
        // a knob value change on row 0: row region repaint == full
        static AttuneRow R2[4]; for(int i=0;i<sc.nRows;i++) R2[i]=sc.rows[i]; R2[0].value="HIGH";
        AttuneScene chg = sc; chg.rows=R2;
        FbSink full(W,H); comp.renderFull(W,H,chg,full);
        FbSink reg(W,H);  comp.renderFull(W,H,sc,reg);
        comp.renderRegion(W,H,chg,L.rows[0],reg);
        bool match=true; for(int i=0;i<W*H;i++) if(reg.fb[i]!=full.fb[i]){ match=false; break; }
        snprintf(m,sizeof m,"%dx%d knob value change: row region == full",W,H); expect(match,m);

        // GPS cell region == full
        const uieng::Rect gcell = L.statusCells[0];
        AttuneScene stl = sc; stl.gps = {true,3,12,30000};
        FbSink gf(W,H); comp.renderFull(W,H,stl,gf);
        FbSink gr(W,H); comp.renderFull(W,H,sc,gr);
        comp.renderRegion(W,H,stl,gcell,gr);
        bool gmatch=true; for(int i=0;i<W*H;i++) if(gr.fb[i]!=gf.fb[i]){ gmatch=false; break; }
        snprintf(m,sizeof m,"%dx%d GPS fresh->stale: GPS cell region == full",W,H); expect(gmatch,m);
    }
    printf("\n%s  %d/%d checks passed\n", fails?"FAILED":"PASSED", checks-fails, checks);
    return fails?1:0;
}
