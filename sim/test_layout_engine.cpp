// Host test for ui/layout_engine.h. Asserts:
//  (1) layout regions reproduce geometry.json exactly for both profiles;
//  (2) the state-aware hit set matches the COMPLETE oracle (oracle_hits.inc)
//      for every SCAN/DOMAIN state at both sizes -- each hit bound to its
//      action + payload (+ record key where applicable), plus bounds / >=44px /
//      distinct slots / non-overlap / full-case-set;
//  (3) NEGATIVE checks: swapped actions, wrong payloads, missing keys, and a
//      short oracle all fail.
// Build + run:
//   python3 sim/gen_oracle.py <geometry.json> > sim/oracle_hits.inc
//   g++ -std=c++17 -Wall -I. -Isim sim/test_layout_engine.cpp -o /tmp/tle && /tmp/tle
#include "../ui/layout_engine.h"
#include "oracle_hits.inc"
#include <cstdio>
#include <cstring>
#include <cstdarg>
using namespace uieng;

static int fails = 0, checks = 0;
static void expect(bool cond, const char* fmt, ...) {
    ++checks;
    if (cond) return;
    ++fails;
    va_list ap; va_start(ap, fmt); printf("  FAIL "); vprintf(fmt, ap); printf("\n"); va_end(ap);
}

// ---------- (1) layout regions, exact, both profiles --------------------------
static void ckRect(const char* tag, Rect g, int x,int y,int w,int h) {
    expect(g==Rect{x,y,w,h}, "%-12s got (%d,%d,%d,%d) exp (%d,%d,%d,%d)", tag,g.x,g.y,g.w,g.h,x,y,w,h);
}
static void testLayout() {
    for (int P=0; P<2; ++P) {
        const int W = P? 320:480, H = P? 240:320, cw = W/4;
        ScanLayout s = scan(W,H);
        ckRect("status", s.status, 0,0,W,44);
        ckRect("nav", s.nav, 0,H-44,W,44);
        ckRect("body", s.body, 8,52,W-16,H-104);
        for (int i=0;i<4;i++){ ckRect("sCell",s.statusCells[i],i*cw,0,cw,44);
                               ckRect("nCell",s.navCells[i],i*cw,H-44,cw,44); }
    }
    ckRect("counts480", scan(480,320).counts, 8,52,111,216);
    ckRect("hero480",   scan(480,320).hero,   127,52,345,216);
    ckRect("tile2_480", scan(480,320).tiles[2], 8,196,111,72);
    ckRect("counts320", scan(320,240).counts, 8,52,88,136);
    ckRect("hero320",   scan(320,240).hero,   104,52,208,136);
    ckRect("tile2_320", scan(320,240).tiles[2], 8,142,88,46);
    ckRect("Dmap480",   domain(480,320).map, 8,52,312,216);
    ckRect("Dcon480",   domain(480,320).contacts, 328,52,144,168);
    ckRect("Dplus480",  domain(480,320).plus, 428,224,44,44);
    ckRect("Dmap320",   domain(320,240).map, 8,52,184,136);
    ckRect("Dcon320",   domain(320,240).contacts, 200,52,112,88);
    ckRect("Dplus320",  domain(320,240).plus, 268,144,44,44);
}

