// Host test for ui/layout_engine.h. Asserts:
//  (1) layout regions reproduce geometry.json exactly for both profiles;
//  (2) the state-aware hit set matches the COMPLETE oracle (oracle_hits.inc,
//      generated from geometry.json) for every SCAN/DOMAIN state at both sizes,
//      with bounds / >=44px / distinct slots / non-overlap / threat-awareness.
// Build + run:
//   python3 sim/gen_oracle.py <geometry.json> > sim/oracle_hits.inc
//   g++ -std=c++17 -Wall -I. sim/test_layout_engine.cpp -o /tmp/tle && /tmp/tle
#include "../ui/layout_engine.h"
#include "oracle_hits.inc"
#include <cstdio>
#include <cstring>
using namespace uieng;

static int fails = 0, checks = 0;

static void ck(const char* tag, Rect got, int x, int y, int w, int h) {
    const Rect exp{x, y, w, h};
    ++checks;
    if (got == exp) {} // quiet on pass to keep the oracle output readable
    else { ++fails; printf("  FAIL %-14s got (%d,%d,%d,%d) exp (%d,%d,%d,%d)\n",
                           tag, got.x, got.y, got.w, got.h, x, y, w, h); }
}
static void chromeCk(const ScanLayout& s, int W, int H, int cw) {
    ck("status", s.status, 0,0,W,44);
    ck("nav", s.nav, 0,H-44,W,44);
    ck("body", s.body, 8,52,W-16,H-104);
    for (int i=0;i<4;i++){ ck("statusCell", s.statusCells[i], i*cw,0,cw,44);
                           ck("navCell", s.navCells[i], i*cw,H-44,cw,44); }
}

// --- layout region oracle (both profiles, exact) ------------------------------
static void testLayout() {
    { ScanLayout s = scan(480,320); chromeCk(s,480,320,120);
      ck("counts", s.counts, 8,52,111,216); ck("hero", s.hero, 127,52,345,216);
      ck("tile0", s.tiles[0], 8,52,111,72); ck("tile1", s.tiles[1], 8,124,111,72);
      ck("tile2", s.tiles[2], 8,196,111,72); }
    { ScanLayout s = scan(320,240); chromeCk(s,320,240,80);
      ck("counts", s.counts, 8,52,88,136); ck("hero", s.hero, 104,52,208,136);
      ck("tile0", s.tiles[0], 8,52,88,45); ck("tile1", s.tiles[1], 8,97,88,45);
      ck("tile2", s.tiles[2], 8,142,88,46); }
    { DomainLayout d = domain(480,320);
      ck("map", d.map, 8,52,312,216); ck("contacts", d.contacts, 328,52,144,168);
      ck("range", d.range, 328,224,144,44); ck("minus", d.minus, 328,224,44,44);
      ck("plus", d.plus, 428,224,44,44); }
    { DomainLayout d = domain(320,240);
      ck("map", d.map, 8,52,184,136); ck("contacts", d.contacts, 200,52,112,88);
      ck("range", d.range, 200,144,112,44); ck("minus", d.minus, 200,144,44,44);
      ck("plus", d.plus, 268,144,44,44); }
}

// --- complete hit oracle (every state, both sizes) ----------------------------
static HitCtx ctxFor(const char* state) {
    HitCtx c;
    static const char* const keys[4] = {"ka","kb","kc","kd"};
    c.rowKeys = keys; c.nRowKeys = 4;
    if (!strcmp(state,"acquiring")) { c.hasFix = false; c.nLocated = 0; }  // no fix -> no mapped rows
    if (!strcmp(state,"threat"))     c.threat = true;
    return c;
}
static bool req(const Rect& a, const ORect& b) { return a.x==b.x && a.y==b.y && a.w==b.w && a.h==b.h; }
static bool overlap(const Rect& a, const Rect& b) {
    return a.x < b.x+b.w && b.x < a.x+a.w && a.y < b.y+b.h && b.y < a.y+a.h;
}

static void testHits() {
    for (int ci = 0; ci < ORACLE_N; ci++) {
        const OCase& c = ORACLE[ci];
        const HitCtx ctx = ctxFor(c.state);
        const HitList h = (strcmp(c.screen,"scan")==0) ? scanHits(c.W,c.H,ctx) : domainHits(c.W,c.H,ctx);
        char tag[40]; snprintf(tag,sizeof tag,"%s-%s-%d",c.screen,c.state,c.W);
        int before = fails;

        ++checks;                                            // count
        if (h.count != c.n) { ++fails; printf("  FAIL %-18s count got %d exp %d\n",tag,h.count,c.n); }

        ++checks;                                            // rect bijection vs oracle
        bool used[20] = {false}; int matched = 0;
        for (int i=0;i<c.n;i++) for (int j=0;j<h.count;j++)
            if (!used[j] && req(h.items[j].rect, c.r[i])) { used[j]=true; ++matched; break; }
        if (matched != c.n) { ++fails; printf("  FAIL %-18s rect match %d/%d\n",tag,matched,c.n); }

        ++checks;                                            // bounds / >=44 / distinct / non-overlap
        bool ok = true;
        for (int j=0;j<h.count;j++) {
            const Rect r = h.items[j].rect;
            if (r.x<0||r.y<0||r.x+r.w>c.W||r.y+r.h>c.H) ok=false;
            if (r.w<44||r.h<44) ok=false;
            if (!h.items[j].slot || !h.items[j].slot[0]) ok=false;
            for (int k=0;k<j;k++) {
                if (!strcmp(h.items[j].slot,h.items[k].slot)) ok=false;
                if (overlap(r, h.items[k].rect)) ok=false;
            }
        }
        if (!ok) { ++fails; printf("  FAIL %-18s structural (bounds/min44/dup-slot/overlap)\n",tag); }

        ++checks;                                            // state-awareness
        int nthreat = 0;
        for (int j=0;j<h.count;j++) if (h.items[j].action==Action::OpenThreatDetail) ++nthreat;
        const int expThreat = (strcmp(c.state,"threat")==0) ? 1 : 0;
        if (nthreat != expThreat) { ++fails; printf("  FAIL %-18s threat targets %d exp %d\n",tag,nthreat,expThreat); }

        printf("  %-4s %-18s n=%d\n", (fails==before)?"ok":"FAIL", tag, h.count);
    }
}

int main() {
    printf("layout regions (exact, both profiles):\n");
    testLayout();
    printf("hit oracle (%d cases, all states x both sizes):\n", ORACLE_N);
    testHits();
    printf("\n%s  %d/%d checks passed\n", fails ? "FAILED" : "PASSED", checks - fails, checks);
    return fails ? 1 : 0;
}
