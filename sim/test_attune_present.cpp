// Host test for the ATTUNE layout + hit set (no oracle; regions vs layout rules
// + structural invariants, both profiles). Verifies: knob rows are 44px with
// >=44x44 [-][+] steppers; the pager/CLEAR footer buttons are >=44px; the
// stepper payloads encode the GLOBAL knob index + direction as +/-(knob+1);
// the pager cycles pages and CLEAR resets; pagination counts adapt per profile.
// Build:
//   g++ -std=c++17 -I. -Isim sim/test_attune_present.cpp ui/palette.cpp \
//       ui/themepack.cpp ui/muffinseal.cpp ui/motif.cpp ui/canvas.cpp ui/font5x7.cpp -o /tmp/tap1 -lm
#include "../ui/attune_present.h"
#include <cstdio>
#include <cstring>
using namespace uieng;

static int fails=0, checks=0;
static void expect(bool c,const char* m){ ++checks; if(!c){++fails;printf("  FAIL %s\n",m);} else printf("  ok   %s\n",m); }
static bool overlap(Rect a, Rect b){ return a.x<b.x+b.w && b.x<a.x+a.w && a.y<b.y+b.h && b.y<a.y+a.h; }

int main(){
    for (int P=0;P<2;++P){
        const int W=P?320:480, H=P?240:320;
        const AttuneLayout a = attune(W,H);
        const int per = attuneRowsPerPage(H), pages = attunePages(H);
        expect(per == (P?2:3), P?"320: 2 knob rows/page":"480: 3 knob rows/page");
        expect(pages == (kAttuneKnobs + per - 1)/per, "page count = ceil(knobs/perPage)");

        // rows 44px; steppers 44x44 inside the row's right edge; footer >=44
        bool geo=true;
        for (int i=0;i<per;i++){
            if (a.rows[i].h!=44) geo=false;
            if (a.minus[i].w!=44||a.minus[i].h!=44||a.plus[i].w!=44||a.plus[i].h!=44) geo=false;
            if (overlap(a.minus[i], a.plus[i])) geo=false;
            if (a.plus[i].x+a.plus[i].w != a.body.x+a.body.w) geo=false;
        }
        expect(geo, "knob rows 44px with two 44x44 steppers at the right");
        expect(a.pager.w>=44 && a.pager.h==44 && a.clear.w>=44 && a.clear.h==44 && !overlap(a.pager,a.clear),
               "footer pager + CLEAR are >=44px and don't overlap");
        expect(a.footer.y+a.footer.h==a.body.y+a.body.h, "footer sits at the body bottom");

        // hits on page 0: steppers + pager + clear + chrome
        HitList h = attuneHits(W,H,0);
        bool structural=true;
        for(int j=0;j<h.count;j++){ const Rect r=h.items[j].rect;
            if(r.x<0||r.y<0||r.x+r.w>W||r.y+r.h>H||r.w<44||r.h<44) structural=false;
            for(int k=0;k<j;k++){ if(!strcmp(h.items[j].slot,h.items[k].slot)) structural=false;
                                  if(overlap(r,h.items[k].rect)) structural=false; } }
        expect(structural, "attune hits: in-bounds, >=44px, distinct slots, non-overlapping");
        const int visible = per < kAttuneKnobs ? per : kAttuneKnobs;
        expect(h.count == 8 + 2*visible + 2, "hit count = chrome + 2*rows + pager + clear");

        // stepper payload encoding: knob0 minus=-1 plus=+1; knob1 minus=-2 plus=+2 ...
        auto find=[&](const char* slot)->const Hit*{ for(int j=0;j<h.count;j++) if(!strcmp(h.items[j].slot,slot)) return &h.items[j]; return nullptr; };
        bool enc=true;
        for(int i=0;i<visible;i++){ char ms[8],ps[8]; snprintf(ms,sizeof ms,"knob%d-",i); snprintf(ps,sizeof ps,"knob%d+",i);
            const Hit* mn=find(ms); const Hit* pl=find(ps);
            if(!mn||mn->action!=Action::SettingStep||mn->payload!=-(i+1)) enc=false;
            if(!pl||pl->action!=Action::SettingStep||pl->payload!=+(i+1)) enc=false; }
        expect(enc, "steppers encode +/-(knob+1): minus down, plus up");
        const Hit* pg=find("pager"); const Hit* cl=find("clear");
        expect(pg && pg->action==Action::SettingPage && pg->payload==(1%pages), "pager -> SettingPage (next page)");
        expect(cl && cl->action==Action::SettingReset, "CLEAR -> SettingReset");

        // page 1 steppers bind the NEXT block of knobs (global index continues)
        if (pages > 1) {
            HitList h1 = attuneHits(W,H,1);
            const Hit* m0=nullptr; for(int j=0;j<h1.count;j++) if(!strcmp(h1.items[j].slot,"knob0-")) m0=&h1.items[j];
            expect(m0 && m0->payload==-(per+1), "page 1 first knob continues the global index");
        }
    }
    printf("\n%s  %d/%d checks passed\n", fails?"FAILED":"PASSED", checks-fails, checks);
    return fails?1:0;
}
