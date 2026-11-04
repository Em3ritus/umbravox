// Host test for ui/files_present.h: FilesStore snapshot (owned row strings +
// confirm/status text), status/body scene diff, and the entry->commit->service
// sequencer. Opening the confirm modal and the delete result are body changes.
// Build:
//   g++ -std=c++17 -I. -Isim sim/test_files_engine.cpp ui/palette.cpp \
//       ui/themepack.cpp ui/muffinseal.cpp ui/motif.cpp ui/canvas.cpp ui/font5x7.cpp -o /tmp/tfe1 -lm
#include "../ui/files_present.h"
#include "files_fixture.h"
#include <cstdio>
#include <cstring>
#include <vector>
using namespace flspres;

static int fails=0, checks=0;
static void expect(bool c,const char* m){ ++checks; if(!c){++fails;printf("  FAIL %s\n",m);} else printf("  ok   %s\n",m); }

static void fill(FilesStore& st, const flsr::FilesScene& s){
    st.begin(s.theme, s.now); st.gps(s.gps); st.status(s.nodes,s.hasCell,s.battPct,s.rec);
    st.list(s.card, s.total, s.firstIndex, s.sel, s.deletable);
    st.bar(s.status, s.statusInk);
    st.modal(s.confirm, s.confirmName, s.confirmSize);
    for (int i=0;i<s.nRows;i++) st.addRow(s.rows[i].name, s.rows[i].size, s.rows[i].active);
}
struct FbSink : flsr::Sink {
    int W,H; std::vector<int> cnt; bool oob=false;
    FbSink(int w,int h):W(w),H(h),cnt(w*h,0){}
    void strip(int x,int y,int w,int h,const uint16_t*) override {
        for(int ry=0;ry<h;ry++)for(int rx=0;rx<w;rx++){int gx=x+rx,gy=y+ry;
            if(gx<0||gx>=W||gy<0||gy>=H){oob=true;continue;} cnt[gy*W+gx]++;} }
};

int main(){
    FilesStore st; fill(st, flsfix::demo(2));
    const flsr::FilesScene& o = st.scene();
    expect(o.nRows==3 && !strcmp(o.rows[0].name,"WD-20260127-153012") && o.rows[0].active, "store: rows copied, active flag carried");
    expect(o.card && o.sel==1 && o.deletable && o.total==9, "store: list state carried");
    FilesStore cap; cap.begin(0,0); int n=0;
    for(int i=0;i<FilesStore::MAXROWS+5;i++) if(cap.addRow("n","s",false)) n++;
    expect(n==FilesStore::MAXROWS, "store: row cap enforced");

    const flsr::FilesScene a = flsfix::demo(2), base = flsfix::demo(2);
    bool s,b;
    sceneDirty(a, base, s, b); expect(!s && !b, "diff: identical -> nothing dirty");
    flsr::FilesScene g = flsfix::demo(2); g.gps={true,3,12,30000};
    sceneDirty(a, g, s, b); expect(s && !b, "diff: GPS -> status only");
    flsr::FilesScene cf = flsfix::confirm(2);
    sceneDirty(a, cf, s, b); expect(!s && b, "diff: confirm modal up -> body only");
    flsr::FilesScene se = flsfix::demo(2); se.sel=2;
    sceneDirty(a, se, s, b); expect(!s && b, "diff: selection change -> body only");

    const int W=480,H=320;
    FilesEngine e; e.reset(W,H); flsr::Compositor comp; FbSink fb(W,H);
    e.enter(); fill(e.back(), flsfix::demo(2)); e.commit();
    expect(e.pending() && !e.acceptsTouch(), "engine: entry queues full repaint, gates touch");
    bool bounded=true,once=true,done=false; std::vector<int> cover(W*H,0);
    while (e.pending()){ StripScheduler::Slice sl=e.nextSlice(4);
        if(sl.y1-sl.y0>64) bounded=false;
        if(sl.y1>sl.y0) comp.renderRows(W,H,e.scene(),sl.y0,sl.y1,fb);
        for(int y=sl.y0;y<sl.y1;y++) for(int x=0;x<W;x++) cover[y*W+x]++; done=sl.done; }
    for(int i=0;i<W*H;i++) if(cover[i]!=1){once=false;break;}
    expect(bounded && done && once && !fb.oob, "engine: entry paints every pixel once in bounded slices");
    expect(e.acceptsTouch(), "engine: touch accepted after entry");
    fill(e.back(), flsfix::demo(2)); e.commit();
    expect(!e.pending(), "engine: unchanged scene queues no repaint");

    printf("\n%s  %d/%d checks passed\n", fails?"FAILED":"PASSED", checks-fails, checks);
    return fails?1:0;
}
