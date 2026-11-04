// Host test for ui/codex_present.h -- the CODEX device adapter's pure core:
// the stable CodexStore snapshot (owned row key/label bytes), the status/body
// scene diff, and the entry->commit->service sequencer built on the SHARED
// scanpres scheduler/transition/metrics (no second copy).
// Build:
//   g++ -std=c++17 -I. -Isim sim/test_codex_engine.cpp ui/palette.cpp \
//       ui/themepack.cpp ui/muffinseal.cpp ui/motif.cpp ui/canvas.cpp ui/font5x7.cpp -o /tmp/tce -lm
#include "../ui/codex_present.h"
#include "codex_fixture.h"
#include <cstdio>
#include <cstring>
#include <vector>
using namespace codpres;

static int fails=0, checks=0;
static void expect(bool c,const char* m){ ++checks; if(!c){++fails;printf("  FAIL %s\n",m);} else printf("  ok   %s\n",m); }

static void fill(CodexStore& st, const codr::CodexScene& s){
    st.begin(s.theme, s.now);
    st.gps(s.gps);
    st.status(s.nodes, s.hasCell, s.battPct, s.rec);
    st.filter(s.filter);
    st.page(s.pageLo, s.pageHi, s.listTotal);
    for (int i=0;i<s.nRows;i++) st.addRow(s.rows[i].key,s.rows[i].label,s.rows[i].band,s.rows[i].rssi,s.rows[i].chan,s.rows[i].threat);
}

struct FbSink : codr::Sink {
    int W,H; std::vector<int> cnt; bool oob=false;
    FbSink(int w,int h):W(w),H(h),cnt(w*h,0){}
    void strip(int x,int y,int w,int h,const uint16_t*) override {
        for(int ry=0;ry<h;ry++)for(int rx=0;rx<w;rx++){int gx=x+rx,gy=y+ry;
            if(gx<0||gx>=W||gy<0||gy>=H){oob=true;continue;} cnt[gy*W+gx]++;} }
};

static void testStore(){
    CodexStore st;
    fill(st, codfix::demo(2, codr::F_WIFI));
    const codr::CodexScene& o = st.scene();
    expect(o.theme==2 && o.now==5000, "store: scalar fields carried");
    expect(o.filter==codr::F_WIFI && o.nRows==3, "store: filter + row count wired");
    expect(!strcmp(o.rows[0].key,"w:aabbccddeeff") && !strcmp(o.rows[0].label,"NEST-CAM") && o.rows[0].threat,
           "store: row key+label copied, flags carried");
    expect(o.pageLo==1 && o.pageHi==3 && o.listTotal==14, "store: page window carried");

    CodexStore c; c.begin(0,0);
    int added=0; for(int i=0;i<CodexStore::MAXROWS+5;i++) if(c.addRow("k","l",0,-50,1,false)) added++;
    expect(added==CodexStore::MAXROWS && c.rowCount()==CodexStore::MAXROWS, "store: row cap enforced");
    c.begin(0,0); expect(c.rowCount()==0, "store: begin() clears rows");
}

static void testDirty(){
    const codr::CodexScene a = codfix::demo(2,codr::F_WIFI), base = codfix::demo(2,codr::F_WIFI);
    bool st,bd;
    sceneDirty(a, base, st, bd); expect(!st && !bd, "diff: identical scenes -> nothing dirty");

    codr::CodexScene g = codfix::demo(2,codr::F_WIFI); g.gps={true,3,12,30000};
    sceneDirty(a, g, st, bd); expect(st && !bd, "diff: GPS change -> status only");

    codr::CodexScene f = codfix::demo(2,codr::F_BLE);
    sceneDirty(a, f, st, bd); expect(!st && bd, "diff: filter change -> body only");

    static codr::CodexRow R2[3]; for(int i=0;i<3;i++) R2[i]=codfix::ROWS[i]; R2[0].rssi-=5;
    codr::CodexScene r = codfix::demo(2,codr::F_WIFI); r.rows=R2;
    sceneDirty(a, r, st, bd); expect(!st && bd, "diff: a row's signal change -> body only");

    codr::CodexScene p = codfix::demo(2,codr::F_WIFI); p.pageLo=4; p.pageHi=6;
    sceneDirty(a, p, st, bd); expect(!st && bd, "diff: a page turn -> body only");
}

static void testEngine(){
    const int W=480,H=320;
    CodexEngine e; e.reset(W,H);
    codr::Compositor comp; FbSink fb(W,H);

    e.enter();
    expect(!e.acceptsTouch(), "engine: entry gates touch before the first paint");
    fill(e.back(), codfix::demo(2,codr::F_WIFI)); e.commit();
    expect(e.scene().nRows==3, "engine: served scene is the committed snapshot");
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
    expect(e.metrics().frames==1, "engine: one frame counted on completion");

    // a filter change repaints the body (below the status band)
    fill(e.back(), codfix::demo(2,codr::F_BLE)); e.commit();
    expect(e.pending(), "engine: a filter change queues a repaint");
    const uieng::CodexLayout L = uieng::codex(W,H);
    const int statusBottom = L.statusCells[0].y + L.statusCells[0].h;
    int minY=H, maxY=0; while (e.pending()){ StripScheduler::Slice sl=e.nextSlice(100);
        if(sl.y1>sl.y0){ if(sl.y0<minY)minY=sl.y0; if(sl.y1>maxY)maxY=sl.y1; } }
    // the body invalidation unions (statusBottom, navTop) and snaps to the 16px
    // strip grid, so it skips the status top and is never a full-screen repaint.
    expect(minY>=statusBottom-16 && maxY>statusBottom && (maxY-minY)<H,
           "engine: filter change repaints a bounded body band (status top untouched, not full-screen)");

    // unchanged scene queues nothing
    fill(e.back(), codfix::demo(2,codr::F_BLE)); e.commit();
    expect(!e.pending(), "engine: an unchanged scene queues no repaint");
}

int main(){
    testStore();
    testDirty();
    testEngine();
    printf("\n%s  %d/%d checks passed\n", fails?"FAILED":"PASSED", checks-fails, checks);
    return fails?1:0;
}
