// Host test for ui/scan_present.h -- the SCAN device adapter's pure core:
// stable scene snapshot (pre-projected blips), the resumable bounded strip
// scheduler, the transition/touch gate, the instrumentation accumulator, the
// scene diff, and the integrated entry->commit->service sequencer.
// Build:
//   g++ -std=c++17 -I. sim/test_scan_engine.cpp ui/palette.cpp ui/themepack.cpp \
//       ui/muffinseal.cpp ui/motif.cpp ui/canvas.cpp ui/font5x7.cpp -o /tmp/tse -lm
#include "../ui/scan_present.h"
#include "scan_fixture.h"
#include <cstdio>
#include <cstring>
#include <vector>
using namespace scanpres;

static int fails=0, checks=0;
static void expect(bool c,const char* m){ ++checks; if(!c){++fails;printf("  FAIL %s\n",m);} else printf("  ok   %s\n",m); }

// Copy a scene into a store (what the device snapshot does from live state).
static void fill(SceneStore& st, const scanr::ScanScene& s){
    st.begin(s.theme, s.now);
    st.gps(s.gps);
    st.status(s.nodes, s.hasCell, s.battPct, s.rec);
    st.counts(s.wifi, s.g5, s.ble);
    st.channel(s.ch, s.chBand);
    for (int i=0;i<s.nChannels && i<SceneStore::NCH;i++) st.setChannel(i, s.channels[i]);
    for (int i=0;i<s.nBlips;i++) st.addBlip(s.blips[i].bearing,s.blips[i].dist,s.blips[i].band,s.blips[i].lastSeen);
}

// A counting sink for the integration render.
struct FbSink : scanr::Sink {
    int W,H; std::vector<int> cnt; bool oob=false;
    FbSink(int w,int h):W(w),H(h),cnt(w*h,0){}
    void strip(int x,int y,int w,int h,const uint16_t*) override {
        for(int ry=0;ry<h;ry++)for(int rx=0;rx<w;rx++){int gx=x+rx,gy=y+ry;
            if(gx<0||gx>=W||gy<0||gy>=H){oob=true;continue;} cnt[gy*W+gx]++;}
    }
};

static void testStore(){
    SceneStore st;
    st.begin(/*theme*/2, /*now*/5000);
    st.gps({true,3,12,500});
    st.status(3,true,78, Rec::Off);
    st.counts(1665,418,2903);
    st.channel("CH 06","2.4 GHz");
    for (int i=0;i<SceneStore::NCH;i++) st.setChannel(i, i*3);
    st.addBlip(20,15,0,1000);
    st.addBlip(300,85,1, 500);

    const scanr::ScanScene& s = st.scene();
    expect(s.theme==2 && s.now==5000, "store: scalar scene fields carried");
    expect(s.nBlips==2 && s.nChannels==SceneStore::NCH, "store: counts wired");
    expect(s.blips[0].bearing==20 && s.blips[0].dist==15 && s.blips[0].band==0 && s.blips[0].lastSeen==1000,
           "store: blip 0 geometry carried verbatim (no re-hash)");
    expect(s.blips[1].bearing==300 && s.blips[1].band==1, "store: blip 1 geometry carried");
    expect(s.channels[7]==21 && !strcmp(s.ch,"CH 06"), "store: channels + label carried");

    SceneStore o; o.begin(0,0);
    int added=0; for (int i=0;i<SceneStore::MAXC+10;i++) if (o.addBlip(0,0,0,0)) added++;
    expect(added==SceneStore::MAXC && o.contactCount()==SceneStore::MAXC, "store: contact cap enforced");
    o.begin(0,0);
    expect(o.contactCount()==0, "store: begin() clears contacts");
}

static void testScheduler(){
    const int H=320;
    StripScheduler sc; sc.reset(H);
    expect(!sc.pending(), "sched: idle after reset");
    sc.invalidateAll();
    std::vector<int> cover(H,0);
    int steps=0, worst=0; bool done=false;
    while (sc.pending()){
        StripScheduler::Slice sl = sc.step(4);
        expect(sl.y1>sl.y0, "sched: slice is non-empty while pending");
        if (sl.y1-sl.y0>worst) worst=sl.y1-sl.y0;
        for (int y=sl.y0;y<sl.y1;y++) cover[y]++;
        steps++; done=sl.done;
    }
    bool once=true; for(int y=0;y<H;y++) if(cover[y]!=1){once=false;break;}
    expect(once, "sched: full repaint covers every row exactly once");
    expect(worst<=64, "sched: each slice bounded to <=maxStrips rows");
    expect(steps==5 && done, "sched: 320px/64 -> 5 bounded slices, last is done");

    sc.reset(H); sc.invalidate(40,50);
    StripScheduler::Slice p = sc.step(4);
    expect(p.y0==32 && p.y1==64 && p.done, "sched: partial span snaps to strip grid, one slice");
}

