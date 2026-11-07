// Host test for ui/domain_present.h -- the DOMAIN device adapter's pure core:
// the stable DomainStore snapshot (owned dots/track/rows + key/label bytes),
// the scene diff's status/body bands, and the entry->commit->service sequencer
// built on the SHARED scanpres scheduler/transition/metrics (no second copy).
// Build:
//   g++ -std=c++17 -I. -Isim sim/test_domain_engine.cpp ui/palette.cpp \
//       ui/themepack.cpp ui/muffinseal.cpp ui/motif.cpp ui/canvas.cpp ui/font5x7.cpp -o /tmp/tde -lm
#include "../ui/domain_present.h"
#include "domain_fixture.h"
#include <cstdio>
#include <cstring>
#include <vector>
using namespace dompres;

static int fails=0, checks=0;
static void expect(bool c,const char* m){ ++checks; if(!c){++fails;printf("  FAIL %s\n",m);} else printf("  ok   %s\n",m); }

// Copy a scene into a store (what the device snapshot does from live State).
static void fill(DomainStore& st, const domr::DomainScene& s){
    st.begin(s.theme, s.now);
    st.gps(s.gps);
    st.status(s.nodes, s.hasCell, s.battPct, s.rec);
    st.counts(s.plotShown, s.plotTotal, s.plotBeyond, s.rangeM);
    st.page(s.pageLo, s.pageHi, s.listTotal);
    for (int i=0;i<s.nDots;i++)  st.addDot(s.dots[i].dx, s.dots[i].dy, s.dots[i].band, s.dots[i].threat);
    for (int i=0;i<s.nTrack;i++) st.addTrack(s.track[i].dx, s.track[i].dy);
    for (int i=0;i<s.nRows;i++)  st.addRow(s.rows[i].key, s.rows[i].label, s.rows[i].band);
}

struct FbSink : domr::Sink {
    int W,H; std::vector<int> cnt; bool oob=false;
    FbSink(int w,int h):W(w),H(h),cnt(w*h,0){}
    void strip(int x,int y,int w,int h,const uint16_t*) override {
        for(int ry=0;ry<h;ry++)for(int rx=0;rx<w;rx++){int gx=x+rx,gy=y+ry;
            if(gx<0||gx>=W||gy<0||gy>=H){oob=true;continue;} cnt[gy*W+gx]++;}
    }
};

static void testStore(){
    DomainStore st;
    const domr::DomainScene s = domfix::demo(2, /*wide*/true);
    fill(st, s);
    const domr::DomainScene& o = st.scene();
    expect(o.theme==2 && o.now==5000, "store: scalar scene fields carried");
    expect(o.nDots==6 && o.nTrack==5 && o.nRows==2, "store: dot/track/row counts wired");
    expect(o.dots[3].threat && o.dots[3].band==0, "store: dot geometry carried verbatim");
    expect(!strcmp(o.rows[0].key,"w:aabbccddeeff") && !strcmp(o.rows[0].label,"FLOCK-CAM"),
           "store: row key+label copied into owned storage");
    expect(o.plotShown==6 && o.plotTotal==9 && o.plotBeyond==3 && o.rangeM==250, "store: counts/range carried");
    expect(o.pageLo==1 && o.pageHi==2 && o.listTotal==9, "store: page window carried");

    // caps + begin() clears
    DomainStore c; c.begin(0,0);
    int added=0; for (int i=0;i<DomainStore::MAXDOTS+10;i++) if (c.addDot(0,0,0,false)) added++;
    expect(added==DomainStore::MAXDOTS && c.dotCount()==DomainStore::MAXDOTS, "store: dot cap enforced");
    c.begin(0,0);
    expect(c.dotCount()==0 && c.rowCount()==0, "store: begin() clears dots and rows");
}

