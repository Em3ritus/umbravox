// Render static SCAN to PNG for comparison with the approved geometry.
//   g++ -std=c++17 -I. sim/scan_png.cpp ui/palette.cpp ui/themepack.cpp \
//       ui/muffinseal.cpp ui/motif.cpp ui/canvas.cpp ui/font5x7.cpp -o /tmp/scanpng -lm
//   /tmp/scanpng [themeId]            # writes scan-480.png, scan-320.png (2x)
#include "../ui/scan_render.h"
#include "../ui/palette.h"
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
    Compositor comp; ScanScene sc;
    struct { int W,H; const char* f; } cs[2] = {{480,320,"scan-480.png"},{320,240,"scan-320.png"}};
    for (auto& c : cs){
        PngFb fb(c.W,c.H);
        comp.renderFull(c.W,c.H,sc,fb);
        const bool ok = png::write(c.f, fb.fb.data(), c.W, c.H, 2);
        printf("%s %s (%dx%d, theme %d, 2x)\n", ok?"wrote":"FAILED", c.f, c.W, c.H, th);
    }
    return 0;
}
