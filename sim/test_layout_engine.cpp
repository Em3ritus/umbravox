// Host test for ui/layout_engine.h. Asserts:
//  (1) layout regions reproduce geometry.json exactly for both profiles;
//  (2) the state-aware hit set matches the COMPLETE oracle (oracle_hits.inc) for
//      every SCAN/DOMAIN state at both sizes -- each hit bound to its action,
//      payload AND exact record key at its rect, plus bounds / >=44px / distinct
//      slots / non-overlap, and the full case set verified by membership+uniqueness;
//  (3) NEGATIVE checks: swapped actions, wrong payloads, swapped/wrong/empty/null
//      keys, and a missing/duplicate oracle case all fail.
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

static const char* const KEYS[4] = {"k0","k1","k2","k3"};
static const char* const MATCH   = "match";
static const int CONTACT_Y = 52, BAND = 44;

// ---------- (1) layout regions, exact, both profiles --------------------------
static void ckRect(const char* tag, Rect g, int x,int y,int w,int h) {
    expect(g==Rect{x,y,w,h}, "%-12s got (%d,%d,%d,%d) exp (%d,%d,%d,%d)", tag,g.x,g.y,g.w,g.h,x,y,w,h);
}
static void testLayout() {
    // SCAN is design #1 now: status/nav bands full-width, a 16px body margin at
    // 480 (8 at 320), and the design-1 tiles/hero/STOP (not 4 equal chrome cells).
    for (int P=0; P<2; ++P) {
        const int W=P?320:480, H=P?240:320;
        ScanLayout s = scan(W,H);
        ckRect("status", s.status, 0,0,W,44);
        ckRect("nav", s.nav, 0,H-44,W,44);
        ckRect("body", s.body, P?8:16, 52, P?304:448, H-104);
    }
    ckRect("counts480",   scan(480,320).counts, 16,52,132,216);
    ckRect("hero480",     scan(480,320).hero,   156,52,308,216);
    ckRect("tile2_480",   scan(480,320).tiles[2], 16,192,132,62);
    ckRect("stop480",     scan(480,320).stop, 398,276,82,44);
    ckRect("wordmark480", scan(480,320).wordmark, 16,13,96,16);
    ckRect("counts320",   scan(320,240).counts, 8,52,88,136);
    ckRect("hero320",     scan(320,240).hero,   104,52,208,136);
    ckRect("tile2_320",   scan(320,240).tiles[2], 8,142,88,46);
    ckRect("stop320",     scan(320,240).stop, 260,196,60,44);
    ckRect("Dmap480",   domain(480,320).map, 8,52,312,216);
    ckRect("Dcon480",   domain(480,320).contacts, 328,52,144,168);
    ckRect("Dplus480",  domain(480,320).plus, 428,224,44,44);
    ckRect("Dmap320",   domain(320,240).map, 8,52,184,136);
    ckRect("Dcon320",   domain(320,240).contacts, 200,52,112,88);
    ckRect("Dplus320",  domain(320,240).plus, 268,144,44,44);
}

