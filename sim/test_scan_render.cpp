// Host test for ui/scan_render.h (static SCAN strip compositor). Verifies, with
// colors from the real theme model (pal::setTheme) and caller-supplied scene data
// (scanfix::demo -- no embedded fixtures in the renderer):
//   - clipping: no strip write lands outside the logical canvas;
//   - strip boundaries + complete repaint: a full render writes every pixel
//     EXACTLY once (strips tile the canvas with no gaps or overlaps);
//   - hero separation + no-intrusion + fixed channel plan (collisions reviewed);
//   - rendered spectrum coverage: the fixture's channels actually paint
//     kChannels bars (not merely that kChannels is a constant);
//   - empty data: a scene with no contacts/channels still tiles cleanly;
//   - background restoration + state transitions: repainting only the affected
//     region yields the same image as a full repaint (old pixels erased), at
//     both canvas sizes.
// Build:
//   g++ -std=c++17 -I. sim/test_scan_render.cpp ui/palette.cpp ui/themepack.cpp \
//       ui/muffinseal.cpp ui/motif.cpp ui/canvas.cpp ui/font5x7.cpp -o /tmp/tsr -lm
#include "../ui/scan_render.h"
#include "../ui/palette.h"
#include "scan_fixture.h"
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
    uint16_t at(int x,int y) const { return fb[y*W+x]; }
};

static int fails=0, checks=0;
static void expect(bool c,const char* m){ ++checks; if(!c){++fails; printf("  FAIL %s\n",m);} else printf("  ok   %s\n",m); }

// Count how many of the fixed channel columns actually carry a painted bar in
// the rendered image -- rendered coverage, not a constant. A bar for channel i
// occupies column sx0+i*pitch at the row just above barBase whenever activity>0.
static int renderedBars(const FbSink& fb, int W, int H) {
    const uieng::Rect hr = uieng::scan(W,H).hero;
    const HeroSub hs = heroSub(hr);
    // match the compositor's design-1 spectrum geometry (36 bars @8px from 172)
    const int sx0=(W>=400)?172:hs.spectrum.x;
    const int pitch=(W>=400)?8:((hs.spectrum.w/kSpecBars>=2)?hs.spectrum.w/kSpecBars:2);
    int n=0;
    for (int i=0;i<kSpecBars;i++){
        const uint16_t want = i<kSpecPink ? pal::MAGENTA : pal::ICE;
        const int x=sx0+i*pitch, y=hs.barBase-1;
        if (x>=0&&x<W&&y>=0&&y<H && fb.at(x,y)==want) ++n;
    }
    return n;
}

