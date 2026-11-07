// Host test for ui/detail_present.h: DetailStore snapshot (owned strings +
// RSSI history), status/body scene diff, and the entry->commit->service
// sequencer. Also the graceful deselect: a device that goes invalid is a body
// change (the "GONE" state), not a crash.
// Build:
//   g++ -std=c++17 -I. -Isim sim/test_detail_engine.cpp ui/palette.cpp \
//       ui/themepack.cpp ui/muffinseal.cpp ui/motif.cpp ui/canvas.cpp ui/font5x7.cpp -o /tmp/tdte1 -lm
#include "../ui/detail_present.h"
#include "detail_fixture.h"
#include <cstdio>
#include <cstring>
#include <vector>
using namespace detpres;

static int fails=0, checks=0;
static void expect(bool c,const char* m){ ++checks; if(!c){++fails;printf("  FAIL %s\n",m);} else printf("  ok   %s\n",m); }

static void fill(DetailStore& st, const detr::DetailScene& s){
    st.begin(s.theme, s.now); st.gps(s.gps); st.status(s.nodes,s.hasCell,s.battPct,s.rec);
    st.device(s.valid, s.name, s.mac, s.bandChan, s.band, s.rssi, s.auth, s.authOpen,
              s.threat, s.conf, s.why, s.firstSeen, s.lastSeen);
    if (s.hist && s.histN) st.history(s.hist, s.histN);
}
struct FbSink : detr::Sink {
    int W,H; std::vector<int> cnt; bool oob=false;
    FbSink(int w,int h):W(w),H(h),cnt(w*h,0){}
    void strip(int x,int y,int w,int h,const uint16_t*) override {
        for(int ry=0;ry<h;ry++)for(int rx=0;rx<w;rx++){int gx=x+rx,gy=y+ry;
            if(gx<0||gx>=W||gy<0||gy>=H){oob=true;continue;} cnt[gy*W+gx]++;} }
};

int main(){
    DetailStore st; fill(st, detfix::demo(2));
    const detr::DetailScene& o = st.scene();
    expect(o.valid && !strcmp(o.name,"FLOCK-CAM") && !strcmp(o.mac,"AA:BB:CC:DD:EE:FF"), "store: identity copied verbatim");
    expect(o.histN==12 && o.hist[0]==-88 && o.hist[11]==-58, "store: RSSI history copied (oldest..newest)");
    expect(!strcmp(o.threat,"FLOCK") && o.conf==2, "store: signature verdict carried");
    DetailStore cap; cap.begin(0,0); int8_t big[40]; for(int i=0;i<40;i++) big[i]=-50;
    cap.history(big, 40);
    expect(cap.scene().histN==DetailStore::HN, "store: history clamped to HIST_N");

    const detr::DetailScene a = detfix::demo(2), base = detfix::demo(2);
    bool s,b;
    sceneDirty(a, base, s, b); expect(!s && !b, "diff: identical -> nothing dirty");
    detr::DetailScene g = detfix::demo(2); g.gps={true,3,12,30000};
    sceneDirty(a, g, s, b); expect(s && !b, "diff: GPS -> status only");
    detr::DetailScene gone = detfix::demo(2, false);
    sceneDirty(a, gone, s, b); expect(!s && b, "diff: device deselected (valid->false) -> body only (graceful)");
    static int8_t H2[13]; for(int i=0;i<12;i++) H2[i]=detfix::HIST[i]; H2[12]=-55;
    detr::DetailScene nw = detfix::demo(2); nw.rssi=-55; nw.hist=H2; nw.histN=13;
    sceneDirty(a, nw, s, b); expect(!s && b, "diff: a new sighting -> body only");

    const int W=480,H=320;
    DetailEngine e; e.reset(W,H); detr::Compositor comp; FbSink fb(W,H);
    e.enter(); fill(e.back(), detfix::demo(2)); e.commit();
    expect(e.pending() && !e.acceptsTouch(), "engine: entry queues full repaint, gates touch");
    bool bounded=true,once=true,done=false; std::vector<int> cover(W*H,0);
    while (e.pending()){ StripScheduler::Slice sl=e.nextSlice(4);
        if(sl.y1-sl.y0>64) bounded=false;
        if(sl.y1>sl.y0) comp.renderRows(W,H,e.scene(),sl.y0,sl.y1,fb);
        for(int y=sl.y0;y<sl.y1;y++) for(int x=0;x<W;x++) cover[y*W+x]++; done=sl.done; }
    for(int i=0;i<W*H;i++) if(cover[i]!=1){once=false;break;}
    expect(bounded && done && once && !fb.oob, "engine: entry paints every pixel once in bounded slices");
    expect(e.acceptsTouch(), "engine: touch accepted after entry");
    fill(e.back(), detfix::demo(2)); e.commit();
    expect(!e.pending(), "engine: unchanged scene queues no repaint");

    printf("\n%s  %d/%d checks passed\n", fails?"FAILED":"PASSED", checks-fails, checks);
    return fails?1:0;
}
