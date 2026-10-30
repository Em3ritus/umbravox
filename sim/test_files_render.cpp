// Host test for ui/files_render.h: strip tiling + no OOB both profiles; NO CARD
// and NO LOGS states tile cleanly; opening the confirm modal repainted over the
// body matches a full repaint; GPS cell region==full.
// Build:
//   g++ -std=c++17 -I. -Isim sim/test_files_render.cpp ui/palette.cpp \
//       ui/themepack.cpp ui/muffinseal.cpp ui/motif.cpp ui/canvas.cpp ui/font5x7.cpp -o /tmp/tfr1 -lm
#include "../ui/files_render.h"
#include "../ui/palette.h"
#include "files_fixture.h"
#include <cstdio>
#include <vector>
using namespace flsr;

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
        const FilesScene sc = flsfix::demo(pal::TH_UMBRAVOX);
        FbSink fb(W,H); comp.renderFull(W,H,sc,fb);
        snprintf(m,sizeof m,"%dx%d no OOB",W,H); expect(!fb.oob,m);
        bool once=true; for(int i=0;i<W*H;i++) if(fb.cnt[i]!=1){ once=false; break; }
        snprintf(m,sizeof m,"%dx%d strips tile exactly once",W,H); expect(once,m);

        FilesScene noCard = sc; noCard.card=false; noCard.rows=nullptr; noCard.nRows=0; noCard.sel=-1; noCard.deletable=false;
        FbSink nf(W,H); comp.renderFull(W,H,noCard,nf);
        bool nonce=true; for(int i=0;i<W*H;i++) if(nf.cnt[i]!=1){ nonce=false; break; }
        snprintf(m,sizeof m,"%dx%d NO CARD tiles cleanly",W,H); expect(nonce && !nf.oob,m);

        FilesScene noLogs = sc; noLogs.rows=nullptr; noLogs.nRows=0; noLogs.sel=-1; noLogs.deletable=false; noLogs.total=0;
        FbSink lf(W,H); comp.renderFull(W,H,noLogs,lf);
        bool lonce=true; for(int i=0;i<W*H;i++) if(lf.cnt[i]!=1){ lonce=false; break; }
        snprintf(m,sizeof m,"%dx%d NO LOGS tiles cleanly",W,H); expect(lonce && !lf.oob,m);

        // opening the confirm modal: body region == full (modal drawn over list + bar)
        const uieng::FilesLayout L = uieng::files(W,H);
        const FilesScene cf = flsfix::confirm(pal::TH_UMBRAVOX);
        FbSink full(W,H); comp.renderFull(W,H,cf,full);
        FbSink reg(W,H);  comp.renderFull(W,H,sc,reg);
        uieng::Rect bodyRect{L.body.x,L.body.y,L.body.w,L.body.h};
        comp.renderRegion(W,H,cf,bodyRect,reg);
        bool match=true; for(int i=0;i<W*H;i++) if(reg.fb[i]!=full.fb[i]){ match=false; break; }
        snprintf(m,sizeof m,"%dx%d confirm modal: body region == full",W,H); expect(match,m);
        bool changed=false; for(int i=0;i<W*H;i++) if(full.fb[i]!=fb.fb[i]){ changed=true; break; }
        snprintf(m,sizeof m,"%dx%d confirm modal actually changes the body (sanity)",W,H); expect(changed,m);

        const uieng::Rect gcell = L.statusCells[0];
        FilesScene stl = sc; stl.gps = {true,3,12,30000};
        FbSink gf(W,H); comp.renderFull(W,H,stl,gf);
        FbSink gr(W,H); comp.renderFull(W,H,sc,gr);
        comp.renderRegion(W,H,stl,gcell,gr);
        bool gmatch=true; for(int i=0;i<W*H;i++) if(gr.fb[i]!=gf.fb[i]){ gmatch=false; break; }
        snprintf(m,sizeof m,"%dx%d GPS fresh->stale: GPS cell region == full",W,H); expect(gmatch,m);
    }
    printf("\n%s  %d/%d checks passed\n", fails?"FAILED":"PASSED", checks-fails, checks);
    return fails?1:0;
}
