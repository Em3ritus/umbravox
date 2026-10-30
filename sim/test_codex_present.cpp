// Host test for the CODEX layout + presentation. CODEX has no delivered
// geometry.json oracle (that covers scan+domain only), so regions are asserted
// against the layout rules + structural invariants, for BOTH profiles. Verifies:
//   - filter bar tiles the body width in 4 cells (WIFI/5GHZ/BLE/LOCATED); rows
//     are 44px and fit the list (3 @ 480, 2 @ 320); every hit is in-bounds,
//     >=44px, distinct-slotted and non-overlapping;
//   - interaction bindings: WIFI/5GHZ/BLE -> CodexFilter 0/1/2, LOCATED ->
//     OpenLocatedCodex, each row -> OpenDeviceDetail bound to its EXACT key,
//     with slot != key; null/empty keys fabricate no rows;
//   - the shared decision-#5 helpers (ordering + pagination) drive the window.
// Build:
//   g++ -std=c++17 -I. -Isim sim/test_codex_present.cpp ui/palette.cpp \
//       ui/themepack.cpp ui/muffinseal.cpp ui/motif.cpp ui/canvas.cpp ui/font5x7.cpp -o /tmp/tcp -lm
#include "../ui/codex_present.h"
#include <cstdio>
#include <cstring>
using namespace uieng;

static int fails=0, checks=0;
static void expect(bool c,const char* m){ ++checks; if(!c){++fails;printf("  FAIL %s\n",m);} else printf("  ok   %s\n",m); }
static bool overlap(Rect a, Rect b){ return a.x<b.x+b.w && b.x<a.x+a.w && a.y<b.y+b.h && b.y<a.y+a.h; }

static const char* const KEYS[3] = {"w:aabbccddeeff","w:0011223344ff","b:112233445566"};

