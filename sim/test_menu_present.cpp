// Host test for the MENU (hub) layout + hit set. No geometry.json oracle exists
// for MENU, so regions are asserted against the layout rules + structural
// invariants, for BOTH profiles. Verifies:
//   - the body holds a 3x3 grid of 9 cells that tile the body exactly and each
//     stay >=44x44 at both sizes (480x320 and 320x240);
//   - every hit is in-bounds, >=44px, distinct-slotted, non-overlapping;
//   - bindings: the 8 launcher tiles -> Nav with the right ScreenId payload (in
//     the delivered GRIMOIRE order), the 9th -> ToggleLabels, plus 8 chrome.
// Build:
//   g++ -std=c++17 -I. -Isim sim/test_menu_present.cpp ui/palette.cpp \
//       ui/themepack.cpp ui/muffinseal.cpp ui/motif.cpp ui/canvas.cpp ui/font5x7.cpp -o /tmp/tmp_ -lm
#include "../ui/menu_present.h"
#include <cstdio>
#include <cstring>
using namespace uieng;

static int fails=0, checks=0;
static void expect(bool c,const char* m){ ++checks; if(!c){++fails;printf("  FAIL %s\n",m);} else printf("  ok   %s\n",m); }
static bool overlap(Rect a, Rect b){ return a.x<b.x+b.w && b.x<a.x+a.w && a.y<b.y+b.h && b.y<a.y+a.h; }

int main(){
    for (int P=0;P<2;++P){
        const int W=P?320:480, H=P?240:320;
        const MenuLayout m = menu(W,H);
        expect(m.body==Rect{8,52,W-16,H-104}, P?"320 body rect":"480 body rect");
        expect(m.nCells==9 && m.cols==3 && m.rows==3, "menu: 3x3 grid, 9 cells");

        // cells tile the body exactly (no gaps/overlaps) and are each >=44x44
        bool fills=true, big=true;
        long area=0;
        for(int i=0;i<9;i++){ const Rect c=m.cells[i];
            area += (long)c.w*c.h;
            if(c.w<44||c.h<44) big=false;
            if(c.x<m.body.x||c.y<m.body.y||c.x+c.w>m.body.x+m.body.w||c.y+c.h>m.body.y+m.body.h) fills=false;
            for(int j=0;j<i;j++) if(overlap(c,m.cells[j])) fills=false; }
        expect(big, "menu: every cell >=44x44 at this profile");
        expect(fills && area==(long)m.body.w*m.body.h, "menu: cells tile the body exactly (area == body, no overlap)");

        // hit set: structure + count
        HitList h = menuHits(W,H);
        bool ok=true;
        for(int j=0;j<h.count;j++){ const Rect r=h.items[j].rect;
            if(r.x<0||r.y<0||r.x+r.w>W||r.y+r.h>H) ok=false;
            if(r.w<44||r.h<44) ok=false;
            if(!h.items[j].slot||!h.items[j].slot[0]) ok=false;
            for(int k=0;k<j;k++){ if(!strcmp(h.items[j].slot,h.items[k].slot)) ok=false;
                                  if(overlap(r,h.items[k].rect)) ok=false; } }
        expect(ok, "menu hits: in-bounds, >=44px, distinct slots, non-overlapping");
        expect(h.count==8+8+1, "menu hits: 8 chrome + 8 launchers + 1 labels");

        // bindings: each launcher -> Nav(ScreenId); labels -> ToggleLabels
        auto find=[&](const char* slot)->const Hit*{
            for(int j=0;j<h.count;j++) if(!strcmp(h.items[j].slot,slot)) return &h.items[j]; return nullptr; };
        struct B { const char* slot; ScreenId id; };
        const B want[8] = {
            {"menu-scan",ScreenId::Scan}, {"menu-codex",ScreenId::Codex},
            {"menu-domain",ScreenId::Domain}, {"menu-echoes",ScreenId::Echoes},
            {"menu-mesh",ScreenId::Mesh}, {"menu-omens",ScreenId::Omens},
            {"menu-attune",ScreenId::Attune}, {"menu-aspect",ScreenId::Aspect} };
        bool bind=true;
        for(int i=0;i<8;i++){ const Hit* t=find(want[i].slot);
            if(!t||t->action!=Action::Nav||t->payload!=(int)want[i].id) bind=false; }
        expect(bind, "menu: 8 launcher tiles bind Nav -> correct ScreenId payload");
        const Hit* lab=find("labels");
        expect(lab && lab->action==Action::ToggleLabels, "menu: LABELS tile -> ToggleLabels");
    }

    printf("\n%s  %d/%d checks passed\n", fails?"FAILED":"PASSED", checks-fails, checks);
    return fails?1:0;
}
