// Host test for ui/mesh_present.h: the MeshStore snapshot (owned nodes), the
// status/body scene diff, and the entry->commit->service sequencer on the
// shared scanpres machinery.
// Build:
//   g++ -std=c++17 -I. -Isim sim/test_mesh_engine.cpp ui/palette.cpp \
//       ui/themepack.cpp ui/muffinseal.cpp ui/motif.cpp ui/canvas.cpp ui/font5x7.cpp -o /tmp/tme1 -lm
#include "../ui/mesh_present.h"
#include "mesh_fixture.h"
#include <cstdio>
#include <vector>
using namespace mshpres;

static int fails=0, checks=0;
static void expect(bool c,const char* m){ ++checks; if(!c){++fails;printf("  FAIL %s\n",m);} else printf("  ok   %s\n",m); }

static void fill(MeshStore& st, const mshr::MeshScene& s){
    st.begin(s.theme, s.now); st.gps(s.gps);
    st.status(s.nodes, s.hasCell, s.battPct, s.rec);
    st.header(s.meshCoords, s.coreCamping, s.coreChan, s.meshLossPct, s.netId, s.enrollSecs);
    for (int i=0;i<s.nRows;i++) st.addNode(s.rows[i].id, s.rows[i].ageS, s.rows[i].lossPct, s.rows[i].live);
}
struct FbSink : mshr::Sink {
    int W,H; std::vector<int> cnt; bool oob=false;
    FbSink(int w,int h):W(w),H(h),cnt(w*h,0){}
    void strip(int x,int y,int w,int h,const uint16_t*) override {
        for(int ry=0;ry<h;ry++)for(int rx=0;rx<w;rx++){int gx=x+rx,gy=y+ry;
            if(gx<0||gx>=W||gy<0||gy>=H){oob=true;continue;} cnt[gy*W+gx]++;} }
};

int main(){
    MeshStore st; fill(st, mshfix::demo(2));
    const mshr::MeshScene& o = st.scene();
    expect(o.nRows==4 && o.rows[2].lossPct==33 && !o.rows[2].live, "store: nodes copied verbatim");
    expect(o.netId==2 && o.coreChan==6 && o.meshLossPct==7, "store: header carried");
    MeshStore cap; cap.begin(0,0); int n=0; uint8_t id[3]={1,2,3};
    for(int i=0;i<MeshStore::MAXROWS+5;i++) if(cap.addNode(id,0,0,true)) n++;
    expect(n==MeshStore::MAXROWS, "store: node cap enforced");

    const mshr::MeshScene a = mshfix::demo(2), base = mshfix::demo(2);
    bool s,b;
    sceneDirty(a, base, s, b); expect(!s && !b, "diff: identical -> nothing dirty");
    mshr::MeshScene g = mshfix::demo(2); g.gps={true,3,12,30000};
    sceneDirty(a, g, s, b); expect(s && !b, "diff: GPS -> status only");
    mshr::MeshScene e = mshfix::demo(2); e.enrollSecs = 30;
    sceneDirty(a, e, s, b); expect(!s && b, "diff: pairing window -> body only");
    static mshr::MeshNode N2[4]; for(int i=0;i<4;i++) N2[i]=mshfix::NODES[i]; N2[0].ageS+=5;
    mshr::MeshScene nd = mshfix::demo(2); nd.rows=N2;
    sceneDirty(a, nd, s, b); expect(!s && b, "diff: a node age change -> body only");

    const int W=480,H=320;
    MeshEngine eng; eng.reset(W,H); mshr::Compositor comp; FbSink fb(W,H);
    eng.enter(); fill(eng.back(), mshfix::demo(2)); eng.commit();
    expect(eng.pending() && !eng.acceptsTouch(), "engine: entry queues full repaint, gates touch");
    bool bounded=true, gated=true, done=false; std::vector<int> cover(W*H,0);
    while (eng.pending()){ StripScheduler::Slice sl=eng.nextSlice(4);
        if(sl.y1-sl.y0>64) bounded=false;
        if(sl.y1>sl.y0) comp.renderRows(W,H,eng.scene(),sl.y0,sl.y1,fb);
        for(int y=sl.y0;y<sl.y1;y++) for(int x=0;x<W;x++) cover[y*W+x]++;
        if(!sl.done && eng.acceptsTouch()) gated=false; done=sl.done; }
    bool once=true; for(int i=0;i<W*H;i++) if(cover[i]!=1){once=false;break;}
    expect(bounded && done && once && !fb.oob, "engine: entry paints every pixel once in bounded slices");
    expect(gated && eng.acceptsTouch(), "engine: touch gated through entry, accepted after");
    fill(eng.back(), mshfix::demo(2)); eng.commit();
    expect(!eng.pending(), "engine: unchanged scene queues no repaint");

    printf("\n%s  %d/%d checks passed\n", fails?"FAILED":"PASSED", checks-fails, checks);
    return fails?1:0;
}