static void testTransition(){
    Transition t;
    expect(!t.acceptsTouch(), "trans: idle rejects touch");
    t.enter();  expect(t.needsFull() && !t.acceptsTouch(), "trans: entering forces full, swallows touch");
    t.settled();expect(!t.needsFull() && t.acceptsTouch(), "trans: settled accepts touch");
    t.enter();  expect(!t.acceptsTouch(), "trans: re-entry (theme/wake) swallows touch again");
    t.settled(); t.leave();
    expect(!t.acceptsTouch(), "trans: leaving returns to idle");
}

static void testMetrics(){
    PresentMetrics m;
    m.slice(100,900); m.slice(50,150); m.frameDone();
    expect(m.slices==2 && m.frames==1, "metrics: slice/frame counts");
    expect(m.composeUs==150 && m.transferUs==1050 && m.renderUs==1200, "metrics: compose/transfer/render sums");
    expect(m.maxSliceUs==1000, "metrics: max slice duration tracked");
    expect(m.renderPerMille(100)==12, "metrics: render-per-second (per-mille) over interval");

    // capture: first sample primes (adds nothing), later deltas ACCUMULATE
    m.capture(1000,200,50);               // prime
    expect(m.capFrames==0 && m.capUniq==0 && m.bleReports==0, "metrics: first capture sample primes, adds nothing");
    m.capture(1100,210,70);               // +100,+10,+20
    m.capture(1250,215,90);               // +150,+5,+20
    expect(m.capFrames==250 && m.capUniq==15 && m.bleReports==40, "metrics: capture deltas accumulate over the window");
    m.capture(5,5,5);                     // source RESET: reset-tolerant delta, still accumulates
    expect(m.capFrames==255 && m.capUniq==20 && m.bleReports==45, "metrics: reset-tolerant accumulation");

    m.clear();                            // window ends: sums zeroed, baseline kept
    expect(m.capFrames==0 && m.renderUs==0, "metrics: clear zeroes the window");
    m.capture(105,55,55);                 // continues from the last totals (5,5,5) -> +100,+50,+50
    expect(m.capFrames==100 && m.capUniq==50 && m.bleReports==50, "metrics: baseline survives clear (continuous deltas)");
}

static void testSceneDirty(){
    const int W=480,H=320;
    scanr::ScanScene a = scanfix::demo(0), b = scanfix::demo(0);   // now=5000
    bool st,bd;
    sceneDirty(W,H,a,b,st,bd); expect(!st && !bd, "diff: identical scenes -> nothing dirty");

    scanr::ScanScene g = scanfix::demo(0); g.gps = {true,3,12,30000};     // -> STALE label
    sceneDirty(W,H,a,g,st,bd); expect(st && !bd, "diff: GPS change -> status only");

    scanr::ScanScene cnt = scanfix::demo(0); cnt.wifi += 1;               // count tile changed
    sceneDirty(W,H,a,cnt,st,bd); expect(!st && bd, "diff: count change -> body (count tiles)");

    static int CH2[21]; for(int i=0;i<21;i++) CH2[i]=scanfix::CHANNELS[i]; CH2[4]+=20;
    scanr::ScanScene sp = scanfix::demo(0); sp.channels = CH2;
    sceneDirty(W,H,a,sp,st,bd); expect(!st && bd, "diff: spectrum change -> body");

    scanr::ScanScene tiny = scanfix::demo(0); tiny.now = 5100;            // clock ticked, nothing crossed
    sceneDirty(W,H,a,tiny,st,bd); expect(!st && !bd, "diff: tiny clock tick (no marker change) -> no repaint");

    scanr::ScanScene fade = scanfix::demo(0); fade.now = 25000;           // blips cross a fade step
    sceneDirty(W,H,a,fade,st,bd); expect(!st && bd, "diff: time-only blip fade -> body (marker repaint)");

    scanr::ScanScene exp = scanfix::demo(0); exp.now = 70000;             // blips expire (age>60s)
    sceneDirty(W,H,a,exp,st,bd); expect(!st && bd, "diff: time-only blip expiry -> body (erase marker)");
}

