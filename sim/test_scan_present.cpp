// Host test for the SCAN presentation layer (pure projections in scan_render.h):
// contact identity/signal/band/age -> field markers, and GPS/recording state ->
// status labels. Build:
//   g++ -std=c++17 -I. sim/test_scan_present.cpp ui/palette.cpp ui/themepack.cpp \
//       ui/muffinseal.cpp ui/motif.cpp ui/canvas.cpp ui/font5x7.cpp -o /tmp/tsp -lm
#include "../ui/scan_render.h"
#include "../ui/palette.h"
#include <cstdio>
#include <cstring>
#include <cmath>
using namespace scanr;

static int fails=0, checks=0;
static void expect(bool c,const char* m){ ++checks; if(!c){++fails;printf("  FAIL %s\n",m);} else printf("  ok   %s\n",m); }
static int rad(const Marker& m){ return (int)std::lround(std::sqrt((double)(m.dx*m.dx + m.dy*m.dy))); }

int main(){
    pal::setTheme(pal::TH_UMBRAVOX);
    const int fr = 80;

    // fresh vs expired
    expect( projectContact({"x",-50,0,1000}, 2000,         fr).shown, "fresh contact shown");
    expect(!projectContact({"x",-50,0,1000}, 1000+60001,   fr).shown, "expired contact (age>60s) dropped");
    expect( projectContact({"x",-50,0,1000}, 1000+60000,   fr).shown, "contact at exactly 60s still shown");

    // signal -> radius (strong near centre, weak near rim)
    { Marker a=projectContact({"s",-30,0,1000},1000,fr), b=projectContact({"w",-90,0,1000},1000,fr);
      expect(rad(a) <= 2 && rad(b) >= fr-2 && rad(a) < rad(b), "radius: strong=centre, weak=rim"); }
    // mid signal lands between
    { Marker m=projectContact({"m",-60,0,1000},1000,fr); expect(rad(m)>fr/4 && rad(m)<3*fr/4, "radius: mid signal mid-field"); }

    // invalid bands are rejected before they can index the colour table
    expect(!projectContact({"x",-50, 3,1000},1000,fr).shown, "band 3 (out of range) rejected");
    expect(!projectContact({"x",-50,-1,1000},1000,fr).shown, "band -1 (negative) rejected");
    expect( projectContact({"x",-50, 2,1000},1000,fr).shown, "band 2 (BLE, valid) shown");

    // band + stable identity
    expect(projectContact({"x",-50,2,1000},1000,fr).band==2, "band preserved");
    { Marker a=projectContact({"same",-50,0,1000},1000,fr), b=projectContact({"same",-50,1,1000},1000,fr);
      expect(a.dx==b.dx && a.dy==b.dy, "same key+signal -> same (stable) bearing"); }
    { Marker a=projectContact({"keyA",-50,0,1000},1000,fr), b=projectContact({"keyB",-50,0,1000},1000,fr);
      expect(a.dx!=b.dx || a.dy!=b.dy, "different keys -> different bearing"); }

    // age -> marker fades
    { Marker young=projectContact({"x",-50,0,0},10000,fr), old=projectContact({"x",-50,0,0},50000,fr);
      expect(young.sizePx > old.sizePx, "marker fades (smaller) with age"); }

    // GPS projection mirrors ui::Gnss::fresh(): freshness is validity+age only
    // (strict 5s), independent of mode; mode only labels dimensionality.
    { char b[16]; uint16_t c;
      // freshness boundary is the existing strict FRESH_MS (valid && age < 5000)
      expect(gpsState({true,3,12,4999})==GpsState::Fresh, "age 4999ms -> Fresh (<5s)");
      expect(gpsState({true,3,12,5000})==GpsState::Stale, "age 5000ms -> Stale (boundary is strict)");
      expect(gpsState({true,3,12,5001})==GpsState::Stale, "age 5001ms -> Stale");
      // no valid position -> Acquiring regardless of mode/age
      expect(gpsState({false,3,12,10})==GpsState::Acquiring, "no valid position -> Acquiring (even mode 3)");
      // a valid position with mode unknown/none is NOT hidden behind Acquiring
      expect(gpsState({true,0,6,1000})==GpsState::Fresh, "valid fresh, mode unknown -> Fresh (not hidden)");
      expect(gpsState({true,0,6,9000})==GpsState::Stale, "valid aged, mode none/unknown -> Stale (not Acquiring)");
      // labels: dimensionality only when GSA reports it
      c=gpsLabel({true,3,12,500},b,16); expect(!strcmp(b,"3D 12SV") && c==pal::GREEN, "mode 3 fresh -> '3D 12SV', green");
      c=gpsLabel({true,2,7,500},b,16);  expect(!strcmp(b,"2D 7SV")  && c==pal::GREEN, "mode 2 fresh -> '2D 7SV', green");
      c=gpsLabel({true,0,6,500},b,16);  expect(!strcmp(b,"FIX 6SV") && c==pal::GREEN, "mode unknown fresh -> neutral 'FIX' (no dimensionality)");
      c=gpsLabel({true,1,6,500},b,16);  expect(!strcmp(b,"FIX 6SV"), "mode 1 (none) fresh -> neutral 'FIX', not 2D/3D");
      c=gpsLabel({true,3,12,12000},b,16); expect(!strncmp(b,"STALE",5) && strstr(b,"12") && c==pal::AMBER, "aged fix -> 'STALE <s>', amber");
      c=gpsLabel({false,0,0,0},b,16);   expect(!strcmp(b,"ACQUIRING") && c==pal::DIM, "no fix -> ACQUIRING, dim"); }

    // recording state is explicit
    { ScanScene s; char b[16];
      s.rec=Rec::On;     uint16_t c=recLabel(s,b,16); expect(!strcmp(b,"REC")   && c==pal::RED,   "rec on -> REC, red");
      s.rec=Rec::NoCard; c=recLabel(s,b,16);          expect(!strcmp(b,"NO SD") && c==pal::AMBER, "rec no-card -> NO SD, amber");
      s.rec=Rec::Off;    c=recLabel(s,b,16);          expect(!strcmp(b,"STOP")  && c==pal::DIM,   "rec off -> STOP, dim"); }

    printf("\n%s  %d/%d checks passed\n", fails?"FAILED":"PASSED", checks-fails, checks);
    return fails?1:0;
}
