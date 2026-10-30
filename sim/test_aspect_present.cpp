// Host test for the ASPECT layout + hit set (no oracle; regions vs layout rules
// + structural invariants, both profiles). Verifies the preview column + three
// 44px stepper rows (44x44 [-][+] on the right), the THEME/SWEEP bindings, and
// that the ALERT steppers appear only when the alert choice is live.
// Build:
//   g++ -std=c++17 -I. -Isim sim/test_aspect_present.cpp ui/palette.cpp \
//       ui/themepack.cpp ui/muffinseal.cpp ui/motif.cpp ui/canvas.cpp ui/font5x7.cpp -o /tmp/tasp1 -lm
#include "../ui/aspect_present.h"
#include <cstdio>
#include <cstring>
using namespace uieng;

static int fails=0, checks=0;
static void expect(bool c,const char* m){ ++checks; if(!c){++fails;printf("  FAIL %s\n",m);} else printf("  ok   %s\n",m); }
static bool overlap(Rect a, Rect b){ return a.x<b.x+b.w && b.x<a.x+a.w && a.y<b.y+b.h && b.y<a.y+a.h; }

int main(){
    for (int P=0;P<2;++P){
        const int W=P?320:480, H=P?240:320;
        const AspectLayout a = aspect(W,H);
        expect(a.body==Rect{8,52,W-16,H-104}, P?"320 body":"480 body");
        // preview is the left column; it does not overlap the stepper rows
        expect(a.preview.x==a.body.x && a.preview.h==a.body.h, "preview fills the left column");
        bool geo=true;
        for(int i=0;i<3;i++){
            if(a.rows[i].h!=44) geo=false;
            if(a.minus[i].w!=44||a.minus[i].h!=44||a.plus[i].w!=44||a.plus[i].h!=44) geo=false;
            if(overlap(a.minus[i],a.plus[i])||overlap(a.preview,a.rows[i])) geo=false;
            if(a.plus[i].x+a.plus[i].w!=a.body.x+a.body.w) geo=false;
            if(a.minus[i].x < a.preview.x+a.preview.w) geo=false;
        }
        expect(geo, "3 stepper rows: 44px, 44x44 steppers right, clear of the preview");

        // alertLive -> 6 stepper hits; not live -> 4 (ALERT omitted)
        HitList hl = aspectHits(W,H,true);
        HitList hn = aspectHits(W,H,false);
        expect(hl.count==(W>=400?10:9)+6, "alertLive: chrome + THEME/SWEEP/ALERT steppers");
        expect(hn.count==(W>=400?10:9)+4, "not live: ALERT steppers omitted (not a dead target)");

        auto find=[&](HitList&h,const char* s)->const Hit*{ for(int j=0;j<h.count;j++) if(!strcmp(h.items[j].slot,s)) return &h.items[j]; return nullptr; };
        const Hit* tm=find(hl,"theme-"); const Hit* tp=find(hl,"theme+");
        expect(tm&&tm->action==Action::ThemeStep&&tm->payload==-1, "THEME - -> ThemeStep(-1)");
        expect(tp&&tp->action==Action::ThemeStep&&tp->payload==+1, "THEME + -> ThemeStep(+1)");
        const Hit* sw=find(hl,"sweep+");
        expect(sw&&sw->action==Action::SettingStep&&sw->payload==+2, "SWEEP + -> SettingStep(+2)");
        const Hit* al=find(hl,"alert+");
        expect(al&&al->action==Action::SettingStep&&al->payload==+3, "ALERT + -> SettingStep(+3) when live");
        expect(find(hn,"alert+")==nullptr, "ALERT + absent when not live");

        // structural on the live set
        bool ok=true;
        for(int j=0;j<hl.count;j++){ const Rect r=hl.items[j].rect;
            if(r.x<0||r.y<0||r.x+r.w>W||r.y+r.h>H||r.w<44||r.h<44) ok=false;
            for(int k=0;k<j;k++){ if(!strcmp(hl.items[j].slot,hl.items[k].slot)) ok=false;
                                  if(overlap(r,hl.items[k].rect)) ok=false; } }
        expect(ok, "aspect hits: in-bounds, >=44px, distinct slots, non-overlapping");
    }
    printf("\n%s  %d/%d checks passed\n", fails?"FAILED":"PASSED", checks-fails, checks);
    return fails?1:0;
}
