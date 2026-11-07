// Host test for the DOMAIN presentation layer (pure projections). Verifies:
//   - range ladder: -/+ step to an adjacent EXISTING range and clamp at the ends,
//     and an off-ladder stored value snaps to the nearest rung;
//   - list ordering (open decision #5): strongest signal first, ties broken by
//     ascending stable key -- deterministic, stable sort;
//   - pagination: fixed pages sized to the row capacity, 1-based lo-hi/total,
//     last page clamped, empty list reads 0-0/0;
//   - row-identity freeze: the order is latched at touch-down and held across a
//     live reorder until release (no reshuffle under the finger);
//   - radar geometry stays inside the map rect at both profiles.
// Build:
//   g++ -std=c++17 -I. -Isim sim/test_domain_present.cpp ui/palette.cpp \
//       ui/themepack.cpp ui/muffinseal.cpp ui/motif.cpp ui/canvas.cpp ui/font5x7.cpp -o /tmp/tdp -lm
#include "../ui/domain_present.h"
#include <cstdio>
#include <cstring>
using namespace domr;

static int fails = 0, checks = 0;
static void expect(bool c, const char* m) { ++checks; if (!c) { ++fails; printf("  FAIL %s\n", m); } else printf("  ok   %s\n", m); }

int main() {
    // --- range ladder --------------------------------------------------------
    expect(rangeStep(250, +1) == 500, "range +: 250 -> 500 (next rung)");
    expect(rangeStep(250, -1) == 100, "range -: 250 -> 100 (prev rung)");
    expect(rangeStep(10,-1)==10,"range: clamps at 10m minimum");
    expect(rangeStep(50,-1)==25 && rangeStep(25,-1)==10,"nearby observations have 25m and 10m zoom");
    expect(rangeAt(1)==25,"native default index is 25m");
    int idx=0;
    for(int i=0;i<kNRanges;++i) {
        expect(rangeAt(idx)==kRanges[i],"input index selects exactly the displayed/projected range");
        idx=rangeIndexStep(idx,1);
    }
    expect(idx==kNRanges-1 && rangeAt(idx)==2000 && rangeAt(999)==2000,"zoom out clamps without modulo wrap");
    for(int i=0;i<20;++i)idx=rangeIndexStep(idx,-1);
    expect(idx==0 && rangeAt(idx)==10,"zoom in clamps at 10m");
    expect(rangeStep(2000,+1) == 2000,"range +: clamps at the 2000m endpoint");
    expect(rangeStep(300, -1) == 100 && rangeStep(300,+1) == 500,
           "range: off-ladder 300 snaps to 250 then steps");
    expect(rangeStep(2500,+1) == 2000, "range: above-ladder snaps to top rung");

    // --- ordering policy (decision #5): strongest first, ties by key ---------
    {   LocRec in[5] = { {"k2",-70}, {"k0",-40}, {"k3",-70}, {"k1",-40}, {"k4",-90} };
        int idx[5]; orderLocated(in, 5, idx);
        // -40 group first (k0 before k1 by key), then -70 (k2 before k3), then -90
        expect(!strcmp(in[idx[0]].key,"k0") && !strcmp(in[idx[1]].key,"k1"),
               "order: strongest (-40) first, tie broken by ascending key");
        expect(!strcmp(in[idx[2]].key,"k2") && !strcmp(in[idx[3]].key,"k3"),
               "order: next band (-70), tie by key");
        expect(!strcmp(in[idx[4]].key,"k4"), "order: weakest (-90) last");
        // determinism: a second call on the same input yields the same order
        int idx2[5]; orderLocated(in, 5, idx2); bool same=true;
        for (int i=0;i<5;i++) if (idx[i]!=idx2[i]) same=false;
        expect(same, "order: deterministic (same input -> same order)");
    }

    // --- pagination ----------------------------------------------------------
    {   Page p = paginate(/*total*/9, /*perPage*/2, /*anchor*/0);
        expect(p.first==0 && p.count==2 && p.lo==1 && p.hi==2 && p.total==9, "page: first page 1-2/9");
        p = paginate(9, 2, 5);
        expect(p.first==4 && p.count==2 && p.lo==5 && p.hi==6, "page: anchor 5 -> block 5-6");
        p = paginate(9, 2, 8);                               // last item, odd tail
        expect(p.first==8 && p.count==1 && p.lo==9 && p.hi==9, "page: last page clamps to a single row");
        p = paginate(0, 2, 0);
        expect(p.first==0 && p.count==0 && p.lo==0 && p.hi==0 && p.total==0, "page: empty list reads 0-0/0");
        p = paginate(9, 2, 99);                              // anchor past the end
        expect(p.lo==9 && p.hi==9, "page: anchor past end clamps to the last block");
    }

    // --- row-identity freeze under a touch -----------------------------------
    {   dompres::FrozenOrder fo;
        const char* a[3] = {"A","B","C"};                    // live order before the touch
        fo.update(a, 3, /*touchDown*/false);
        expect(fo.count()==3 && !strcmp(fo.key(0),"A") && !fo.frozen(), "freeze: idle adopts the live order");
        fo.update(a, 3, /*touchDown*/true);                  // finger lands: latch
        expect(fo.frozen(), "freeze: touch-down latches the order");
        const char* b[3] = {"C","B","A"};                    // signal shifted -> would reorder
        fo.update(b, 3, /*touchDown*/true);                  // still held
        expect(!strcmp(fo.key(0),"A") && !strcmp(fo.key(2),"C"), "freeze: held order ignores a live reorder");
        fo.update(b, 3, /*touchDown*/false);                 // finger lifts: adopt the new order
        expect(!fo.frozen() && !strcmp(fo.key(0),"C"), "freeze: release adopts the new live order");
    }

    // --- radar geometry stays within the map rect ----------------------------
    for (int P=0;P<2;++P){
        const int W=P?320:480, H=P?240:320;
        const uieng::Rect m = uieng::domain(W,H).map;
        const RadarGeom g = radarGeom(m);
        const bool inside = g.cx-g.r >= m.x && g.cx+g.r <= m.x+m.w
                         && g.cy-g.r >= m.y && g.cy+g.r <= m.y+m.h && g.r > 0;
        expect(inside, P? "radar: fits inside the 320 map rect" : "radar: fits inside the 480 map rect");
    }

    printf("\n%s  %d/%d checks passed\n", fails?"FAILED":"PASSED", checks-fails, checks);
    return fails?1:0;
}
