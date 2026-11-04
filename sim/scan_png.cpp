// Render static SCAN to PNG from a caller-supplied scene (scanfix::demo), at both
// native 1x and 2x, for comparison with the approved geometry.
//   g++ -std=c++17 -I. sim/scan_png.cpp ui/palette.cpp ui/themepack.cpp \
//       ui/muffinseal.cpp ui/motif.cpp ui/canvas.cpp ui/font5x7.cpp -o /tmp/scanpng -lm
//   /tmp/scanpng [themeId]     # writes scan-{W}-t{theme}[@2x].png for 480 and 320
#include "../ui/scan_render.h"
#include "../ui/palette.h"
#include "scan_fixture.h"
#include "png_writer.h"
#include <cstdio>
#include <cstdlib>
#include <vector>
using namespace scanr;

struct PngFb : Sink {
    int W,H; std::vector<uint16_t> fb;
    PngFb(int w,int h):W(w),H(h),fb(w*h,0){}
    void strip(int x,int y,int w,int h,const uint16_t* px) override {
        for(int ry=0;ry<h;ry++) for(int rx=0;rx<w;rx++){ const int gx=x+rx,gy=y+ry;
            if(gx>=0&&gx<W&&gy>=0&&gy<H) fb[gy*W+gx]=px[ry*w+rx]; }
    }
};

int main(int argc, char** argv){
    const uint8_t th = argc>1 ? (uint8_t)atoi(argv[1]) : (uint8_t)pal::TH_UMBRAVOX;
    pal::setTheme(th);
    Compositor comp;
    const ScanScene sc = scanfix::demo(th);     // caller-supplied scene; artwork follows theme
    struct { int W,H; } cs[2] = {{480,320},{320,240}};
    for (auto& c : cs){
        PngFb fb(c.W,c.H);
        comp.renderFull(c.W,c.H,sc,fb);
        char f1[48], f2[48];
        snprintf(f1,sizeof f1,"scan-%d-t%u.png",   c.W, th);   // native 1x
        snprintf(f2,sizeof f2,"scan-%d-t%u@2x.png", c.W, th);  // 2x preview
        const bool ok1 = png::write(f1, fb.fb.data(), c.W, c.H, 1);
        const bool ok2 = png::write(f2, fb.fb.data(), c.W, c.H, 2);
        printf("%s %s (%dx%d, theme %d, 1x)\n", ok1?"wrote":"FAILED", f1, c.W, c.H, th);
        printf("%s %s (%dx%d, theme %d, 2x)\n", ok2?"wrote":"FAILED", f2, c.W, c.H, th);
    }
    return 0;
}