// ---------- oracle id -> runtime action/payload mapping -----------------------
struct Expect { Action a; bool chkPay; int pay; bool needKey; bool known; };
static Expect expectFor(const char* id) {
    auto E=[](Action a,bool cp=false,int p=0,bool nk=false){ return Expect{a,cp,p,nk,true}; };
    if(!strcmp(id,"gps"))         return E(Action::OpenGps);
    if(!strcmp(id,"mesh"))        return E(Action::OpenMesh);
    if(!strcmp(id,"power"))       return E(Action::PowerDetail);
    if(!strcmp(id,"logging"))     return E(Action::ToggleLogging);
    if(!strcmp(id,"nav-scan"))    return E(Action::Nav,true,0);
    if(!strcmp(id,"nav-domain"))  return E(Action::Nav,true,1);
    if(!strcmp(id,"nav-codex"))   return E(Action::Nav,true,2);
    if(!strcmp(id,"nav-menu"))    return E(Action::Nav,true,3);
    if(!strcmp(id,"WIFI"))        return E(Action::CodexFilter,true,0);
    if(!strcmp(id,"5 GHZ"))       return E(Action::CodexFilter,true,1);
    if(!strcmp(id,"BLE"))         return E(Action::CodexFilter,true,2);
    if(!strcmp(id,"range-minus")) return E(Action::RangeStep,true,-1);
    if(!strcmp(id,"range-plus"))  return E(Action::RangeStep,true,1);
    if(!strcmp(id,"all-contacts"))return E(Action::OpenLocatedCodex);
    if(!strcmp(id,"threat"))      return E(Action::OpenThreatDetail,false,0,true);
    if(!strncmp(id,"row",3))      return E(Action::OpenDeviceDetail,false,0,true);
    return Expect{Action::OpenGps,false,0,false,false};   // unknown -> never matches
}
// Does the engine list contain a hit at oracle rect with the expected action,
// payload, and (if a record target) a non-empty key distinct from the slot?
static bool bound(const HitList& h, const OHit& o) {
    const Expect e = expectFor(o.id);
    if (!e.known) return false;
    for (int j=0;j<h.count;j++) {
        const Hit& g = h.items[j];
        if (!(g.rect==Rect{o.x,o.y,o.w,o.h})) continue;
        if (g.action != e.a) continue;
        if (e.chkPay && g.payload != e.pay) continue;
        if (e.needKey && (!g.key || !g.key[0] || !g.slot || !strcmp(g.slot,g.key))) continue;
        return true;
    }
    return false;
}
static bool overlap(Rect a, Rect b) {
    return a.x<b.x+b.w && b.x<a.x+a.w && a.y<b.y+b.h && b.y<a.y+a.h;
}

static const char* const KEYS[4] = {"k0","k1","k2","k3"};
static HitCtx ctxFor(const char* st) {
    HitCtx c;
    if(!strcmp(st,"active")||!strcmp(st,"stale")||!strcmp(st,"off")) { c.hasFix=true; c.rowKeys=KEYS; c.nRowKeys=4; }
    else if(!strcmp(st,"acquiring")) { c.hasFix=false; }               // fix lost -> no mapped rows
    else if(!strcmp(st,"threat"))    { c.hasFix=true; c.rowKeys=KEYS; c.nRowKeys=4; c.matchKey="match"; }
    return c;
}
static HitList hitsFor(const OCase& c) {
    const HitCtx ctx = ctxFor(c.state);
    return (strcmp(c.screen,"scan")==0) ? scanHits(c.W,c.H,ctx) : domainHits(c.W,c.H,ctx);
}

// ---------- (2) complete hit oracle -------------------------------------------
static void testHits() {
    expect(ORACLE_N==ORACLE_EXPECTED, "oracle has %d cases, expected %d", ORACLE_N, ORACLE_EXPECTED);
    for (int ci=0; ci<ORACLE_N; ++ci) {
        const OCase& c = ORACLE[ci];
        const HitList h = hitsFor(c);
        char t[40]; snprintf(t,sizeof t,"%s-%s-%d",c.screen,c.state,c.W);
        int before = fails;
        expect(h.count==c.n, "%-18s count got %d exp %d", t, h.count, c.n);
        int boundN=0; for(int i=0;i<c.n;i++) if(bound(h,c.h[i])) ++boundN;
        expect(boundN==c.n, "%-18s action/payload/key bound %d/%d", t, boundN, c.n);
        bool ok=true;
        for(int j=0;j<h.count;j++){
            const Rect r=h.items[j].rect;
            if(r.x<0||r.y<0||r.x+r.w>c.W||r.y+r.h>c.H) ok=false;
            if(r.w<44||r.h<44) ok=false;
            if(!h.items[j].slot||!h.items[j].slot[0]) ok=false;
            for(int k=0;k<j;k++){ if(!strcmp(h.items[j].slot,h.items[k].slot)) ok=false;
                                  if(overlap(r,h.items[k].rect)) ok=false; }
        }
        expect(ok, "%-18s structural (bounds/min44/dup-slot/overlap)", t);
        printf("  %-4s %-18s n=%d\n", fails==before?"ok":"FAIL", t, h.count);
    }
}