// ---------- oracle id -> expected action/payload/key --------------------------
struct Expect { Action a; bool chkPay; int pay; bool needKey; const char* key; bool known; };
static Expect expectFor(const char* id, Rect r) {
    auto E=[](Action a,bool cp=false,int p=0){ return Expect{a,cp,p,false,nullptr,true}; };
    if(!strcmp(id,"gps"))         return E(Action::OpenGps);
    if(!strcmp(id,"mesh"))        return E(Action::OpenMesh);
    if(!strcmp(id,"power"))       return E(Action::PowerDetail);
    if(!strcmp(id,"logging"))     return E(Action::ToggleLogging);
    if(!strcmp(id,"nav-scan"))    return E(Action::Nav,true,0);
    if(!strcmp(id,"nav-domain"))  return E(Action::Nav,true,1);
    if(!strcmp(id,"nav-codex"))   return E(Action::Nav,true,2);
    if(!strcmp(id,"nav-menu"))    return E(Action::Nav,true,3);
    if(!strcmp(id,"stop"))        return E(Action::ToggleLogging);   // SCAN design #1: REC/STOP on the nav
    if(!strcmp(id,"WIFI"))        return E(Action::CodexFilter,true,0);
    if(!strcmp(id,"5 GHZ"))       return E(Action::CodexFilter,true,1);
    if(!strcmp(id,"BLE"))         return E(Action::CodexFilter,true,2);
    if(!strcmp(id,"range-minus")) return E(Action::RangeStep,true,-1);
    if(!strcmp(id,"range-plus"))  return E(Action::RangeStep,true,1);
    if(!strcmp(id,"all-contacts"))return E(Action::OpenLocatedCodex);
    if(!strcmp(id,"threat"))      return Expect{Action::OpenThreatDetail,false,0,true,MATCH,true};
    if(!strncmp(id,"row",3)) {    // row at band b binds original record b-1
        const int band = (r.y - CONTACT_Y) / BAND, rec = band - 1;
        return Expect{Action::OpenDeviceDetail,true,rec,true,(rec>=0&&rec<4)?KEYS[rec]:nullptr,true};
    }
    return Expect{Action::OpenGps,false,0,false,nullptr,false};   // unknown id -> never matches
}
static bool validKeyStr(const char* k){ return k && k[0]; }
// Engine list must contain a hit at the oracle rect with the expected action,
// payload, and (for record targets) a non-empty key distinct from the slot AND
// equal to the expected record key.
static bool bound(const HitList& h, const OHit& o) {
    const Rect orect{o.x,o.y,o.w,o.h};
    const Expect e = expectFor(o.id, orect);
    if(!e.known) return false;
    for(int j=0;j<h.count;j++){
        const Hit& g=h.items[j];
        if(!(g.rect==orect)) continue;
        if(g.action!=e.a) continue;
        if(e.chkPay && g.payload!=e.pay) continue;
        if(e.needKey){
            if(!validKeyStr(g.key)) continue;
            if(!g.slot || !strcmp(g.slot,g.key)) continue;         // slot != record key
            if(e.key && strcmp(g.key,e.key)!=0) continue;          // EXACT expected key
        }
        return true;
    }
    return false;
}
static bool overlap(Rect a, Rect b){ return a.x<b.x+b.w && b.x<a.x+a.w && a.y<b.y+b.h && b.y<a.y+a.h; }

static HitCtx ctxFor(const char* st) {
    HitCtx c;
    if(!strcmp(st,"active")||!strcmp(st,"stale")||!strcmp(st,"off")) { c.hasFix=true; c.rowKeys=KEYS; c.nRowKeys=4; }
    else if(!strcmp(st,"acquiring")) { c.hasFix=false; }
    else if(!strcmp(st,"threat"))    { c.hasFix=true; c.rowKeys=KEYS; c.nRowKeys=4; c.matchKey=MATCH; }
    return c;
}
static HitList hitsFor(const OCase& c) {
    const HitCtx ctx = ctxFor(c.state);
    return (strcmp(c.screen,"scan")==0) ? scanHits(c.W,c.H,ctx) : domainHits(c.W,c.H,ctx);
}
static int boundCount(const HitList& h, const OCase& c){ int n=0; for(int i=0;i<c.n;i++) if(bound(h,c.h[i])) ++n; return n; }

