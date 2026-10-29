// Host test for ui/scan_render.h (static SCAN strip compositor). Verifies, with
// colors from the real theme model (pal::setTheme):
//   - clipping: no strip write lands outside the logical canvas;
//   - strip boundaries + complete repaint: a full render writes every pixel
//     EXACTLY once (strips tile the canvas with no gaps or overlaps);
//   - background restoration: after changing a count, repainting only that
//     tile's strips yields the same image as a full repaint (old value erased).
// Build:
//   g++ -std=c++17 -I. sim/test_scan_render.cpp ui/palette.cpp ui/themepack.cpp \
//       ui/muffinseal.cpp ui/motif.cpp ui/canvas.cpp ui/font5x7.cpp -o /tmp/tsr -lm
#include "../ui/scan_render.h"
#include "../ui/palette.h"
#include <cstdio>
#include <vector>
#include <algorithm>
using namespace scanr;

struct FbSink : Sink {
    int W, H; std::vector<uint16_t> fb; std::vector<int> cnt; bool oob=false;
    FbSink(int w,int h):W(w),H(h),fb(w*h,0),cnt(w*h,0){}
    void strip(int x,int y,int w,int h,const uint16_t* px) override {
        for(int ry=0;ry<h;ry++) for(int rx=0;rx<w;rx++){
            const int gx=x+rx, gy=y+ry;
            if(gx<0||gx>=W||gy<0||gy>=H){ oob=true; continue; }
            fb[gy*W+gx]=px[ry*w+rx]; ++cnt[gy*W+gx];
        }
    }
};

static int fails=0, checks=0;
static void expect(bool c,const char* m){ ++checks; if(!c){++fails; printf("  FAIL %s\n",m);} else printf("  ok   %s\n",m); }

int main(){
    pal::setTheme(pal::TH_UMBRAVOX);     // colors from the existing theme model
    Compositor comp; ScanScene sc;
    printf("strip buffer: %d bytes (%d px wide x %d rows)\n",
           Compositor::BUF_BYTES, Compositor::MAXW, Compositor::STRIP_H);

    for (int P=0;P<2;++P){
        const int W=P?320:480, H=P?240:320;
        FbSink fb(W,H);
        comp.renderFull(W,H,sc,fb);
        char m[72];
        snprintf(m,sizeof m,"%dx%d clipping: no out-of-bounds strip writes",W,H);
        expect(!fb.oob, m);
        bool once=true; for(int i=0;i<W*H;i++) if(fb.cnt[i]!=1){ once=false; break; }
        snprintf(m,sizeof m,"%dx%d strips tile exactly: every pixel written once",W,H);
        expect(once, m);

        // hero separation + no-intrusion + fixed channel plan (collisions the review flagged)
        const uieng::Rect hr = uieng::scan(W,H).hero;
        const HeroSub hs = heroSub(hr);
        snprintf(m,sizeof m,"%dx%d field clears the channel header (no border/text collision)",W,H);
        expect(hs.field.y >= hr.y+27, m);
        snprintf(m,sizeof m,"%dx%d spectrum sits below the field (no overlap)",W,H);
        expect(hs.spectrum.y >= hs.field.y+hs.field.h, m);
        snprintf(m,sizeof m,"%dx%d tallest spectrum bar stays below the field (no intrusion)",W,H);
        expect(hs.barBase - hs.barMax >= hs.field.y+hs.field.h, m);
        snprintf(m,sizeof m,"%dx%d fixed %d-channel plan (size-independent)",W,H,kChannels);
        expect(kChannels==21, m);
    }

    // background restoration after a count change
    {
        const int W=480,H=320; const uieng::ScanLayout L=uieng::scan(W,H);
        ScanScene a; a.wifi=1665;
        ScanScene b=a; b.wifi=42;
        FbSink full(W,H); comp.renderFull(W,H,b,full);         // reference: full render, new count
        FbSink reg(W,H);  comp.renderFull(W,H,a,reg);          // start from old count
        comp.renderRegion(W,H,b,L.tiles[0],reg);               // repaint only the WIFI tile strips
        bool match=true; for(int i=0;i<W*H;i++) if(reg.fb[i]!=full.fb[i]){ match=false; break; }
        expect(match, "480x320 count change: region repaint == full repaint (old value erased)");
        FbSink oldfb(W,H); comp.renderFull(W,H,a,oldfb);
        bool changed=false; for(int i=0;i<W*H;i++) if(oldfb.fb[i]!=full.fb[i]){ changed=true; break; }
        expect(changed, "480x320 count change actually alters pixels (sanity)");
    }

    printf("\n%s  %d/%d checks passed\n", fails?"FAILED":"PASSED", checks-fails, checks);
    return fails?1:0;
}
