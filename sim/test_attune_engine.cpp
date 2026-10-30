// Host test for ui/attune_present.h: AttuneStore snapshot (owned label/value
// strings), status/body scene diff, and the entry->commit->service sequencer.
// Build:
//   g++ -std=c++17 -I. -Isim sim/test_attune_engine.cpp ui/palette.cpp \
//       ui/themepack.cpp ui/muffinseal.cpp ui/motif.cpp ui/canvas.cpp ui/font5x7.cpp -o /tmp/tae1 -lm
#include "../ui/attune_present.h"
#include "attune_fixture.h"
#include <cstdio>
#include <cstring>
#include <vector>
using namespace atnpres;

static int fails=0, checks=0;
static void expect(bool c,const char* m){ ++checks; if(!c){++fails;printf("  FAIL %s\n",m);} else printf("  ok   %s\n",m); }

static void fill(AttuneStore& st, const atnr::AttuneScene& s){
    st.begin(s.theme, s.now); st.gps(s.gps); st.status(s.nodes,s.hasCell,s.battPct,s.rec);
    st.paging(s.page, s.nPages);
    for (int i=0;i<s.nRows;i++) st.addRow(s.rows[i].label, s.rows[i].value);
}
struct FbSink : atnr::Sink {
    int W,H; std::vector<int> cnt; bool oob=false;
    FbSink(int w,int h):W(w),H(h),cnt(w*h,0){}
    void strip(int x,int y,int w,int h,const uint16_t*) override {
        for(int ry=0;ry<h;ry++)for(int rx=0;rx<w;rx++){int gx=x+rx,gy=y+ry;
            if(gx<0||gx>=W||gy<0||gy>=H){oob=true;continue;} cnt[gy*W+gx]++;} }
};

int main(){
    const int W=480,H=320;
    AttuneStore st; fill(st, atnfix::demo(2, W, H, 0));
    const atnr::AttuneScene& o = st.scene();
    expect(o.nRows==3 && !strcmp(o.rows[0].label,"ALERT") && !strcmp(o.rows[0].value,"MED"), "store: knob rows copied verbatim");
    AttuneStore cap; cap.begin(0,0); int n=0;
    for(int i=0;i<AttuneStore::MAXROWS+5;i++) if(cap.addRow("L","V")) n++;
    expect(n==AttuneStore::MAXROWS, "store: row cap enforced");

    const atnr::AttuneScene a = atnfix::demo(2, W, H, 0), base = atnfix::demo(2, W, H, 0);
    bool s,b;
    sceneDirty(a, base, s, b); expect(!s && !b, "diff: identical -> nothing dirty");
    atnr::AttuneScene g = atnfix::demo(2, W, H, 0); g.gps={true,3,12,30000};
    sceneDirty(a, g, s, b); expect(s && !b, "diff: GPS -> status only");
    atnr::AttuneScene pg = atnfix::demo(2, W, H, 1);
    sceneDirty(a, pg, s, b); expect(!s && b, "diff: a page turn -> body only");
    static atnr::AttuneRow R2[4]; for(int i=0;i<a.nRows;i++) R2[i]=a.rows[i]; R2[0].value="HIGH";
    atnr::AttuneScene vc = atnfix::demo(2, W, H, 0); vc.rows=R2;
    sceneDirty(a, vc, s, b); expect(!s && b, "diff: a knob value change -> body only");

    AttuneEngine e; e.reset(W,H); atnr::Compositor comp; FbSink fb(W,H);
    e.enter(); fill(e.back(), atnfix::demo(2, W, H, 0)); e.commit();
    expect(e.pending() && !e.acceptsTouch(), "engine: entry queues full repaint, gates touch");
    bool bounded=true,once=true,done=false; std::vector<int> cover(W*H,0);
    while (e.pending()){ StripScheduler::Slice sl=e.nextSlice(4);
        if(sl.y1-sl.y0>64) bounded=false;
        if(sl.y1>sl.y0) comp.renderRows(W,H,e.scene(),sl.y0,sl.y1,fb);
        for(int y=sl.y0;y<sl.y1;y++) for(int x=0;x<W;x++) cover[y*W+x]++; done=sl.done; }
    for(int i=0;i<W*H;i++) if(cover[i]!=1){once=false;break;}
    expect(bounded && done && once && !fb.oob, "engine: entry paints every pixel once in bounded slices");
    expect(e.acceptsTouch(), "engine: touch accepted after entry");
    fill(e.back(), atnfix::demo(2, W, H, 0)); e.commit();
    expect(!e.pending(), "engine: unchanged scene queues no repaint");

    printf("\n%s  %d/%d checks passed\n", fails?"FAILED":"PASSED", checks-fails, checks);
    return fails?1:0;
}