// ---------- (2) complete hit oracle: binding + case-set membership ------------
static void testCaseSet() {
    expect(ORACLE_N==ORACLE_EXPECTED, "oracle %d cases exp %d", ORACLE_N, ORACLE_EXPECTED);
    const char* SCR[2]={"scan","domain"}; const char* ST[5]={"active","acquiring","stale","off","threat"}; int SZ[2]={480,320};
    for(int a=0;a<2;a++) for(int s=0;s<5;s++) for(int z=0;z<2;z++){
        int n=0; for(int i=0;i<ORACLE_N;i++)
            if(!strcmp(ORACLE[i].screen,SCR[a])&&!strcmp(ORACLE[i].state,ST[s])&&ORACLE[i].W==SZ[z]) ++n;
        expect(n==1, "case %s-%s-%d present %dx (exp 1)", SCR[a],ST[s],SZ[z],n);
    }
}
static void testHits() {
    for (int ci=0; ci<ORACLE_N; ++ci) {
        const OCase& c = ORACLE[ci];
        const HitList h = hitsFor(c);
        char t[40]; snprintf(t,sizeof t,"%s-%s-%d",c.screen,c.state,c.W);
        int before = fails;
        expect(h.count==c.n, "%-18s count got %d exp %d", t, h.count, c.n);
        expect(boundCount(h,c)==c.n, "%-18s action/payload/key bound %d/%d", t, boundCount(h,c), c.n);
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
static int actionIndex(HitList& h, Action a){ for(int j=0;j<h.count;j++) if(h.items[j].action==a) return j; return -1; }
static int deviceCount(const HitList& h){ int n=0; for(int j=0;j<h.count;j++) if(h.items[j].action==Action::OpenDeviceDetail)++n; return n; }
static int threatCount(const HitList& h){ int n=0; for(int j=0;j<h.count;j++) if(h.items[j].action==Action::OpenThreatDetail)++n; return n; }

static void testNegative() {
    // swapped actions: gps<->logging must break full binding
    { const OCase& c=find("scan","active",480); HitList h=hitsFor(c);
      Action a=h.items[0].action; h.items[0].action=h.items[3].action; h.items[3].action=a;
      expect(boundCount(h,c) < c.n, "neg: swapped gps/logging action not detected"); }
    // wrong payload: mangle nav-domain payload
    { const OCase& c=find("scan","active",480); HitList h=hitsFor(c);
      h.items[5].payload = 99;
      expect(boundCount(h,c) < c.n, "neg: wrong nav payload not detected"); }
    // swapped ROW keys: k0<->k1 on their rects must break binding
    { const OCase& c=find("domain","active",480); HitList h=hitsFor(c);
      int i0=-1,i1=-1; for(int j=0;j<h.count;j++) if(h.items[j].action==Action::OpenDeviceDetail){ if(i0<0)i0=j; else i1=j; }
      const char* tk=h.items[i0].key; h.items[i0].key=h.items[i1].key; h.items[i1].key=tk;
      expect(boundCount(h,c) < c.n, "neg: swapped row keys not detected"); }
    // wrong THREAT key: replace with an unrelated valid key
    { const OCase& c=find("domain","threat",480); HitList h=hitsFor(c);
      int it=actionIndex(h,Action::OpenThreatDetail); h.items[it].key="unrelated";
      expect(boundCount(h,c) < c.n, "neg: wrong threat key not detected"); }
    // NULL keys: no fix/match -> no device or threat targets
    { HitCtx c; HitList h=domainHits(480,320,c);   // all defaults null
      expect(deviceCount(h)==0 && threatCount(h)==0 && h.count==11, "neg: null keys fabricate targets"); }
    // EMPTY keys: empty strings must not create targets
    { static const char* const EMPTY[2]={"",""}; HitCtx c; c.hasFix=true; c.rowKeys=EMPTY; c.nRowKeys=2; c.matchKey="";
      HitList hd=domainHits(480,320,c); HitList hs=scanHits(480,320,c);
      expect(deviceCount(hd)==0 && threatCount(hd)==0, "neg: empty keys make DOMAIN targets");
      expect(threatCount(hs)==0, "neg: empty matchKey makes SCAN threat"); }
    // record rows carry a key distinct from slot (every row, not just one)
    { const OCase& c=find("domain","active",480); HitList h=hitsFor(c); bool good=true;
      for(int j=0;j<h.count;j++) if(h.items[j].action==Action::OpenDeviceDetail)
          if(!validKeyStr(h.items[j].key)||!strcmp(h.items[j].slot,h.items[j].key)) good=false;
      expect(good, "neg: row slot/key not separated"); }
    // wide threat retains the SECOND row (k1), drops the first (k0)
    { const OCase& c=find("domain","threat",480); HitList h=hitsFor(c);
      bool k1=false,k0=false; for(int j=0;j<h.count;j++) if(h.items[j].action==Action::OpenDeviceDetail){
          if(h.items[j].key&&!strcmp(h.items[j].key,"k1"))k1=true; if(h.items[j].key&&!strcmp(h.items[j].key,"k0"))k0=true; }
      expect(k1 && !k0, "neg: wide-threat retains k1 drops k0 (k1=%d k0=%d)", k1, k0); }
}

int main() {
    printf("layout regions (exact, both profiles):\n"); testLayout();
    printf("case set (membership + uniqueness):\n");     testCaseSet();
    printf("hit oracle (%d cases, all states x both sizes):\n", ORACLE_N); testHits();
    printf("negative checks (detectors must bite):\n");   testNegative();
    printf("\n%s  %d/%d checks passed\n", fails ? "FAILED" : "PASSED", checks-fails, checks);
    return fails ? 1 : 0;
}
