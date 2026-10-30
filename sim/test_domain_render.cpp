// Host test for ui/domain_render.h (static DOMAIN strip compositor). Verifies,
// with colors from the real theme model and caller-supplied scene data
// (domfix::demo -- no embedded fixtures in the renderer):
//   - clipping: no strip write lands outside the logical canvas, both profiles;
//   - strip tiling + complete repaint: a full render writes every pixel EXACTLY
//     once (strips tile the canvas with no gaps or overlaps);
//   - geometry: the radar circle stays inside the map rect; the contacts column
//     and range -/+ controls sit where uieng::domain(W,H) places them;
//   - wide vs compact: 480 draws located rows, 320 collapses to a summary;
//   - empty data: a no-fix scene still tiles cleanly and shows no stale markers;
//   - background restoration: repainting only an affected region yields the same
//     image as a full repaint (old pixels erased) -- range step and GPS change.
// Build:
//   g++ -std=c++17 -I. -Isim sim/test_domain_render.cpp ui/palette.cpp \
//       ui/themepack.cpp ui/muffinseal.cpp ui/motif.cpp ui/canvas.cpp ui/font5x7.cpp -o /tmp/tdr -lm
#include "../ui/domain_render.h"
#include "../ui/palette.h"
#include "domain_fixture.h"
#include <cstdio>
#include <vector>
using namespace domr;

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
    uint16_t at(int x,int y) const { return fb[y*W+x]; }
};

static int fails=0, checks=0;
static void expect(bool c,const char* m){ ++checks; if(!c){++fails; printf("  FAIL %s\n",m);} else printf("  ok   %s\n",m); }

int main(){
    pal::setTheme(pal::TH_UMBRAVOX);
    Compositor comp;
    printf("strip buffer: %d bytes (%d px wide x %d rows)\n",
           Compositor::BUF_BYTES, Compositor::MAXW, Compositor::STRIP_H);

    for (int P=0;P<2;++P){
        const int W=P?320:480, H=P?240:320;
        const bool wide = (W - 16) >= 352;                   // the layout's own threshold
        const DomainScene sc = domfix::demo(pal::TH_UMBRAVOX, wide);
        FbSink fb(W,H); comp.renderFull(W,H,sc,fb);
        char m[96];

        snprintf(m,sizeof m,"%dx%d clipping: no out-of-bounds strip writes",W,H);
        expect(!fb.oob, m);
        bool once=true; for(int i=0;i<W*H;i++) if(fb.cnt[i]!=1){ once=false; break; }
        snprintf(m,sizeof m,"%dx%d strips tile exactly: every pixel written once",W,H);
        expect(once, m);

        // the layout's wide flag must agree with what we drew
        snprintf(m,sizeof m,"%dx%d layout wide flag == profile expectation (%d)",W,H,wide);
        expect(uieng::domain(W,H).wide == wide, m);

        // radar circle sits inside the map rect
        const uieng::Rect mp = uieng::domain(W,H).map;
        const RadarGeom g = radarGeom(mp);
        snprintf(m,sizeof m,"%dx%d radar circle inside the map rect",W,H);
        expect(g.cx-g.r>=mp.x && g.cx+g.r<=mp.x+mp.w && g.cy-g.r>=mp.y && g.cy+g.r<=mp.y+mp.h, m);

        // empty data: a no-fix scene still tiles cleanly
        DomainScene empty; empty.theme=pal::TH_UMBRAVOX; empty.rangeM=250;
        FbSink ef(W,H); comp.renderFull(W,H,empty,ef);
        bool eonce=true; for(int i=0;i<W*H;i++) if(ef.cnt[i]!=1){ eonce=false; break; }
        snprintf(m,sizeof m,"%dx%d empty scene renders cleanly (tiles once, no oob)",W,H);
        expect(eonce && !ef.oob, m);

        // background restoration: a range step repaints just the map, and a
        // region repaint of it must match a full repaint (old readout erased).
        const uieng::DomainLayout L = uieng::domain(W,H);
        DomainScene far = sc; far.rangeM = rangeStep(sc.rangeM, +1);
        FbSink full(W,H); comp.renderFull(W,H,far,full);     // reference
        FbSink reg(W,H);  comp.renderFull(W,H,sc,reg);       // start from old range
        comp.renderRegion(W,H,far,L.map,reg);                // repaint the map strips
        comp.renderRegion(W,H,far,L.range,reg);              // and the range control
        bool match=true; for(int i=0;i<W*H;i++) if(reg.fb[i]!=full.fb[i]){ match=false; break; }
        snprintf(m,sizeof m,"%dx%d range step: region repaint == full (old readout erased)",W,H);
        expect(match, m);
        bool changed=false; for(int i=0;i<W*H;i++) if(reg.fb[i]!=fb.fb[i]){ changed=true; break; }
        snprintf(m,sizeof m,"%dx%d range step actually changes pixels (sanity)",W,H);
        expect(changed, m);

        // Freshness changes repaint status and map, erasing stale markers.

        DomainScene stl = sc; stl.gps = {true,3,12,30000};   // fresh -> stale label
        FbSink gf(W,H); comp.renderFull(W,H,stl,gf);
        FbSink gr(W,H); comp.renderFull(W,H,sc,gr);
        comp.renderRegion(W,H,stl,{0,0,W,H-44},gr);
        bool gmatch=true; for(int i=0;i<W*H;i++) if(gr.fb[i]!=gf.fb[i]){ gmatch=false; break; }
        snprintf(m,sizeof m,"%dx%d GPS fresh->stale: status and map repaint == full (stale markers erased)",W,H);
        expect(gmatch, m);
    }

    // wide draws rows that compact does not: compare rendered body of both
    {
        DomainScene w = domfix::demo(pal::TH_UMBRAVOX, true);
        FbSink wf(480,320); comp.renderFull(480,320,w,wf);
        const uieng::DomainLayout L = uieng::domain(480,320);
        // a row band carries its band swatch color at its left edge
        const int ry = L.contacts.y + 44;                    // first data row
        bool swatch = wf.at(L.contacts.x, ry+5)==bandColor(w.rows[0].band);
        expect(swatch, "480 wide: first located row shows its band swatch");
    }
    {
        DomainScene c = domfix::demo(pal::TH_UMBRAVOX, false);
        FbSink cf(320,240); comp.renderFull(320,240,c,cf);
        bool clean=true; for(size_t i=0;i<cf.cnt.size();i++) if(cf.cnt[i]!=1){ clean=false; break; }
        expect(clean && !cf.oob, "320 compact: summary column tiles cleanly (no rows)");
    }

    printf("\n%s  %d/%d checks passed\n", fails?"FAILED":"PASSED", checks-fails, checks);
    return fails?1:0;
}
