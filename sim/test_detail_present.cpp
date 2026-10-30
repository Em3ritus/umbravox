// Host test for the DETAIL layout + hit set (no oracle; regions vs layout rules
// + structural invariants, both profiles). Verifies the back/header/stats/spark
// split, the >=44px BACK target, and that BACK binds Action::Back.
// Build:
//   g++ -std=c++17 -I. -Isim sim/test_detail_present.cpp ui/palette.cpp \
//       ui/themepack.cpp ui/muffinseal.cpp ui/motif.cpp ui/canvas.cpp ui/font5x7.cpp -o /tmp/tdt1 -lm
#include "../ui/detail_present.h"
#include <cstdio>
#include <cstring>
using namespace uieng;

static int fails=0, checks=0;
static void expect(bool c,const char* m){ ++checks; if(!c){++fails;printf("  FAIL %s\n",m);} else printf("  ok   %s\n",m); }
static bool overlap(Rect a, Rect b){ return a.x<b.x+b.w && b.x<a.x+a.w && a.y<b.y+b.h && b.y<a.y+a.h; }

int main(){
    for (int P=0;P<2;++P){
        const int W=P?320:480, H=P?240:320;
        const DetailLayout d = detail(W,H);
        expect(d.body==Rect{8,52,W-16,H-104}, P?"320 body":"480 body");
        expect(d.back==Rect{d.body.x,d.body.y,44,44}, "BACK is a 44x44 target, top-left");
        expect(d.header.x==d.body.x+48 && d.header.h==44 && !overlap(d.back,d.header), "header right of BACK, no overlap");
        expect(d.stats.y==d.body.y+44 && d.spark.y==d.body.y+d.body.h-d.spark.h && d.stats.h>=76, "stats then spark fill the rest");
        expect(d.spark.y+d.spark.h==d.body.y+d.body.h && !overlap(d.stats,d.spark), "spark at body bottom, clear of stats");

        HitList h = detailHits(W,H);
        expect(h.count==(W>=400?10:9)+1, "detail hits: 8 chrome + BACK");
        bool ok=true; const Hit* bk=nullptr;
        for(int j=0;j<h.count;j++){ const Rect r=h.items[j].rect;
            if(r.x<0||r.y<0||r.x+r.w>W||r.y+r.h>H||r.w<44||r.h<44) ok=false;
            if(!strcmp(h.items[j].slot,"back")) bk=&h.items[j];
            for(int k=0;k<j;k++){ if(!strcmp(h.items[j].slot,h.items[k].slot)) ok=false;
                                  if(overlap(r,h.items[k].rect)) ok=false; } }
        expect(ok, "detail hits: in-bounds, >=44px, distinct slots, non-overlapping");
        expect(bk && bk->action==Action::Back, "BACK -> Action::Back");
    }
    printf("\n%s  %d/%d checks passed\n", fails?"FAILED":"PASSED", checks-fails, checks);
    return fails?1:0;
}