int main(){
    pal::setTheme(pal::TH_UMBRAVOX);     // colors from the existing theme model
    Compositor comp;
    printf("strip buffer: %d bytes (%d px wide x %d rows)\n",
           Compositor::BUF_BYTES, Compositor::MAXW, Compositor::STRIP_H);

    for (int P=0;P<2;++P){
        const int W=P?320:480, H=P?240:320;
        const ScanScene sc = scanfix::demo(pal::TH_UMBRAVOX);   // caller-supplied scene
        FbSink fb(W,H);
        comp.renderFull(W,H,sc,fb);
        char m[80];
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

        // rendered coverage: the compositor paints all kSpecBars design-1 bars
        snprintf(m,sizeof m,"%dx%d spectrum renders all %d bars (design #1)",W,H,kSpecBars);
        expect(renderedBars(fb,W,H)==kSpecBars, m);

        // empty data: no contacts, no channels -> still tiles cleanly, no oob
        ScanScene empty; empty.theme=pal::TH_UMBRAVOX;      // all pointers null, counts 0
        FbSink ef(W,H); comp.renderFull(W,H,empty,ef);
        bool eonce=true; for(int i=0;i<W*H;i++) if(ef.cnt[i]!=1){ eonce=false; break; }
        snprintf(m,sizeof m,"%dx%d empty scene renders cleanly (tiles once, no oob)",W,H);
        expect(eonce && !ef.oob, m);
        snprintf(m,sizeof m,"%dx%d empty scene paints no spectrum bars",W,H);
        expect(renderedBars(ef,W,H)==0, m);

        // state transition: rec Off -> On. Design #1 puts REC/STOP on the NAV
        // bar, so repaint the STOP region (not a status cell) and compare to full.
        const uieng::ScanLayout L = uieng::scan(W,H);
        ScanScene on = sc; on.rec = Rec::On;
        FbSink full(W,H); comp.renderFull(W,H,on,full);        // reference: full render, rec On
        FbSink reg(W,H);  comp.renderFull(W,H,sc,reg);         // start from rec Off
        comp.renderRegion(W,H,on,L.stop,reg);                  // repaint only the STOP strips
        bool match=true; for(int i=0;i<W*H;i++) if(reg.fb[i]!=full.fb[i]){ match=false; break; }
        snprintf(m,sizeof m,"%dx%d rec Off->On: STOP region repaint == full (old label erased)",W,H);
        expect(match, m);

        // GPS transitions acquiring->fresh->stale: repaint only the GPS cell,
        // compare to a full repaint (old label pixels must be erased).
        const uieng::Rect gcell = L.statusCells[0];
        {   ScanScene acq=sc; acq.gps={false,0,0,0};            // ACQUIRING
            ScanScene fsh=sc; fsh.gps={true,3,12,500};          // 3D 12SV
            FbSink ff(W,H); comp.renderFull(W,H,fsh,ff);
            FbSink rr(W,H); comp.renderFull(W,H,acq,rr);
            comp.renderRegion(W,H,fsh,gcell,rr);
            bool mt=true; for(int i=0;i<W*H;i++) if(rr.fb[i]!=ff.fb[i]){ mt=false; break; }
            snprintf(m,sizeof m,"%dx%d GPS acquiring->fresh: region==full (ACQUIRING label erased)",W,H);
            expect(mt, m); }
        {   ScanScene fsh=sc; fsh.gps={true,3,12,500};          // Fresh
            ScanScene stl=sc; stl.gps={true,3,12,30000};        // Stale
            FbSink ff(W,H); comp.renderFull(W,H,stl,ff);
            FbSink rr(W,H); comp.renderFull(W,H,fsh,rr);
            comp.renderRegion(W,H,stl,gcell,rr);
            bool mt=true; for(int i=0;i<W*H;i++) if(rr.fb[i]!=ff.fb[i]){ mt=false; break; }
            snprintf(m,sizeof m,"%dx%d GPS fresh->stale: region==full (old label erased)",W,H);
            expect(mt, m); }

        // A visible contact expires: advancing the clock past 60s drops its
        // marker. Repaint only the hero region; it must match a full repaint
        // (the old marker pixels are removed) and must actually change pixels.
        {   ScanScene vis=sc;                                   // now=5000 -> contacts fresh
            ScanScene exp=sc; exp.now = 5000 + kContactExpireMs + 2000;  // all contacts expired
            FbSink ff(W,H); comp.renderFull(W,H,exp,ff);
            FbSink rr(W,H); comp.renderFull(W,H,vis,rr);
            comp.renderRegion(W,H,exp,L.hero,rr);
            bool mt=true; for(int i=0;i<W*H;i++) if(rr.fb[i]!=ff.fb[i]){ mt=false; break; }
            snprintf(m,sizeof m,"%dx%d contact expiry: hero region==full (old marker pixels removed)",W,H);
            expect(mt, m);
            FbSink vf(W,H); comp.renderFull(W,H,vis,vf);
            bool ch=false; for(int i=0;i<W*H;i++) if(vf.fb[i]!=ff.fb[i]){ ch=true; break; }
            snprintf(m,sizeof m,"%dx%d contact expiry actually removes marker pixels (sanity)",W,H);
            expect(ch, m); }
    }

    // background restoration after a count change (full-size)
    {
        const int W=480,H=320; const uieng::ScanLayout L=uieng::scan(W,H);
        ScanScene a = scanfix::demo(pal::TH_UMBRAVOX); a.wifi=1665;
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
