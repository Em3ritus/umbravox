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
    for (int P=0;P<3;++P){
        const int widths[]={480,320,320},heights[]={320,240,480};
        const int W=widths[P],H=heights[P];
        const MeshLayout m = mesh(W,H);
        expect(m.body==Rect{8,52,W-16,H-104}, P?"320 body":"480 body");
        expect(m.previous.h==44 && m.pager.h==44,"device selectors are touch sized");
        expect(m.list.y>=m.title.y+m.title.h && m.list.y+m.list.h<=m.tune.y,"content between selector and tabs");
        for(int page:{0,1,2,MESH_NETWORK_PAGE}){
            HitList h=meshHits(W,H,page,1);bool ok=true,enroll=false,reset=false,scanning=false,connection=false;
            for(int j=0;j<h.count;j++){
                const auto& hit=h.items[j];const Rect r=hit.rect;
                if(r.x<0||r.y<0||r.x+r.w>W||r.y+r.h>H||r.w<44||r.h<44)ok=false;
                for(int k=0;k<j;k++)if(overlap(r,h.items[k].rect)||!strcmp(hit.slot,h.items[k].slot))ok=false;
                enroll|=hit.action==Action::MeshEnroll;reset|=hit.action==Action::MeshResetNet;
                scanning|=!strcmp(hit.slot,"node-scanning");connection|=!strcmp(hit.slot,"node-connection");
            }
            expect(ok,"all visible hits bounded, nonoverlapping, >=44px");
            expect(enroll==(page==MESH_NETWORK_PAGE)&&reset==enroll,"network mutations only on Network view");
            expect(scanning==(page==1||page==2)&&connection==scanning,"diagnostic tabs only for selected node");
        }
        expect(meshNavigate(0,2,1)==1 && meshNavigate(1,2,1)==3 && meshNavigate(3,2,1)==0,"next cycles devices, not diagnostic pages");
        expect(meshNavigate(0,2,-1)==3 && meshNavigate(3,2,-1)==1,"previous cycles devices backwards");
        expect(meshNavigate(1,2,3)==2 && meshNavigate(2,2,2)==1,"tabs retain selected node");
        expect(meshNavigate(3,2,3)==4 && meshNavigate(4,2,2)==3,"second node tabs retain identity");
        expect(meshNavigate(0,0,1)==0 && meshNavigate(0,0,-1)==0,"empty fleet stays on handheld");
        expect(meshPage(4,1)==0,"removed node falls back to handheld");
        expect(meshNavigate(2,1,4)==MESH_NETWORK_PAGE && meshNavigate(MESH_NETWORK_PAGE,1,4)==0,"Network toggles independently");

    }
    printf("\n%s  %d/%d checks passed\n", fails?"FAILED":"PASSED", checks-fails, checks);
    return fails?1:0;
}
