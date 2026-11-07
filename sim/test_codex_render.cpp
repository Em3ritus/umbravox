// Host test for ui/codex_render.h (static CODEX strip compositor). Verifies,
// with colors from the real theme model and caller-supplied scene data:
//   - clipping + strip tiling: a full render writes every pixel EXACTLY once,
//     no out-of-bounds, both profiles;
//   - filter highlight: the active cell is lit, and a filter change repainted
//     only over the filter bar matches a full repaint (old highlight erased);
//   - empty list: a listTotal==0 scene still tiles cleanly;
//   - background restoration: a GPS status change repainted over just the GPS
//     cell matches a full repaint (old label erased).
// Build:
//   g++ -std=c++17 -I. -Isim sim/test_codex_render.cpp ui/palette.cpp \
//       ui/themepack.cpp ui/muffinseal.cpp ui/motif.cpp ui/canvas.cpp ui/font5x7.cpp -o /tmp/tcr -lm
#include "../ui/codex_render.h"
#include "../ui/palette.h"
#include "codex_fixture.h"
#include <cstdio>
#include <vector>
using namespace codr;

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
    printf("strip buffer: %d bytes\n", Compositor::BUF_BYTES);

    for (int P=0;P<2;++P){
        const int W=P?320:480, H=P?240:320;
        const CodexScene sc = codfix::demo(pal::TH_UMBRAVOX, F_WIFI);
        FbSink fb(W,H); comp.renderFull(W,H,sc,fb);
        char m[96];
        snprintf(m,sizeof m,"%dx%d clipping: no out-of-bounds strip writes",W,H); expect(!fb.oob,m);
        bool once=true; for(int i=0;i<W*H;i++) if(fb.cnt[i]!=1){ once=false; break; }
        snprintf(m,sizeof m,"%dx%d strips tile exactly: every pixel written once",W,H); expect(once,m);

        const uieng::CodexLayout L = uieng::codex(W,H);

        // filter change WIFI->BLE: repaint only the filter bar, match full
        CodexScene ble = sc; ble.filter = F_BLE;
        FbSink full(W,H); comp.renderFull(W,H,ble,full);
        FbSink reg(W,H);  comp.renderFull(W,H,sc,reg);
        comp.renderRegion(W,H,ble,L.filterBar,reg);
        bool match=true; for(int i=0;i<W*H;i++) if(reg.fb[i]!=full.fb[i]){ match=false; break; }
        snprintf(m,sizeof m,"%dx%d filter WIFI->BLE: filter-bar region == full (old highlight erased)",W,H);
        expect(match,m);
        bool changed=false; for(int i=0;i<W*H;i++) if(reg.fb[i]!=fb.fb[i]){ changed=true; break; }
        snprintf(m,sizeof m,"%dx%d filter change actually moves the highlight (sanity)",W,H); expect(changed,m);

        // empty list tiles cleanly
        CodexScene empty; empty.theme=pal::TH_UMBRAVOX; empty.filter=F_WIFI;   // listTotal 0
        FbSink ef(W,H); comp.renderFull(W,H,empty,ef);
        bool eonce=true; for(int i=0;i<W*H;i++) if(ef.cnt[i]!=1){ eonce=false; break; }
        snprintf(m,sizeof m,"%dx%d empty list renders cleanly (tiles once, no oob)",W,H);
        expect(eonce && !ef.oob, m);

        // GPS fresh->stale: repaint only the GPS cell, match full
        const uieng::Rect gcell = L.statusCells[0];
        CodexScene stl = sc; stl.gps = {true,3,12,30000};
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
