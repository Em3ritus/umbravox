// Host test for ui/omens_present.h: OmenStore snapshot (owned strings),
// status/body scene diff, and the entry->commit->service sequencer.
// Build:
//   g++ -std=c++17 -I. -Isim sim/test_omens_engine.cpp ui/palette.cpp \
//       ui/themepack.cpp ui/muffinseal.cpp ui/motif.cpp ui/canvas.cpp ui/font5x7.cpp -o /tmp/toe1 -lm
#include "../ui/omens_present.h"
#include "omens_fixture.h"
#include <cstdio>
#include <cstring>
#include <vector>
using namespace omnpres;

static int fails=0, checks=0;
static void expect(bool c,const char* m){ ++checks; if(!c){++fails;printf("  FAIL %s\n",m);} else printf("  ok   %s\n",m); }

static void fill(OmenStore& st, const omnr::OmenScene& s){
    st.begin(s.theme, s.now); st.gps(s.gps); st.status(s.nodes,s.hasCell,s.battPct,s.rec); st.total(s.total);
    for (int i=0;i<s.nRows;i++) st.addRow(s.rows[i].omen,s.rows[i].source,s.rows[i].conf,s.rows[i].agoS,s.rows[i].located);
}
struct FbSink : omnr::Sink {
    int W,H; std::vector<int> cnt; bool oob=false;
    FbSink(int w,int h):W(w),H(h),cnt(w*h,0){}
    void strip(int x,int y,int w,int h,const uint16_t*) override {
        for(int ry=0;ry<h;ry++)for(int rx=0;rx<w;rx++){int gx=x+rx,gy=y+ry;
            if(gx<0||gx>=W||gy<0||gy>=H){oob=true;continue;} cnt[gy*W+gx]++;} }
};

int main(){
    OmenStore st; fill(st, omnfix::demo(2));
    const omnr::OmenScene& o = st.scene();
    expect(o.nRows==3 && o.total==9 && !strcmp(o.rows[0].omen,"FLOCK-CAM") && o.rows[0].located, "store: rows copied verbatim");
    OmenStore cap; cap.begin(0,0); int n=0;
    for(int i=0;i<OmenStore::MAXROWS+5;i++) if(cap.addRow("x","y",0,0,false)) n++;
    expect(n==OmenStore::MAXROWS, "store: row cap enforced");

    const omnr::OmenScene a = omnfix::demo(2), base = omnfix::demo(2);
    bool s,b;
    sceneDirty(a, base, s, b); expect(!s && !b, "diff: identical -> nothing dirty");
    omnr::OmenScene g = omnfix::demo(2); g.gps={true,3,12,30000};
    sceneDirty(a, g, s, b); expect(s && !b, "diff: GPS -> status only");
    static omnr::OmenRow R2[3]; for(int i=0;i<3;i++) R2[i]=omnfix::ROWS[i]; R2[1].agoS+=10;
    omnr::OmenScene c = omnfix::demo(2); c.rows=R2;
    sceneDirty(a, c, s, b); expect(!s && b, "diff: a row age change -> body only");

    const int W=480,H=320;
    OmenEngine e; e.reset(W,H); omnr::Compositor comp; FbSink fb(W,H);
    e.enter(); fill(e.back(), omnfix::demo(2)); e.commit();
    expect(e.pending() && !e.acceptsTouch(), "engine: entry queues full repaint, gates touch");
    bool bounded=true,once=true,done=false; std::vector<int> cover(W*H,0);
    while (e.pending()){ StripScheduler::Slice sl=e.nextSlice(4);
        if(sl.y1-sl.y0>64) bounded=false;
        if(sl.y1>sl.y0) comp.renderRows(W,H,e.scene(),sl.y0,sl.y1,fb);
        for(int y=sl.y0;y<sl.y1;y++) for(int x=0;x<W;x++) cover[y*W+x]++; done=sl.done; }
    for(int i=0;i<W*H;i++) if(cover[i]!=1){once=false;break;}
    expect(bounded && done && once && !fb.oob, "engine: entry paints every pixel once in bounded slices");
    expect(e.acceptsTouch(), "engine: touch accepted after entry");
    fill(e.back(), omnfix::demo(2)); e.commit();
    expect(!e.pending(), "engine: unchanged scene queues no repaint");

    printf("\n%s  %d/%d checks passed\n", fails?"FAILED":"PASSED", checks-fails, checks);
    return fails?1:0;
}