static void testDirty(){
    const domr::DomainScene a = domfix::demo(2,true), base = domfix::demo(2,true);
    bool st,bd;
    sceneDirty(a, base, st, bd); expect(!st && !bd, "diff: identical scenes -> nothing dirty");

    domr::DomainScene g = domfix::demo(2,true); g.gps = {true,3,12,30000};   // -> STALE
    sceneDirty(a, g, st, bd); expect(st && bd, "diff: GPS freshness changes status and map");

    domr::DomainScene r = domfix::demo(2,true); r.rangeM = 500;
    sceneDirty(a, r, st, bd); expect(!st && bd, "diff: range change -> body only");

    static domr::MapDot D2[6]; for(int i=0;i<6;i++) D2[i]=domfix::DOTS[i]; D2[0].dx+=5;
    domr::DomainScene d = domfix::demo(2,true); d.dots=D2;
    sceneDirty(a, d, st, bd); expect(!st && bd, "diff: a moved dot -> body only");

    static domr::DomRow R2[2]; for(int i=0;i<2;i++) R2[i]=domfix::ROWS[i]; R2[1].label="NEW-LABEL";
    domr::DomainScene l = domfix::demo(2,true); l.rows=R2;
    sceneDirty(a, l, st, bd); expect(!st && bd, "diff: a changed list row -> body only");

    domr::DomainScene pg = domfix::demo(2,true); pg.pageLo=3; pg.pageHi=4;
    sceneDirty(a, pg, st, bd); expect(!st && bd, "diff: a page turn -> body only");
}

static void testEngineIntegration(){
    const int W=480,H=320;
    DomainEngine e; e.reset(W,H);
    domr::Compositor comp; FbSink fb(W,H);

    // ENTRY: a snapshot is committed (which invalidates) BEFORE any slice is
    // served, so entry never queues a default/stale frame.
    e.enter();
    expect(!e.acceptsTouch(), "engine: entry gates touch before the first paint");
    fill(e.back(), domfix::demo(2,true)); e.commit();
    expect(e.scene().nDots==6, "engine: served scene is the committed snapshot");
    expect(e.pending(), "engine: entry queued a (full) repaint");

    bool done=false, bounded=true, gatedThroughout=true;
    std::vector<int> cover(W*H,0);
    while (e.pending()){
        StripScheduler::Slice sl = e.nextSlice(4);
        if (sl.y1-sl.y0>64) bounded=false;
        if (sl.y1>sl.y0) comp.renderRows(W,H,e.scene(),sl.y0,sl.y1,fb);
        for(int y=sl.y0;y<sl.y1;y++) for(int x=0;x<W;x++) cover[y*W+x]++;
        e.recordSlice(100,900);
        if (!sl.done && e.acceptsTouch()) gatedThroughout=false;
        done=sl.done;
    }
    bool once=true; for(int i=0;i<W*H;i++) if(cover[i]!=1){once=false;break;}
    expect(bounded && done, "engine: full entry repaint delivered in bounded slices");
    expect(once && !fb.oob, "engine: the entry frame paints every pixel exactly once");
    expect(gatedThroughout, "engine: touch stays gated for the whole entry paint");
    expect(e.acceptsTouch(), "engine: touch accepted only after the entry paint completes");
    expect(e.metrics().frames==1, "engine: one frame counted on completion");

    // INCREMENTAL: a GPS-only change repaints just the status band.
    domr::DomainScene g = domfix::demo(2,true); g.gps = {true,3,12,30000};
    fill(e.back(), g); e.commit();
    expect(e.pending(), "engine: a change queues a repaint");
    const uieng::DomainLayout L = uieng::domain(W,H);
    const int statusBottom = L.statusCells[0].y + L.statusCells[0].h;
    const int statusBand = ((statusBottom + 15) / 16) * 16;
    int rows=0; bool withinStatus=true;
    while (e.pending()){ StripScheduler::Slice sl=e.nextSlice(100); rows+=sl.y1-sl.y0; if(sl.y1>statusBand) withinStatus=false; }
    expect(!withinStatus && rows>=H-44, "engine: stale GPS repaints map as well as status");

    // STEADY: an unchanged scene queues no repaint.
    fill(e.back(), g); e.commit();
    expect(!e.pending(), "engine: an unchanged scene queues no repaint");

    // LEAVE/RE-ENTRY: leaving mid-frame discards pending slices; re-entry
    // services a fresh full frame from the current scene.
    e.enter(); fill(e.back(), domfix::demo(1,true)); e.commit();
    e.nextSlice(1);                                   // partial
    expect(e.pending(), "reentry: frame left partially serviced");
    e.leave();
    expect(!e.pending(), "reentry: leave discards the partial frame's pending slices");
    e.enter(); fill(e.back(), domfix::demo(0,true)); e.commit();
    int rr=0; while (e.pending()){ StripScheduler::Slice sl=e.nextSlice(100); rr+=sl.y1-sl.y0; }
    expect(rr==H && e.acceptsTouch(), "reentry: re-entry services a fresh full frame, then accepts touch");
}

int main(){
    testStore();
    testDirty();
    testEngineIntegration();
    printf("\n%s  %d/%d checks passed\n", fails?"FAILED":"PASSED", checks-fails, checks);
    return fails?1:0;
}
