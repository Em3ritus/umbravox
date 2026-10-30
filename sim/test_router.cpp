// Host test for ui/router.h -- the interaction router. Drives it with hits from
// the REAL per-screen hit sets and asserts: nav-bar + count-tile + located
// transitions; DETAIL open/back preserving the originating list's scope+scroll;
// DOMAIN range stepping with endpoint clamp; MENU launch + LABELS; the ATTUNE
// vs ASPECT SettingStep disambiguation by screen; MESH / FILES / status side
// effects; and the FILES confirm flow. Header-only (no draw deps).
// Build:
//   g++ -std=c++17 -I. -Isim sim/test_router.cpp -o /tmp/trt -lm
#include "../ui/router.h"
#include <cstdio>
#include <cstring>
using namespace uieng;
using uinav::Router;
using uinav::Screen;

static int fails=0, checks=0;
static void expect(bool c,const char* m){ ++checks; if(!c){++fails;printf("  FAIL %s\n",m);} else printf("  ok   %s\n",m); }

static const Hit* slot(const HitList& h, const char* s){ for(int i=0;i<h.count;i++) if(!strcmp(h.items[i].slot,s)) return &h.items[i]; return nullptr; }
static const Hit* act(const HitList& h, Action a){ for(int i=0;i<h.count;i++) if(h.items[i].action==a) return &h.items[i]; return nullptr; }

static HitCtx listCtx(){ HitCtx c; static const char* const K[4]={"k0","k1","k2","k3"}; c.hasFix=true; c.rowKeys=K; c.nRowKeys=4; return c; }

