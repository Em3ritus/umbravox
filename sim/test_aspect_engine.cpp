// Host test for ui/aspect_present.h: AspectStore snapshot (owned name strings),
// status/body scene diff, and the entry->commit->service sequencer.
// Build:
//   g++ -std=c++17 -I. -Isim sim/test_aspect_engine.cpp ui/palette.cpp \
//       ui/themepack.cpp ui/muffinseal.cpp ui/motif.cpp ui/canvas.cpp ui/font5x7.cpp -o /tmp/tase1 -lm
#include "../ui/aspect_present.h"
#include "aspect_fixture.h"
#include <cstdio>
#include <cstring>
#include <vector>
using namespace asppres;

static int fails=0, checks=0;
static void expect(bool c,const char* m){ ++checks; if(!c){++fails;printf("  FAIL %s\n",m);} else printf("  ok   %s\n",m); }

static void fill(AspectStore& st, const aspr::AspectScene& s){
    st.begin(s.theme, s.now); st.gps(s.gps); st.status(s.nodes,s.hasCell,s.battPct,s.rec);
    st.labels(s.plain);
    st.appearance(s.themeName, s.sweepName, s.alertName, s.alertLive, s.packsSkipped);
}
struct FbSink : aspr::Sink {
    int W,H; std::vector<int> cnt; bool oob=false;
    FbSink(int w,int h):W(w),H(h),cnt(w*h,0){}
    void strip(int x,int y,int w,int h,const uint16_t*) override {
        for(int ry=0;ry<h;ry++)for(int rx=0;rx<w;rx++){int gx=x+rx,gy=y+ry;
            if(gx<0||gx>=W||gy<0||gy>=H){oob=true;continue;} cnt[gy*W+gx]++;} }
};

int main(){
    AspectStore st; fill(st, aspfix::demo(2));
    const aspr::AspectScene& o = st.scene();
    expect(!strcmp(o.themeName,"UMBRAVOX") && !strcmp(o.sweepName,"RADAR") && o.alertLive, "store: names copied, flags carried");

    const aspr::AspectScene a = aspfix::demo(2), base = aspfix::demo(2);
    bool s,b;
    sceneDirty(a, base, s, b); expect(!s && !b, "diff: identical -> nothing dirty");
    aspr::AspectScene g = aspfix::demo(2); g.gps={true,3,12,30000};
    sceneDirty(a, g, s, b); expect(s && !b, "diff: GPS -> status only");
    aspr::AspectScene sw = aspfix::demo(2); sw.sweepName = "PULSE";
    sceneDirty(a, sw, s, b); expect(!s && b, "diff: a sweep-name change -> body only");
    aspr::AspectScene sk = aspfix::demo(2); sk.packsSkipped = 3;
    sceneDirty(a, sk, s, b); expect(!s && b, "diff: skipped-pack count -> body only");

    auto labels=a;labels.plain=!a.plain;
    sceneDirty(a,labels,s,b);expect(!s&&b,"Labels mode change schedules body repaint");
    const int W=480,H=320;
    AspectEngine e; e.reset(W,H); aspr::Compositor comp; FbSink fb(W,H);
    e.enter(); fill(e.back(), aspfix::demo(2)); e.commit();
    expect(e.pending() && !e.acceptsTouch(), "engine: entry queues full repaint, gates touch");
    bool bounded=true,once=true,done=false; std::vector<int> cover(W*H,0);
    while (e.pending()){ StripScheduler::Slice sl=e.nextSlice(4);
        if(sl.y1-sl.y0>64) bounded=false;
        if(sl.y1>sl.y0) comp.renderRows(W,H,e.scene(),sl.y0,sl.y1,fb);
        for(int y=sl.y0;y<sl.y1;y++) for(int x=0;x<W;x++) cover[y*W+x]++; done=sl.done; }
    for(int i=0;i<W*H;i++) if(cover[i]!=1){once=false;break;}
    expect(bounded && done && once && !fb.oob, "engine: entry paints every pixel once in bounded slices");
    expect(e.acceptsTouch(), "engine: touch accepted after entry");
    // a theme switch forces a full repaint (palette + preview both move)
    e.themeChanged(); fill(e.back(), aspfix::demo(5)); e.commit();
    int rows=0; while(e.pending()){ StripScheduler::Slice sl=e.nextSlice(100); rows+=sl.y1-sl.y0; }
    expect(rows==H, "engine: themeChanged() forces a full repaint");

    printf("\n%s  %d/%d checks passed\n", fails?"FAILED":"PASSED", checks-fails, checks);
    return fails?1:0;
}
