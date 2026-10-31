#include "scan_fixture.h"
#include "../ui/scan_present.h"
#include "../ui/frame_present.h"
#include <cassert>
#include <set>
#include <vector>
#include <cstdio>
static void geometry() {
    for(int r : {9,13,18,19,23,26,27,28,32,36,38,39,46,55,56,58,65,69,85,98}) {
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
    // Preserve the previous body golden, excluding the old body toggle.
    // Header/footer changes are tested separately, including partial repaints.
    const auto button=uieng::chrome(w,h).bridge;
    const auto hero=uieng::scan(w,h).hero;
    const uieng::Rect former{hero.x+4,hero.y+48,w>=400?76:68,44};
    for(int y=44;y<h-44;++y)for(int x=0;x<w;++x){
        auto px=(x>=former.x && x<former.x+former.w && y>=former.y && y<former.y+former.h)?pal::VOID:old[y*w+x];
        hash^=px;hash*=1099511628211ull;
    }
    assert(hash==expected);
    auto toggled=sc;toggled.bridgeOn=true;
    const auto on=draw(w,h,toggled);auto partialToggle=old;int changed=0;
    for(int y=button.y;y<button.y+button.h;y+=16){
        scanr::S strip{partialToggle.data()+y*w,w,y,std::min(16,button.y+button.h-y)};
        scanr::composeStrip(strip,w,h,toggled);
    }
    for(size_t i=0;i<on.size();++i)changed+=on[i]!=old[i];
    assert(changed>0 && partialToggle==on);
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
static void stagedPresentation(int w,int h,bool menu) {
    std::vector<uint16_t> front(w*h),back(w*h),panel(w*h),source(w*h,0x1234);
    framepresent::Frame f;f.begin(front.data(),back.data(),w,h);
    int sends=0;int sentPixels=0;
    auto sink=[&](uieng::Rect r,const uint16_t* px,int stride){
        assert(r.x>=0 && r.y>=0 && r.x+r.w<=w && r.y+r.h<=h);
        ++sends;sentPixels+=r.w*r.h;
        for(int y=r.y;y<r.y+r.h;++y)std::copy_n(px+y*stride+r.x,r.w,panel.data()+y*w+r.x);
    };
    for(int iteration=0;iteration<100;++iteration) {
        if(iteration==50)f.invalidate();
        if(iteration) {
            for(int j=0;j<11;++j) {
                int x=(iteration*17+j*43)%w,y=(iteration*29+j*61)%h;
                source[y*w+x]^=0x7fff;
            }
        }
        for(int y=0;y<h;y+=16)f.stage(y,source.data()+y*w,std::min(16,h-y));
        f.present(menu,sink);
        assert(panel==source && front==source);
    }
    sends=sentPixels=0;
    for(int y=0;y<h;y+=16)f.stage(y,source.data()+y*w,std::min(16,h-y));
    f.present(menu,sink);assert(sends==0);
    source[100*w+100]^=1;
    f.stage(96,source.data()+96*w,16);f.present(menu,sink,96,112);
    assert(panel==source && sends==1 && sentPixels==1);
}

int main() {
    geometry();
    auto hits=uieng::scanHits(480,320,{});const int count=hits.count;
    scanpres::recordingHits(hits,scanr::Rec::On);assert(hits.count==count);
    scanpres::recordingHits(hits,scanr::Rec::Saving);assert(hits.count==count-1);
    for(int i=0;i<hits.count;++i)assert(hits.items[i].action!=uieng::Action::ToggleLogging);
    for(bool menu:{false,true}){stagedPresentation(480,320,menu);stagedPresentation(320,240,menu);}
    // Body goldens: wide unchanged; compact native 55px artwork, 8px spectrum, subdued Cvlt seal.
    const uint64_t golden[2][3]={{1146081116921973907ull,13540596851408026256ull,17758537092499423455ull},{9848779580087129569ull,608851863895226392ull,11942760010647885326ull}};
    for(int theme=0;theme<3;++theme){profile(480,320,theme,golden[0][theme]);profile(320,240,theme,golden[1][theme]);}
    scanpres::PresentMetrics m;m.work(100,20,30);assert(m.composeUs==100 && m.diffUs==20 && m.transferUs==30);
    assert(m.renderUs==150 && m.maxSliceUs==150 && m.slices==1);m.entryMaxMs=5;m.clear();
    assert(!m.diffUs && !m.entryMaxMs && !m.renderUs);
    puts("scan latency: exact static pixels, 72 sweep phases, narrow repaint and full-work metrics passed");
}
