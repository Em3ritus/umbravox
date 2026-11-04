// Host test: the Waveshare (480x320 engine) radar carries the same theme artwork as the CYD.
//   CVLT OF BAPH0MAU5 -> the goat-face seal fills the radar, no rune ring.
//   COSMIC MUFFIN     -> the muffin picture fills the radar disc, a ring of stars instead of runes.
//   Every other theme keeps the rune ring.
// Build: make -C sim test_theme_art
#include "../ui/scan_render.h"
#include "../ui/palette.h"
#include "scan_fixture.h"
#include <cstdio>
#include <vector>
using namespace scanr;

struct Fb : Sink {
    int W,H; std::vector<uint16_t> px;
    Fb(int w,int h):W(w),H(h),px(w*h,0){}
    void strip(int x,int y,int w,int h,const uint16_t* p) override {
        for(int ry=0;ry<h;ry++)for(int rx=0;rx<w;rx++){ const int gx=x+rx,gy=y+ry; if(gx>=0&&gx<W&&gy>=0&&gy<H)px[gy*W+gx]=p[ry*w+rx]; } }
};
static int fails=0;
static void expect(bool c,const char* m){ if(!c){++fails; printf("  FAIL %s\n",m);} else printf("  ok   %s\n",m); }

// Pixels in the brightest chalk of the theme's ramp inside radius pct% of the radar. Only the goat-face bitmap uses
// that colour (rings, tail and needle use other tones), so it counts face detail.
static int chalk(uint8_t theme,int pct){
    pal::setTheme(theme);
    Compositor comp; const ScanScene sc=scanfix::demo(theme);
    Fb fb(480,320); comp.renderFull(480,320,sc,fb);
    const auto rb=radarBounds(480,320); const int fr=(rb.w-5)/2, cx=rb.x+fr+2, cy=rb.y+fr+2, R=fr*pct/100;
    int n=0;
    for(int y=cy-R;y<=cy+R;y++)for(int x=cx-R;x<=cx+R;x++){ if((x-cx)*(x-cx)+(y-cy)*(y-cy)>R*R)continue; if(fb.px[y*480+x]==pal::RAMP[3])++n; }
    return n;
}
// Percent of the pixels inside radius pct% of the radar that are not the plain field background.
static int coverage(uint8_t theme,int pct){
    pal::setTheme(theme);
    Compositor comp; const ScanScene sc=scanfix::demo(theme);
    Fb fb(480,320); comp.renderFull(480,320,sc,fb);
    const auto rb=radarBounds(480,320); const int fr=(rb.w-5)/2, cx=rb.x+fr+2, cy=rb.y+fr+2, R=fr*pct/100;
    int n=0,lit=0;
    for(int y=cy-R;y<=cy+R;y++)for(int x=cx-R;x<=cx+R;x++){ if((x-cx)*(x-cx)+(y-cy)*(y-cy)>R*R)continue; ++n; if(fb.px[y*480+x]!=pal::VOID)++lit; }
    return lit*100/n;
}

// Over a picture the tail must tint what is there, not repaint it with the dark fade.
static bool tailBlendsOverPicture(){
    const int W=220,H=220,cx=110,cy=110,r=100; const uint16_t pic=0x7BEF;
    pal::setTheme(pal::TH_COSMICMUFFIN);
    std::vector<uint16_t> px(W*H,pic); S s{px.data(),W,0,H};
    radarTrail(s,cx,cy,r,(uint16_t)300,true);
    const SweepDir d=sweepDir(300); int tinted=0;
    for(int y=0;y<H;y++)for(int x=0;x<W;x++){ const int dx=x-cx,dy=y-cy; if(dx*dx+dy*dy>r*r)continue;
        const int lv=trailLevel(d,dx,dy,kTailLevels);
        const uint16_t want=lv?ui::Canvas::mix(pic,pal::SWEEP,lv,kTailLevels+8):pic;
        if(px[y*W+x]!=want) return false; if(lv)++tinted; }
    return tinted>1000;
}

int main(){
    expect(ringKind(pal::TH_UMBRAVOX)==RING_RUNES,        "default theme keeps the rune ring");
    expect(ringKind(pal::TH_BAPH0MAU5)==RING_NONE,        "CVLT of Baphomau5 has no rune ring");
    expect(ringKind(pal::TH_COSMICMUFFIN)==RING_STARS,    "Cosmic Muffin swaps the runes for a ring of stars");
    const int muffin=coverage(pal::TH_COSMICMUFFIN,60), cvlt=chalk(pal::TH_BAPH0MAU5,60);
    printf("  muffin coverage %d%%, cvlt chalk pixels %d\n",muffin,cvlt);
    expect(muffin>=85, "muffin picture fills the radar disc");
    expect(cvlt>=150,  "goat-face bitmap sits in the middle of the CVLT radar");
    expect(tailBlendsOverPicture(), "the sweep tail tints the muffin picture instead of blacking it out");
    printf(fails?"FAILED\n":"PASSED\n"); return fails?1:0;
}
