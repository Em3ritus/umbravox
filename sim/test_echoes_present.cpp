// Host test for the ECHOES layout + hit set (read-only log; no oracle). Asserts
// the title/header/list split both profiles and the chrome-only hit set.
// Build:
//   g++ -std=c++17 -I. -Isim sim/test_echoes_present.cpp ui/palette.cpp \
//       ui/themepack.cpp ui/muffinseal.cpp ui/motif.cpp ui/canvas.cpp ui/font5x7.cpp -o /tmp/tep1 -lm
#include "../ui/echoes_present.h"
#include <cstdio>
#include <cstring>
using namespace uieng;

static int fails=0, checks=0;
static void expect(bool c,const char* m){ ++checks; if(!c){++fails;printf("  FAIL %s\n",m);} else printf("  ok   %s\n",m); }
static bool overlap(Rect a, Rect b){ return a.x<b.x+b.w && b.x<a.x+a.w && a.y<b.y+b.h && b.y<a.y+a.h; }

int main(){
    for (int P=0;P<2;++P){
        const int W=P?320:480, H=P?240:320;
        const LogLayout g = echoes(W,H);
        expect(g.body==Rect{8,52,W-16,H-104}, P?"320 body":"480 body");
        expect(g.title.y==g.body.y && g.header.y==g.body.y+20 && g.list.y==g.body.y+32, "title/header/list stack");
        expect(g.list.y+g.list.h==g.body.y+g.body.h, "list fills to body bottom");
        HitList h = echoesHits(W,H);
        expect(h.count==(W>=400?9:8), "echoes hits: chrome nav only (log is scroll, not taps)");
        bool ok=true; int nav=0;
        for(int j=0;j<h.count;j++){ const Rect r=h.items[j].rect;
            if(r.x<0||r.y<0||r.x+r.w>W||r.y+r.h>H||r.w<44||r.h<44) ok=false;
            if(h.items[j].action==Action::Nav) nav++;
            for(int k=0;k<j;k++) if(overlap(r,h.items[k].rect)) ok=false; }
        expect(ok, "echoes chrome hits: in-bounds, >=44px, non-overlapping");
        expect(nav==4, "echoes: four nav-bar destinations bound");
    }
    printf("\n%s  %d/%d checks passed\n", fails?"FAILED":"PASSED", checks-fails, checks);
    return fails?1:0;
}
