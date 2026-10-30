// Host test for ui/echoes_present.h: EchoStore snapshot (owned strings),
// status/body scene diff, and the entry->commit->service sequencer.
// Build:
//   g++ -std=c++17 -I. -Isim sim/test_echoes_engine.cpp ui/palette.cpp \
//       ui/themepack.cpp ui/muffinseal.cpp ui/motif.cpp ui/canvas.cpp ui/font5x7.cpp -o /tmp/tee1 -lm
#include "../ui/echoes_present.h"
#include "echoes_fixture.h"
#include <cstdio>
#include <cstring>
#include <vector>
using namespace echpres;

static int fails=0, checks=0;
static void expect(bool c,const char* m){ ++checks; if(!c){++fails;printf("  FAIL %s\n",m);} else printf("  ok   %s\n",m); }

static void fill(EchoStore& st, const echr::EchoScene& s){
    st.begin(s.theme, s.now); st.gps(s.gps); st.status(s.nodes,s.hasCell,s.battPct,s.rec); st.totals(s.total,s.clients);
    for (int i=0;i<s.nRows;i++) st.addRow(s.rows[i].seeking,s.rows[i].who,s.rows[i].randomised,s.rows[i].agoS);
}
struct FbSink : echr::Sink {
    int W,H; std::vector<int> cnt; bool oob=false;
    FbSink(int w,int h):W(w),H(h),cnt(w*h,0){}
    void strip(int x,int y,int w,int h,const uint16_t*) override {
        for(int ry=0;ry<h;ry++)for(int rx=0;rx<w;rx++){int gx=x+rx,gy=y+ry;
            if(gx<0||gx>=W||gy<0||gy>=H){oob=true;continue;} cnt[gy*W+gx]++;} }
};

int main(){
    EchoStore st; fill(st, echfix::demo(2));
    const echr::EchoScene& o = st.scene();
    expect(o.nRows==3 && o.clients==4 && !strcmp(o.rows[1].seeking,"home-network") && o.rows[1].randomised, "store: rows copied verbatim");
    EchoStore cap; cap.begin(0,0); int n=0;
    for(int i=0;i<EchoStore::MAXROWS+5;i++) if(cap.addRow("x","Y",false,0)) n++;
    expect(n==EchoStore::MAXROWS, "store: row cap enforced");

    const echr::EchoScene a = echfix::demo(2), base = echfix::demo(2);
    bool s,b;
    sceneDirty(a, base, s, b); expect(!s && !b, "diff: identical -> nothing dirty");
    echr::EchoScene g = echfix::demo(2); g.gps={true,3,12,30000};
    sceneDirty(a, g, s, b); expect(s && !b, "diff: GPS -> status only");
    echr::EchoScene cl = echfix::demo(2); cl.clients = 9;
    sceneDirty(a, cl, s, b); expect(!s && b, "diff: client count -> body only");

    const int W=480,H=320;
    EchoEngine e; e.reset(W,H); echr::Compositor comp; FbSink fb(W,H);
    e.enter(); fill(e.back(), echfix::demo(2)); e.commit();
    expect(e.pending() && !e.acceptsTouch(), "engine: entry queues full repaint, gates touch");
    bool bounded=true,once=true,done=false; std::vector<int> cover(W*H,0);
    while (e.pending()){ StripScheduler::Slice sl=e.nextSlice(4);
        if(sl.y1-sl.y0>64) bounded=false;
        if(sl.y1>sl.y0) comp.renderRows(W,H,e.scene(),sl.y0,sl.y1,fb);
        for(int y=sl.y0;y<sl.y1;y++) for(int x=0;x<W;x++) cover[y*W+x]++; done=sl.done; }
    for(int i=0;i<W*H;i++) if(cover[i]!=1){once=false;break;}
    expect(bounded && done && once && !fb.oob, "engine: entry paints every pixel once in bounded slices");
    expect(e.acceptsTouch(), "engine: touch accepted after entry");
    fill(e.back(), echfix::demo(2)); e.commit();
    expect(!e.pending(), "engine: unchanged scene queues no repaint");

    printf("\n%s  %d/%d checks passed\n", fails?"FAILED":"PASSED", checks-fails, checks);
    return fails?1:0;
}
