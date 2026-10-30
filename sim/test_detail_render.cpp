// Host test for ui/detail_render.h: strip tiling + no OOB both profiles; the
// "GONE" invalid state tiles cleanly; a new sighting (rssi + history) repainted
// over the body matches a full repaint; GPS cell region==full.
// Build:
//   g++ -std=c++17 -I. -Isim sim/test_detail_render.cpp ui/palette.cpp \
//       ui/themepack.cpp ui/muffinseal.cpp ui/motif.cpp ui/canvas.cpp ui/font5x7.cpp -o /tmp/tdtr1 -lm
#include "../ui/detail_render.h"
#include "../ui/palette.h"
#include "detail_fixture.h"
#include <cstdio>
#include <vector>
using namespace detr;

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
        const DetailScene sc = detfix::demo(pal::TH_UMBRAVOX);
        FbSink fb(W,H); comp.renderFull(W,H,sc,fb);
        snprintf(m,sizeof m,"%dx%d no OOB",W,H); expect(!fb.oob,m);
        bool once=true; for(int i=0;i<W*H;i++) if(fb.cnt[i]!=1){ once=false; break; }
        snprintf(m,sizeof m,"%dx%d strips tile exactly once",W,H); expect(once,m);

        DetailScene gone = detfix::demo(pal::TH_UMBRAVOX, false);
        FbSink gf(W,H); comp.renderFull(W,H,gone,gf);
        bool gonce=true; for(int i=0;i<W*H;i++) if(gf.cnt[i]!=1){ gonce=false; break; }
        snprintf(m,sizeof m,"%dx%d GONE (invalid) tiles cleanly",W,H); expect(gonce && !gf.oob,m);

        const uieng::DetailLayout L = uieng::detail(W,H);
        // a new sighting: rssi + extended history -> body region == full
        static int8_t H2[13]; for(int i=0;i<12;i++) H2[i]=detfix::HIST[i]; H2[12]=-55;
        DetailScene nw = sc; nw.rssi=-55; nw.hist=H2; nw.histN=13; nw.lastSeen="1S";
        FbSink full(W,H); comp.renderFull(W,H,nw,full);
        FbSink reg(W,H);  comp.renderFull(W,H,sc,reg);
        uieng::Rect bodyRect{L.body.x,L.body.y,L.body.w,L.body.h};
        comp.renderRegion(W,H,nw,bodyRect,reg);
        bool match=true; for(int i=0;i<W*H;i++) if(reg.fb[i]!=full.fb[i]){ match=false; break; }
        snprintf(m,sizeof m,"%dx%d new sighting: body region == full",W,H); expect(match,m);

        const uieng::Rect gcell = L.statusCells[0];
        DetailScene stl = sc; stl.gps = {true,3,12,30000};
        FbSink sf(W,H); comp.renderFull(W,H,stl,sf);
        FbSink sr(W,H); comp.renderFull(W,H,sc,sr);
        comp.renderRegion(W,H,stl,gcell,sr);
        bool gmatch=true; for(int i=0;i<W*H;i++) if(sr.fb[i]!=sf.fb[i]){ gmatch=false; break; }
        snprintf(m,sizeof m,"%dx%d GPS fresh->stale: GPS cell region == full",W,H); expect(gmatch,m);
    }
    printf("\n%s  %d/%d checks passed\n", fails?"FAILED":"PASSED", checks-fails, checks);
    return fails?1:0;
}
