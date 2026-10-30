// Host test for the FILES layout + hit set (no oracle; regions vs layout rules
// + structural invariants, both profiles). Verifies: 44px log rows above a 44px
// action bar; DELETE emitted only when a deletable log is selected; in confirm
// mode the hit set is chrome + YES + NO ONLY (rows + DELETE suppressed behind
// the modal); FileSelect payloads are the visible row indices.
// Build:
//   g++ -std=c++17 -I. -Isim sim/test_files_present.cpp ui/palette.cpp \
//       ui/themepack.cpp ui/muffinseal.cpp ui/motif.cpp ui/canvas.cpp ui/font5x7.cpp -o /tmp/tfp1 -lm
#include "../ui/files_present.h"
#include <cstdio>
#include <cstring>
using namespace uieng;

static int fails=0, checks=0;
static void expect(bool c,const char* m){ ++checks; if(!c){++fails;printf("  FAIL %s\n",m);} else printf("  ok   %s\n",m); }
static bool overlap(Rect a, Rect b){ return a.x<b.x+b.w && b.x<a.x+a.w && a.y<b.y+b.h && b.y<a.y+a.h; }

int main(){
    for (int P=0;P<2;++P){
        const int W=P?320:480, H=P?240:320;
        const FilesLayout f = files(W,H);
        expect(f.body==Rect{8,52,W-16,H-104}, P?"320 body":"480 body");
        expect(f.nRows == (P?2:3), P?"320: 2 log rows":"480: 3 log rows");
        bool rowsOk=true;
        for(int i=0;i<f.nRows;i++){ if(f.rows[i].h!=44||f.rows[i].w!=f.body.w) rowsOk=false;
            if(overlap(f.rows[i], f.bar)) rowsOk=false; }
        expect(rowsOk, "log rows 44px, full width, clear of the action bar");
        expect(f.bar.h==44 && f.bar.y+f.bar.h==f.body.y+f.body.h, "action bar 44px at body bottom");
        expect(f.del.w>=44 && f.del.h==44 && f.yes.w>=44 && f.no.w>=44 && !overlap(f.yes,f.no),
               "DELETE and YES/NO are >=44px; YES/NO don't overlap");

        // normal mode, a deletable selection: chrome + rows + DELETE
        HitList h = filesHits(W,H,/*confirm*/false,/*deletable*/true,/*nVisible*/f.nRows);
        expect(h.count == (W>=400?9:8) + f.nRows + 1, "normal: chrome + rows + DELETE");
        bool ok=true; int sel=0; const Hit* del=nullptr;
        for(int j=0;j<h.count;j++){ const Rect r=h.items[j].rect;
            if(r.x<0||r.y<0||r.x+r.w>W||r.y+r.h>H||r.w<44||r.h<44) ok=false;
            if(h.items[j].action==Action::FileSelect){ if(h.items[j].payload!=sel) ok=false; sel++; }
            if(!strcmp(h.items[j].slot,"delete")) del=&h.items[j];
            for(int k=0;k<j;k++){ if(!strcmp(h.items[j].slot,h.items[k].slot)) ok=false;
                                  if(overlap(r,h.items[k].rect)) ok=false; } }
        expect(ok && sel==f.nRows, "rows -> FileSelect with ascending visible-index payloads");
        expect(del && del->action==Action::FileDelete, "DELETE -> FileDelete when deletable");

        // not deletable: DELETE omitted
        HitList hnd = filesHits(W,H,false,false,f.nRows);
        bool noDel=true; for(int j=0;j<hnd.count;j++) if(hnd.items[j].action==Action::FileDelete) noDel=false;
        expect(hnd.count==(W>=400?9:8)+f.nRows && noDel, "not deletable: no DELETE hit");

        // confirm mode: chrome + YES + NO only (rows + DELETE suppressed)
        HitList hc = filesHits(W,H,/*confirm*/true,true,f.nRows);
        bool rowsGone=true; const Hit* yes=nullptr; const Hit* no=nullptr;
        for(int j=0;j<hc.count;j++){ Action a=hc.items[j].action;
            if(a==Action::FileSelect||a==Action::FileDelete) rowsGone=false;
            if(a==Action::FileConfirmYes) yes=&hc.items[j];
            if(a==Action::FileConfirmNo)  no=&hc.items[j]; }
        expect(hc.count==(W>=400?9:8)+2 && rowsGone, "confirm: only chrome + YES + NO (list behind modal is inert)");
        expect(yes && no, "confirm: YES -> FileConfirmYes, NO -> FileConfirmNo");
    }
    printf("\n%s  %d/%d checks passed\n", fails?"FAILED":"PASSED", checks-fails, checks);
    return fails?1:0;
}
