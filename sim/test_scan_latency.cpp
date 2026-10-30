#include "scan_fixture.h"
#include "../ui/scan_present.h"
#include "../ui/frame_present.h"
#include <cassert>
#include <set>
#include <vector>
#include <cstdio>
static void geometry() {
    for(int r : {9,18,23,27,46,69}) {
        std::set<std::pair<int,int>> reference,table;
        for(int a=0;a<1440;++a) {
            double t=a*3.14159265/720.0;
            reference.emplace((int)(r*std::cos(t)+.5),(int)(r*std::sin(t)+.5));
        }
        int n=0;const auto* pts=radargeometry::ring(r,n);assert(pts);
        for(int i=0;i<n;++i)table.emplace(pts[i].x,pts[i].y);
        assert(table==reference); // exact old raster, including asymmetric truncation
    }
}
static std::vector<uint16_t> draw(int w,int h,const scanr::ScanScene& sc) {
    std::vector<uint16_t> px(w*h);
    for(int y=0;y<h;y+=16){scanr::S s{px.data()+y*w,w,y,std::min(16,h-y)};scanr::composeStrip(s,w,h,sc);}
    return px;
}
static void profile(int w,int h,int theme,uint64_t expected) {
    pal::setTheme(theme);auto sc=scanfix::demo(theme);
    auto old=draw(w,h,sc);uint64_t hash=14695981039346656037ull;
    for(auto px:old){hash^=px;hash*=1099511628211ull;}assert(hash==expected);
    sc.animateSweep=true;
    auto base=draw(w,h,sc);
    bool status=false,body=false,field=false;
    auto next=sc;next.now+=50;
    scanpres::sceneDirty(w,h,sc,next,status,body,&field);
    assert(!status && body && field);
    auto same=sc;same.now+=1;
    scanpres::sceneDirty(w,h,sc,same,status,body,&field);assert(!status && !body);
    const auto r=scanr::radarBounds(w,h);
    // Every sweep position, restoring the old ray from the same scene/artwork.
    for(int phase=0;phase<72;++phase) {
        next.now=5000+phase*50;auto partial=base;
        for(int y=r.y;y<r.y+r.h;y+=16){int n=std::min(16,r.y+r.h-y);
            scanr::S s{partial.data()+y*w,w,y,n};scanr::composeStrip(s,w,h,next);}
        auto full=draw(w,h,next);assert(full==partial);
    }
    // Rim contacts may extend beyond the ring by half a square. A fade/expiry
    // must erase those pixels too, even at the exact dirty-region boundary.
    const scanr::FieldBlip rim[]={{270,100,0,0},{90,100,1,0},{0,100,2,0},{180,100,0,0}};
    auto aged=sc;aged.blips=rim;aged.nBlips=4;aged.now=19999;aged.animateSweep=false;
    auto faded=aged;faded.now=20000;auto rimPartial=draw(w,h,aged);
    for(int y=r.y;y<r.y+r.h;y+=16){scanr::S strip{rimPartial.data()+y*w,w,y,std::min(16,r.y+r.h-y)};
        scanr::composeStrip(strip,w,h,faded);}
    assert(rimPartial==draw(w,h,faded));
    scanpres::Engine e;e.reset(w,h);e.enter();
    e.back().begin(theme,5000);e.back().animate(true);e.commit();while(e.pending())e.nextSlice(4);
    e.back().begin(theme,5050);e.back().animate(true);e.commit();
    auto slice=e.nextSlice(100);assert(slice.y0>0 && slice.y1<h-44 && slice.y1-slice.y0<h-88);
    // Limited comparison sees every changed pixel, preserving the other bands.
    std::vector<uint16_t> front(w*h),back(w*h),panel(w*h);
    framepresent::Frame f;f.begin(front.data(),back.data(),w,h);back=base;
    auto sink=[&](uieng::Rect rect,const uint16_t* p,int stride){
        for(int y=rect.y;y<rect.y+rect.h;++y)std::copy_n(p+y*stride+rect.x,rect.w,panel.data()+y*w+rect.x);};
    f.present(false,sink);
    const auto nextPixels=draw(w,h,next);std::copy(nextPixels.begin(),nextPixels.end(),back.begin());
    f.present(false,sink,r.y,r.y+r.h);assert(panel==back);
}
int main() {
    geometry();
    const uint64_t golden[2][3]={{0x2e1a0def75b5ca6eull,0xf7523998baf95599ull,0x05ce089cd868f6e5ull},
        {0xfa6999397f89e695ull,0x1c0a0cf0858756b6ull,0x5f60898d90c7ad6eull}};
    for(int theme=0;theme<3;++theme){profile(480,320,theme,golden[0][theme]);profile(320,240,theme,golden[1][theme]);}
    scanpres::PresentMetrics m;m.work(100,20,30);assert(m.composeUs==100 && m.diffUs==20 && m.transferUs==30);
    assert(m.renderUs==150 && m.maxSliceUs==150 && m.slices==1);m.entryMaxMs=5;m.clear();
    assert(!m.diffUs && !m.entryMaxMs && !m.renderUs);
    puts("scan latency: exact static pixels, 72 sweep phases, narrow repaint and full-work metrics passed");
}
