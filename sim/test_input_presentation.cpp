#include "../ui/input_events.h"
#include "../device/i2c_budget.h"
#include "../ui/pending_hit.h"
#include "../ui/frame_present.h"
#include "../ui/menu_present.h"
#include "../ui/scan_render.h"
#include <cassert>
#include <cstdio>
#include <vector>
#include <algorithm>
using namespace inputevents;
static void input() {
    PressQueue q; Press p;
    q.sample(true, 30, 60, q.epoch()); q.sample(false, -1, -1, q.epoch());
    assert(q.pop(p) && p.x==30 && p.y==60); assert(!q.pop(p));
    q.sample(true, 31, 61, q.epoch()); q.navigate();
    assert(!q.pop(p)); q.sample(true, 31, 61, q.epoch()); assert(!q.pop(p));
    q.sample(false, -1, -1, q.epoch()); q.sample(true, 32, 62, q.epoch()); assert(q.pop(p));
    q.sample(false, -1, -1, q.epoch()); auto old=q.epoch(); q.navigate();
    q.sample(true, 1, 1, old); assert(!q.pop(p)); // sample crossing navigation
    q.sample(false, -1, -1, q.epoch());
    for(int i=0;i<9;++i) {q.sample(true,i,60,q.epoch());q.sample(false,-1,-1,q.epoch());}
    assert(q.dropped()==1); for(int i=0;i<8;++i){assert(q.pop(p));assert(p.x==i);} assert(!q.pop(p));
    PendingHit pending; auto hits=uieng::menuHits(480,320); uieng::Hit hit{};
    const auto cell=uieng::menu(480,320).cells[1];
    assert(pending.observe(hits,cell.x+4,cell.y+4,2));
    assert(!pending.take(false,2,hits,hit)); // release needs no action; event persists
    assert(pending.take(true,2,hits,hit)); assert(hit.payload==(int)uieng::ScreenId::Mesh);
    assert(!pending.take(true,2,hits,hit));
    pending.observe(hits,cell.x+4,cell.y+4,2); assert(!pending.take(true,3,hits,hit));
    uieng::HitList rows; char key[]="original";
    rows.add("row",{0,0,44,44},uieng::Action::OpenDeviceDetail,0,key,"row");
    pending.observe(rows,1,1,4); key[0]='X'; // borrowed key changed; owned event must NOT change
    assert(!pending.take(true,4,rows,hit));
    assert(i2cbudget::remaining(100,100,40)==40);
    assert(i2cbudget::remaining(100,125,40)==15);
    assert(i2cbudget::remaining(100,140,40)==0);
    assert(i2cbudget::remaining(100,141,40)==0);
    assert(i2cbudget::remaining(0xfffffff0u,4,40)==20);
    Deadline d; assert(d.complete(100)==20);assert(d.complete(123)==17);
    assert(d.missed()==0); assert(d.complete(181)==20);assert(d.missed()==1);
    Deadline wrap; assert(wrap.complete(0xfffffff0u)==20);assert(wrap.complete(7)==17);
}
static void render(int w,int h) {
    std::vector<uint16_t> front(w*h),back(w*h),panel(w*h,0x1234);
    framepresent::Frame f; f.begin(front.data(),back.data(),w,h);
    pal::setTheme(pal::TH_UMBRAVOX);
    int calls=0, pixels=0; bool touchedChrome=false;
    auto sink=[&](uieng::Rect r,const uint16_t* p,int stride) {
        ++calls; pixels+=r.w*r.h; if(r.y<44 || r.y+r.h>h-44)touchedChrome=true;
        for(int y=r.y;y<r.y+r.h;++y)std::copy_n(p+y*stride+r.x,r.w,panel.data()+y*w+r.x);
    };
    menr::MenuScene sc;
    for(int y=0;y<h;y+=16) {scanr::S s{f.rows(y),w,y,std::min(16,h-y)};menr::composeStrip(s,w,h,sc);}
    assert(std::all_of(panel.begin(),panel.end(),[](uint16_t v){return v==0x1234;}));
    f.present(true,sink);assert(panel==back);assert(front==back);
    calls=pixels=0;f.present(true,sink);assert(calls==0);
    // Update a complete card off-screen; unchanged chrome is never transferred.
    const auto cell=uieng::menu(w,h).cells[4];
    for(int y=cell.y;y<cell.y+cell.h;++y)for(int x=cell.x;x<cell.x+cell.w;++x)back[y*w+x]^=0xffff;
    const auto before=panel; touchedChrome=false; f.present(true,sink);
    assert(calls==1 && !touchedChrome);assert(panel==back);assert(front==back);assert(panel!=before);
    // Real full-screen replacement: source is complete before any visible write.
    scanr::ScanScene scan;
    for(int y=0;y<h;y+=16) {scanr::S s{f.rows(y),w,y,std::min(16,h-y)};scanr::composeStrip(s,w,h,scan);}
    assert(panel==front); f.present(false,sink);assert(panel==back);
    f.invalidate();calls=0; f.present(false,sink);assert(calls==3 && panel==back);
}
int main(){input();render(480,320);render(320,240);puts("input/presentation: all assertions passed (both sizes)");}