int main(){
    for (int P=0;P<2;++P){
        const int W=P?320:480, H=P?240:320;
        const CodexLayout x = codex(W,H);
        char t[8]; snprintf(t,sizeof t,"%d",W);

        // chrome parity with the shared formulas
        expect(x.body==Rect{8,52,W-16,H-104}, P?"320 body rect":"480 body rect");
        // filter bar: 4 cells tiling the body width exactly, each >=44 tall
        expect(x.filterBar==Rect{x.body.x,x.body.y,x.body.w,44}, "filter bar spans body top");
        int sumw=0; bool fcells=true;
        for(int i=0;i<4;i++){ sumw+=x.filters[i].w; if(x.filters[i].h!=44||x.filters[i].y!=x.body.y) fcells=false; }
        expect(fcells && sumw==x.body.w && x.filters[0].x==x.body.x
               && x.filters[3].x+x.filters[3].w==x.body.x+x.body.w,
               "filter cells tile the body width exactly (4 cells)");
        // compact 320 filter cell is the 76px the contract font note cites
        if(P) expect(x.filters[0].w==76, "320 filter cell is 76px (matches contract font note)");

        // rows: fixed 44px, within the list, expected count per profile
        const int wantRows = P?2:3;
        expect(x.nRows==wantRows, P?"320 list fits 2 rows":"480 list fits 3 rows");
        bool rowsOk=true;
        for(int i=0;i<x.nRows;i++){ const Rect r=x.rows[i];
            if(r.h!=44||r.w!=x.list.w||r.x!=x.list.x) rowsOk=false;
            if(r.y<x.list.y || r.y+r.h>x.list.y+x.list.h) rowsOk=false; }
        expect(rowsOk, "rows are 44px, full width, inside the list region");

        // --- hit set: bindings + structure ----------------------------------
        HitCtx ctx; ctx.rowKeys=KEYS; ctx.nRowKeys=3;
        HitList h = codexHits(W,H,ctx);
        // structural: bounds / >=44 / distinct slots / non-overlap
        bool ok=true;
        for(int j=0;j<h.count;j++){ const Rect r=h.items[j].rect;
            if(r.x<0||r.y<0||r.x+r.w>W||r.y+r.h>H) ok=false;
            if(r.w<44||r.h<44) ok=false;
            if(!h.items[j].slot||!h.items[j].slot[0]) ok=false;
            for(int k=0;k<j;k++){ if(!strcmp(h.items[j].slot,h.items[k].slot)) ok=false;
                                  if(overlap(r,h.items[k].rect)) ok=false; } }
        expect(ok, "hits: in-bounds, >=44px, distinct slots, non-overlapping");

        // chrome(8) + 4 filters + wantRows rows
        expect(h.count==8+4+2+wantRows, "hit count = 8 chrome + 4 filters + 2 pager controls + rows");

        auto findAction=[&](const char* slot)->const Hit*{
            for(int j=0;j<h.count;j++) if(!strcmp(h.items[j].slot,slot)) return &h.items[j]; return nullptr; };
        const Hit* wf=findAction("WIFI"); const Hit* g5=findAction("5 GHZ");
        const Hit* bl=findAction("BLE");  const Hit* lo=findAction("LOCATED");
        expect(wf&&wf->action==Action::CodexFilter&&wf->payload==0, "WIFI -> CodexFilter(0)");
        expect(g5&&g5->action==Action::CodexFilter&&g5->payload==1, "5 GHZ -> CodexFilter(1)");
        expect(bl&&bl->action==Action::CodexFilter&&bl->payload==2, "BLE -> CodexFilter(2)");
        expect(lo&&lo->action==Action::OpenLocatedCodex, "LOCATED -> OpenLocatedCodex (located scope)");

        // rows bind to the EXACT key, slot != key, action OpenDeviceDetail
        int rowHits=0; bool rowBind=true;
        for(int j=0;j<h.count;j++) if(h.items[j].action==Action::OpenDeviceDetail){
            const Hit& r=h.items[j]; const int b=r.payload;
            if(b<0||b>=wantRows) rowBind=false;
            else if(!r.key||strcmp(r.key,KEYS[b])!=0) rowBind=false;
            if(!r.slot||!strcmp(r.slot,r.key)) rowBind=false;
            rowHits++; }
        expect(rowHits==wantRows && rowBind, "rows -> OpenDeviceDetail bound to exact key (slot != key)");
    }

    { const char* keys[8]={"a","b","c","d","e","f","g","h"};HitCtx c;c.rowKeys=keys;c.nRowKeys=8;
      const auto h=codexHits(480,720,c);expect(h.count==20,"taller profiles stay within hit-list capacity"); }

    // null + empty keys fabricate no rows
    { HitCtx c; HitList h=codexHits(480,320,c);              // rowKeys null
      int rows=0; for(int j=0;j<h.count;j++) if(h.items[j].action==Action::OpenDeviceDetail) rows++;
      expect(rows==0 && h.count==14, "null keys: filters present, no fabricated rows"); }
    { static const char* const EMPTY[3]={"","",""}; HitCtx c; c.rowKeys=EMPTY; c.nRowKeys=3;
      HitList h=codexHits(480,320,c);
      int rows=0; for(int j=0;j<h.count;j++) if(h.items[j].action==Action::OpenDeviceDetail) rows++;
      expect(rows==0, "empty keys: no fabricated rows"); }

    // decision-#5 helpers drive the window (shared ordering + pagination)
    { domr::LocRec in[3]={ {"k2",-80}, {"k0",-40}, {"k1",-60} }; int idx[3];
      domr::orderLocated(in,3,idx);
      expect(!strcmp(in[idx[0]].key,"k0")&&!strcmp(in[idx[2]].key,"k2"), "codex order: strongest first (shared policy)");
      domr::Page p = domr::paginate(14, /*perPage*/3, /*anchor*/0);
      expect(p.lo==1&&p.hi==3&&p.total==14, "codex page: 1-3 / 14 (shared paginate)"); }

    printf("\n%s  %d/%d checks passed\n", fails?"FAILED":"PASSED", checks-fails, checks);
    return fails?1:0;
}