int main(){
    const int W=480,H=320;

    // nav bar: SCAN -> DOMAIN / CODEX / MENU
    { Router r; HitList h=scanHits(W,H,HitCtx{});
      r.dispatch(*slot(h,"nav-domain")); expect(r.screen()==Screen::Domain, "nav-domain -> DOMAIN");
      r.dispatch(*slot(h,"nav-menu"));   expect(r.screen()==Screen::Menu,   "nav-menu -> MENU"); }

    // SCAN count tile -> CODEX pre-filtered; status cells -> side effects / MESH
    { Router r; HitList h=scanHits(W,H,HitCtx{});
      r.dispatch(*slot(h,"5 GHZ")); expect(r.screen()==Screen::Codex && r.codexScope()==1, "count tile 5GHZ -> CODEX scope 1");
      auto rq=r.dispatch(*slot(h,"logging")); expect(rq.logToggle, "LOG cell -> logToggle request");
      rq=r.dispatch(*slot(h,"gps")); expect(rq.openGps, "GPS cell -> openGps request");
      r.dispatch(*slot(h,"mesh")); expect(r.screen()==Screen::Mesh, "MESH cell -> MESH screen"); }

    // DOMAIN all-contacts -> located CODEX
    { Router r; HitList h=domainHits(W,H,listCtx());
      r.dispatch(*slot(h,"all-contacts")); expect(r.screen()==Screen::Codex && r.codexScope()==uinav::kCodexLocated,
          "DOMAIN all-contacts -> located CODEX"); }

    // CODEX row -> DETAIL, then BACK restores CODEX scope + scroll
    { Router r; HitList h=codexHits(W,H,listCtx());
      r.dispatch(*slot(h,"BLE")); r.setCodexScroll(5);          // scope=2, scroll=5
      expect(r.screen()==Screen::Codex && r.codexScope()==2 && r.codexScroll()==5, "CODEX pre-state set");
      const Hit* row=act(h,Action::OpenDeviceDetail);
      r.dispatch(*row);
      expect(r.screen()==Screen::Detail && r.detailOrigin()==Screen::Codex, "CODEX row -> DETAIL, origin remembered");
      expect(!strcmp(r.detailKey(), row->key), "DETAIL bound to the row's device key");
      HitList d=detailHits(W,H);
      r.dispatch(*slot(d,"back"));
      expect(r.screen()==Screen::Codex && r.codexScope()==2 && r.codexScroll()==5,
             "BACK -> CODEX with scope + scroll preserved"); }

    // DOMAIN range stepping clamps at both ends (ladder of 6)
    { Router r(6); HitList h=domainHits(W,H,listCtx());
      const Hit* plus=slot(h,"range-plus"); const Hit* minus=slot(h,"range-minus");
      for(int i=0;i<10;i++) r.dispatch(*plus);  expect(r.rangeIdx()==5, "range + clamps at the top rung");
      for(int i=0;i<10;i++) r.dispatch(*minus); expect(r.rangeIdx()==0, "range - clamps at the bottom rung"); }

    // MENU: launcher tiles navigate; LABELS toggles
    { Router r; HitList h=menuHits(W,H);
      r.dispatch(*slot(h,"menu-aspect")); expect(r.screen()==Screen::Aspect, "MENU aspect tile -> ASPECT");
      bool before=r.labelsPlain(); r.dispatch(*slot(h,"labels")); expect(r.labelsPlain()!=before, "LABELS toggles label mode"); }

    // MESH: ENROLL / RESET side effects
    { Router r; HitList h=meshHits(W,H);
      expect(r.dispatch(*slot(h,"enroll")).meshEnroll, "ENROLL -> meshEnroll");
      expect(r.dispatch(*slot(h,"reset")).meshReset,  "RESET -> meshReset"); }

    // ATTUNE: a knob step is a settingStep (NOT misread as an ASPECT sweep/alert)
    { Router r; HitList h=menuHits(W,H); r.dispatch(*slot(h,"menu-attune"));   // now on ATTUNE
      expect(r.screen()==Screen::Attune, "on ATTUNE");
      HitList a=attuneHits(W,H,0);
      auto rq=r.dispatch(*slot(a,"knob1+")); expect(rq.settingStep==+2 && rq.sweepStep==0, "ATTUNE knob1+ -> settingStep +2 (not sweep)");
      const Hit* pager=slot(a,"pager"); r.dispatch(*pager); expect(r.attunePage()==pager->payload, "pager sets attunePage to its target");
      rq=r.dispatch(*slot(a,"clear")); expect(rq.settingReset, "CLEAR -> settingReset"); }

    // ASPECT: ThemeStep + SettingStep(+2/+3) read as sweep/alert on this screen
    { Router r; HitList h=menuHits(W,H); r.dispatch(*slot(h,"menu-aspect"));   // on ASPECT
      HitList a=aspectHits(W,H,true);
      expect(r.dispatch(*slot(a,"theme+")).themeStep==+1, "ASPECT theme+ -> themeStep +1");
      expect(r.dispatch(*slot(a,"sweep+")).sweepStep==+1, "ASPECT sweep+ -> sweepStep +1");
      expect(r.dispatch(*slot(a,"alert+")).alertStep==+1, "ASPECT alert+ -> alertStep +1"); }

    // FILES: select, delete -> confirm, YES deletes + closes, NO just closes
    { Router r; HitList h=filesHits(W,H,false,true,2);
      r.dispatch(*slot(h,"file0")); expect(r.fileSel()==0, "FILES file0 -> select 0");
      r.dispatch(*slot(h,"delete")); expect(r.fileConfirm(), "DELETE -> confirm modal up");
      HitList c=filesHits(W,H,true,true,2);
      auto rq=r.dispatch(*slot(c,"yes")); expect(rq.fileDelete && !r.fileConfirm(), "YES -> fileDelete + modal closed");
      r.dispatch(*slot(h,"delete")); expect(r.fileConfirm(), "DELETE -> confirm again");
      r.dispatch(*slot(c,"no")); expect(!r.fileConfirm(), "NO -> modal closed, no delete"); }

    printf("\n%s  %d/%d checks passed\n", fails?"FAILED":"PASSED", checks-fails, checks);
    return fails?1:0;
}
