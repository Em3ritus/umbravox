// Host test for ui/menu_present.h -- the MENU (hub) adapter's pure core: the
// snapshot store, the status/body scene diff (body dirty only on a label-mode
// flip), and the entry->commit->service sequencer on the SHARED scanpres
// machinery.
// Build:
//   g++ -std=c++17 -I. -Isim sim/test_menu_engine.cpp ui/palette.cpp \
//       ui/themepack.cpp ui/muffinseal.cpp ui/motif.cpp ui/canvas.cpp ui/font5x7.cpp -o /tmp/tme_ -lm
#include "../ui/menu_present.h"
#include "menu_fixture.h"
#include <cstdio>
#include <vector>
using namespace menpres;

static int fails=0, checks=0;
static void expect(bool c,const char* m){ ++checks; if(!c){++fails;printf("  FAIL %s\n",m);} else printf("  ok   %s\n",m); }

static void fill(MenuStore& st, const menr::MenuScene& s){
    st.begin(s.theme, s.now);
    st.gps(s.gps);
    st.status(s.nodes, s.hasCell, s.battPct, s.rec);
    st.labels(s.plain);
}

struct FbSink : menr::Sink {
    int W,H; std::vector<int> cnt; bool oob=false;
    FbSink(int w,int h):W(w),H(h),cnt(w*h,0){}
    void strip(int x,int y,int w,int h,const uint16_t*) override {
        for(int ry=0;ry<h;ry++)for(int rx=0;rx<w;rx++){int gx=x+rx,gy=y+ry;
            if(gx<0||gx>=W||gy<0||gy>=H){oob=true;continue;} cnt[gy*W+gx]++;} }
};

static void testStoreDirty(){
    MenuStore st; fill(st, menfix::demo(2, /*plain*/true));
    const menr::MenuScene& o = st.scene();
    expect(o.theme==2 && o.now==5000 && o.plain, "store: scalar fields + label mode carried");

    const menr::MenuScene a = menfix::demo(2,false), base = menfix::demo(2,false);
    bool s,b;
    sceneDirty(a, base, s, b); expect(!s && !b, "diff: identical scenes -> nothing dirty");
    menr::MenuScene g = menfix::demo(2,false); g.gps={true,3,12,30000};
    sceneDirty(a, g, s, b); expect(s && !b, "diff: GPS change -> status only");
    menr::MenuScene p = menfix::demo(2,true);
    sceneDirty(a, p, s, b); expect(!s && b, "diff: label-mode flip -> body only");
}

static void testEngine(){
    const int W=480,H=320;
    MenuEngine e; e.reset(W,H);
    menr::Compositor comp; FbSink fb(W,H);

    e.enter();
    expect(!e.acceptsTouch(), "engine: entry gates touch before the first paint");
    fill(e.back(), menfix::demo(2,false)); e.commit();
    expect(e.pending(), "engine: entry queued a (full) repaint");

    bool bounded=true, gated=true, done=false; std::vector<int> cover(W*H,0);
    while (e.pending()){ StripScheduler::Slice sl=e.nextSlice(4);
        if(sl.y1-sl.y0>64) bounded=false;
        if(sl.y1>sl.y0) comp.renderRows(W,H,e.scene(),sl.y0,sl.y1,fb);
        for(int y=sl.y0;y<sl.y1;y++) for(int x=0;x<W;x++) cover[y*W+x]++;
        if(!sl.done && e.acceptsTouch()) gated=false; done=sl.done; }
    bool once=true; for(int i=0;i<W*H;i++) if(cover[i]!=1){once=false;break;}
    expect(bounded && done, "engine: full entry repaint in bounded slices");
    expect(once && !fb.oob, "engine: entry frame paints every pixel exactly once");
    expect(gated, "engine: touch gated for the whole entry paint");
    expect(e.acceptsTouch(), "engine: touch accepted after the entry paint");

    // label flip repaints a bounded body band (status top untouched, not full)
    fill(e.back(), menfix::demo(2,true)); e.commit();
    expect(e.pending(), "engine: a label flip queues a repaint");
    const uieng::MenuLayout L = uieng::menu(W,H);
    const int statusBottom = L.statusCells[0].y + L.statusCells[0].h;
    int minY=H, maxY=0; while (e.pending()){ StripScheduler::Slice sl=e.nextSlice(100);
        if(sl.y1>sl.y0){ if(sl.y0<minY)minY=sl.y0; if(sl.y1>maxY)maxY=sl.y1; } }
    expect(minY>=statusBottom-16 && maxY>statusBottom && (maxY-minY)<H,
           "engine: label flip repaints a bounded body band, not full-screen");

    // unchanged scene queues nothing
    fill(e.back(), menfix::demo(2,true)); e.commit();
    expect(!e.pending(), "engine: an unchanged scene queues no repaint");
}

int main(){
    testStoreDirty();
    testEngine();
    printf("\n%s  %d/%d checks passed\n", fails?"FAILED":"PASSED", checks-fails, checks);
    return fails?1:0;
}