static void testEngineIntegration(){
    const int W=480,H=320;
    Engine e; e.reset(W,H);
    scanr::Compositor comp; FbSink fb(W,H);

    // ENTRY: the fix for finding 1 -- a snapshot is committed (which invalidates)
    // BEFORE any slice is served, so entry never queues a default/stale frame.
    e.enter();
    expect(!e.acceptsTouch(), "engine: entry gates touch before the first paint");
    fill(e.back(), scanfix::demo(2));          // populate the back buffer
    e.commit();
    expect(!strcmp(e.scene().ch,"CH 06"), "engine: served scene is the committed snapshot, not 'CH --'");
    expect(e.pending(), "engine: entry queued a (full) repaint");

    int steps=0; bool done=false, bounded=true, gatedThroughout=true;
    std::vector<int> cover(W*H,0);
    while (e.pending()){
        StripScheduler::Slice sl = e.nextSlice(4);
        if (sl.y1-sl.y0>64) bounded=false;
        if (sl.y1>sl.y0) comp.renderRows(W,H,e.scene(),sl.y0,sl.y1,fb);
        for(int y=sl.y0;y<sl.y1;y++) for(int x=0;x<W;x++) cover[y*W+x]++;
        e.recordSlice(100,900);
        if (!sl.done && !e.acceptsTouch()) {} else if (!sl.done) gatedThroughout=false;
        steps++; done=sl.done;
    }
    bool once=true; for(int i=0;i<W*H;i++) if(cover[i]!=1){once=false;break;}
    expect(bounded && done, "engine: full entry repaint delivered in bounded slices");
    expect(once && !fb.oob, "engine: the entry frame paints every pixel exactly once");
    expect(e.acceptsTouch(), "engine: touch accepted only after the entry paint completes");
    expect(e.metrics().frames==1, "engine: one frame counted on completion");

    // INCREMENTAL: a GPS-only change repaints just the status band (finding 2).
    scanr::ScanScene g = scanfix::demo(2); g.gps = {true,3,12,30000};   // fresh -> stale label
    fill(e.back(), g); e.commit();
    expect(e.pending(), "engine: a change queues a repaint");
    const uieng::ScanLayout L = uieng::scan(W,H);
    const int statusBottom = L.statusCells[0].y + L.statusCells[0].h;
    const int statusBand = ((statusBottom + 15) / 16) * 16;   // repaint snaps to the strip grid
    int rows=0; bool withinStatus=true;
    while (e.pending()){ StripScheduler::Slice sl=e.nextSlice(100); rows+=sl.y1-sl.y0; if(sl.y1>statusBand) withinStatus=false; }
    expect(withinStatus && rows==statusBand && statusBand<H, "engine: GPS-only change repaints just the status band");

    // STEADY, unchanged scene: nothing to repaint.
    fill(e.back(), g); e.commit();
    expect(!e.pending(), "engine: an unchanged scene queues no repaint");
}

static void testTouchGate(){
    TouchGate g;
    // a fresh press fires once, then stays consumed while held
    expect(g.edge(true,true)==true,  "touch: fresh accepted press fires a down-edge");
    expect(g.edge(true,true)==false, "touch: held press does not re-fire");
    expect(g.edge(true,true)==false, "touch: still held, still no edge");
    // release re-arms; next press fires
    g.edge(false,true);
    expect(g.edge(true,true)==true,  "touch: press after release fires again");

    // a press that begins gated is consumed: it must NOT fire when the gate opens mid-hold
    TouchGate g2;
    expect(g2.edge(true,false)==false, "touch: press during transition is swallowed");
    expect(g2.edge(true,true)==false,  "touch: same press does not fire when the gate opens (held)");
    g2.edge(false,true);
    expect(g2.edge(true,true)==true,   "touch: a new press after release fires normally");

    // reset() consumes an in-progress press (entry/exit): held finger must not click through
    TouchGate g3;
    g3.reset();                                   // a finger is already down at entry
    expect(g3.edge(true,true)==false,  "touch: reset() swallows a finger held from before (no click-through)");
    g3.edge(false,true);
    expect(g3.edge(true,true)==true,   "touch: after that finger lifts, a new press fires");
}

static void testEngineLeaveReentry(){
    const int W=480,H=320;
    Engine e; e.reset(W,H);
    // start a frame and service only part of it
    e.enter(); fill(e.back(), scanfix::demo(2)); e.commit();
    e.nextSlice(1);                                   // one bounded slice only -> partial
    expect(e.pending(), "reentry: frame left partially serviced");
    // leave mid-frame: the pending slices must be discarded
    e.leave();
    expect(!e.pending(), "reentry: leave discards the partial frame's pending slices");
    // re-enter with a different current scene: commit first, then a FRESH full frame
    e.enter();
    fill(e.back(), scanfix::demo(1)); e.commit();     // different theme/scene
    expect(e.pending() && !e.acceptsTouch(), "reentry: re-entry queues a full repaint and gates touch");
    int rows=0; while (e.pending()){ StripScheduler::Slice sl=e.nextSlice(100); rows+=sl.y1-sl.y0; }
    expect(rows==H, "reentry: re-entry services a fresh full frame from the current scene (no stale resume)");
    expect(e.acceptsTouch(), "reentry: touch accepted after the fresh frame completes");
}

int main(){
    testStore();
    testScheduler();
    testTransition();
    testMetrics();
    testSceneDirty();
    testTouchGate();
    testEngineIntegration();
    testEngineLeaveReentry();
    printf("\n%s  %d/%d checks passed\n", fails?"FAILED":"PASSED", checks-fails, checks);
    return fails?1:0;
}
