// Host test for ui/menu_render.h (static MENU hub compositor). Verifies, with
// colors from the real theme model and caller-supplied scene data:
//   - clipping + strip tiling: a full render writes every pixel EXACTLY once,
//     no out-of-bounds, both profiles;
//   - label toggle: brand -> plain actually changes pixels, and repainting only
//     the body matches a full repaint (old labels erased);
//   - background restoration: a GPS status change repainted over just the GPS
//     cell matches a full repaint (old label erased).
// Build:
//   g++ -std=c++17 -I. -Isim sim/test_menu_render.cpp ui/palette.cpp \
//       ui/themepack.cpp ui/muffinseal.cpp ui/motif.cpp ui/canvas.cpp ui/font5x7.cpp -o /tmp/tmr_ -lm
#include "../ui/menu_render.h"
#include "../ui/palette.h"
#include "menu_fixture.h"
#include <cstdio>
#include <vector>
using namespace menr;

struct FbSink : Sink {
    int W,H; std::vector<uint16_t> fb; std::vector<int> cnt; bool oob=false;
    FbSink(int w,int h):W(w),H(h),fb(w*h,0),cnt(w*h,0){}
    void strip(int x,int y,int w,int h,const uint16_t* px) override {
        for(int ry=0;ry<h;ry++) for(int rx=0;rx<w;rx++){ const int gx=x+rx,gy=y+ry;
            if(gx<0||gx>=W||gy<0||gy>=H){ oob=true; continue; }
            fb[gy*W+gx]=px[ry*w+rx]; ++cnt[gy*W+gx]; } }
};

static int fails=0, checks=0;
static void expect(bool c,const char* m){ ++checks; if(!c){++fails;printf("  FAIL %s\n",m);} else printf("  ok   %s\n",m); }

int main(){
    pal::setTheme(pal::TH_UMBRAVOX);
    Compositor comp;

    for (int P=0;P<2;++P){
        const int W=P?320:480, H=P?240:320;
        const MenuScene sc = menfix::demo(pal::TH_UMBRAVOX, /*plain*/false);
        FbSink fb(W,H); comp.renderFull(W,H,sc,fb);
        char m[96];
        snprintf(m,sizeof m,"%dx%d clipping: no out-of-bounds strip writes",W,H); expect(!fb.oob,m);
        bool once=true; for(int i=0;i<W*H;i++) if(fb.cnt[i]!=1){ once=false; break; }
        snprintf(m,sizeof m,"%dx%d strips tile exactly: every pixel written once",W,H); expect(once,m);

        const uieng::MenuLayout L = uieng::menu(W,H);

        // label toggle brand -> plain: body region repaint == full repaint
        MenuScene plain = sc; plain.plain = true;
        FbSink full(W,H); comp.renderFull(W,H,plain,full);
        FbSink reg(W,H);  comp.renderFull(W,H,sc,reg);
        comp.renderRegion(W,H,plain,L.body,reg);
        bool match=true; for(int i=0;i<W*H;i++) if(reg.fb[i]!=full.fb[i]){ match=false; break; }
        snprintf(m,sizeof m,"%dx%d labels brand->plain: body region == full (old labels erased)",W,H);
        expect(match,m);
        bool changed=false; for(int i=0;i<W*H;i++) if(full.fb[i]!=fb.fb[i]){ changed=true; break; }
        snprintf(m,sizeof m,"%dx%d label toggle actually relabels tiles (sanity)",W,H); expect(changed,m);

        // GPS fresh->stale: GPS cell region repaint == full
        const uieng::Rect gcell = L.statusCells[0];
        MenuScene stl = sc; stl.gps = {true,3,12,30000};
        FbSink gf(W,H); comp.renderFull(W,H,stl,gf);
        FbSink gr(W,H); comp.renderFull(W,H,sc,gr);
        comp.renderRegion(W,H,stl,gcell,gr);
        bool gmatch=true; for(int i=0;i<W*H;i++) if(gr.fb[i]!=gf.fb[i]){ gmatch=false; break; }
        snprintf(m,sizeof m,"%dx%d GPS fresh->stale: GPS cell region == full (old label erased)",W,H);
        expect(gmatch,m);
    }

    printf("\n%s  %d/%d checks passed\n", fails?"FAILED":"PASSED", checks-fails, checks);
    return fails?1:0;
}