// ---------- (3) negative checks: the detectors must bite ----------------------
static const OCase& find(const char* screen, const char* state, int W) {
    for(int i=0;i<ORACLE_N;i++) if(!strcmp(ORACLE[i].screen,screen)&&!strcmp(ORACLE[i].state,state)&&ORACLE[i].W==W) return ORACLE[i];
    static OCase none{}; return none;
}
static void testNegative() {
    // swapped actions: gps<->logging must break the gps binding
    { const OCase& c = find("scan","active",480); HitList h = hitsFor(c);
      Action a0=h.items[0].action; h.items[0].action=h.items[3].action; h.items[3].action=a0;
      OHit gps{0,0,120,44,"gps"};
      expect(!bound(h,gps), "negative: swapped gps/logging action not detected"); }
    // wrong payload: nav-domain payload mangled must break its binding
    { const OCase& c = find("scan","active",480); HitList h = hitsFor(c);
      h.items[5].payload = 99;   // nav-domain
      OHit nd{120,276,120,44,"nav-domain"};
      expect(!bound(h,nd), "negative: wrong nav payload not detected"); }
    // missing keys: hasFix but no rowKeys/matchKey -> NO device/threat targets
    { HitCtx c; c.hasFix=true;   // rowKeys=null, matchKey=null
      HitList h = domainHits(480,320,c);
      int dev=0,thr=0; for(int j=0;j<h.count;j++){ if(h.items[j].action==Action::OpenDeviceDetail)++dev;
                                                   if(h.items[j].action==Action::OpenThreatDetail)++thr; }
      expect(dev==0 && thr==0, "negative: fabricated targets without keys (dev=%d thr=%d)", dev, thr);
      expect(h.count==11, "negative: no-key domain-480 count got %d exp 11", h.count); }
    // a record row must carry a key distinct from its slot
    { HitCtx c; c.hasFix=true; c.rowKeys=KEYS; c.nRowKeys=4;
      HitList h = domainHits(480,320,c);
      bool good=true; for(int j=0;j<h.count;j++) if(h.items[j].action==Action::OpenDeviceDetail)
          if(!h.items[j].key||!strcmp(h.items[j].slot,h.items[j].key)) good=false;
      expect(good, "negative: row slot/key not separated"); }
    // wide threat retains the SECOND row's record (k1), not restart at k0
    { HitCtx c; c.hasFix=true; c.rowKeys=KEYS; c.nRowKeys=4; c.matchKey="match";
      HitList h = domainHits(480,320,c);
      bool hasK1=false, hasK0Row=false;
      for(int j=0;j<h.count;j++){ if(h.items[j].action==Action::OpenDeviceDetail){
          if(h.items[j].key && !strcmp(h.items[j].key,"k1")) hasK1=true;
          if(h.items[j].key && !strcmp(h.items[j].key,"k0")) hasK0Row=true; } }
      expect(hasK1 && !hasK0Row, "wide-threat retains second row k1 (k1=%d k0row=%d)", hasK1, hasK0Row); }
}

int main() {
    printf("layout regions (exact, both profiles):\n"); testLayout();
    printf("hit oracle (%d cases, all states x both sizes):\n", ORACLE_N); testHits();
    printf("negative checks (detectors must bite):\n"); testNegative();
    printf("\n%s  %d/%d checks passed\n", fails ? "FAILED" : "PASSED", checks-fails, checks);
    return fails ? 1 : 0;
}
