// Host test for the MESH layout + hit set (no geometry.json oracle; regions
// asserted vs layout rules + structural invariants, both profiles). Verifies
// the body split (title/header/list/control-row), the two >=44px control
// buttons tile the control row, and the ENROLL/RESET bindings.
// Build:
//   g++ -std=c++17 -I. -Isim sim/test_mesh_present.cpp ui/palette.cpp \
//       ui/themepack.cpp ui/muffinseal.cpp ui/motif.cpp ui/canvas.cpp ui/font5x7.cpp -o /tmp/tmp1 -lm
#include "../ui/mesh_present.h"
#include <cstdio>
#include <cstring>
using namespace uieng;

static int fails=0, checks=0;
static void expect(bool c,const char* m){ ++checks; if(!c){++fails;printf("  FAIL %s\n",m);} else printf("  ok   %s\n",m); }
static bool overlap(Rect a, Rect b){ return a.x<b.x+b.w && b.x<a.x+a.w && a.y<b.y+b.h && b.y<a.y+a.h; }

int main(){
    for (int P=0;P<2;++P){
        const int W=P?320:480, H=P?240:320;
        const MeshLayout m = mesh(W,H);
        expect(m.body==Rect{8,52,W-16,H-104}, P?"320 body":"480 body");
        // body regions stack within the body and don't overlap
        expect(m.title.y==m.body.y && m.header.y>=m.title.y+m.title.h-4, "title above header");
        expect(m.list.y>=m.header.y+m.header.h-4 && m.list.y+m.list.h<=m.enroll.y, "list between header and controls");
        // control row: two >=44px buttons spanning the body width, non-overlapping
        expect(m.enroll.h==44 && m.reset.h==44 && m.enroll.w>=44 && m.reset.w>=44, "control buttons >=44x44");
        expect(!overlap(m.enroll,m.reset) && m.enroll.x==m.body.x
               && m.reset.x+m.reset.w==m.body.x+m.body.w, "ENROLL left, RESET right, no overlap");

        HitList h = meshHits(W,H);
        expect(h.count==10+4, "mesh hits: chrome + ENROLL + PAGE + TUNE + RESET");
        bool ok=true;
        for(int j=0;j<h.count;j++){ const Rect r=h.items[j].rect;
            if(r.x<0||r.y<0||r.x+r.w>W||r.y+r.h>H||r.w<44||r.h<44) ok=false;
            for(int k=0;k<j;k++){ if(!strcmp(h.items[j].slot,h.items[k].slot)) ok=false;
                                  if(overlap(r,h.items[k].rect)) ok=false; } }
        expect(ok, "mesh hits: in-bounds, >=44px, distinct slots, non-overlapping");
        auto find=[&](const char* s)->const Hit*{ for(int j=0;j<h.count;j++) if(!strcmp(h.items[j].slot,s)) return &h.items[j]; return nullptr; };
        const Hit* en=find("enroll"); const Hit* rs=find("reset");
        expect(en && en->action==Action::MeshEnroll,   "ENROLL -> MeshEnroll");
        expect(rs && rs->action==Action::MeshResetNet, "RESET -> MeshResetNet");
    }
    printf("\n%s  %d/%d checks passed\n", fails?"FAILED":"PASSED", checks-fails, checks);
    return fails?1:0;
}
