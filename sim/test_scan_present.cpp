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

    // band + stable identity
    expect(projectContact({"x",-50,2,1000},1000,fr).band==2, "band preserved");
    { Marker a=projectContact({"same",-50,0,1000},1000,fr), b=projectContact({"same",-50,1,1000},1000,fr);
      expect(a.dx==b.dx && a.dy==b.dy, "same key+signal -> same (stable) bearing"); }
    { Marker a=projectContact({"keyA",-50,0,1000},1000,fr), b=projectContact({"keyB",-50,0,1000},1000,fr);
      expect(a.dx!=b.dx || a.dy!=b.dy, "different keys -> different bearing"); }

    // age -> marker fades
    { Marker young=projectContact({"x",-50,0,0},10000,fr), old=projectContact({"x",-50,0,0},50000,fr);
      expect(young.sizePx > old.sizePx, "marker fades (smaller) with age"); }

    // GPS state is explicit and never shows stale as fresh
    { ScanScene s; char b[16];
      s.gps=Gps::Fresh; s.gpsSats=12; uint16_t c=gpsLabel(s,b,16);
      expect(!strncmp(b,"3D",2) && strstr(b,"12") && c==pal::GREEN, "GPS fresh -> 3D <sats>, green");
      s.gps=Gps::Stale; s.gpsAgeMs=12000; c=gpsLabel(s,b,16);
      expect(!strncmp(b,"STALE",5) && strstr(b,"12") && c==pal::AMBER, "GPS stale -> STALE <age>, amber");
      s.gps=Gps::Acquiring; c=gpsLabel(s,b,16);
      expect(!strcmp(b,"ACQUIRING") && c==pal::DIM, "GPS acquiring -> ACQUIRING, dim"); }

    // recording state is explicit
    { ScanScene s; char b[16];
      s.rec=Rec::On;     uint16_t c=recLabel(s,b,16); expect(!strcmp(b,"REC")   && c==pal::RED,   "rec on -> REC, red");
      s.rec=Rec::NoCard; c=recLabel(s,b,16);          expect(!strcmp(b,"NO SD") && c==pal::AMBER, "rec no-card -> NO SD, amber");
      s.rec=Rec::Off;    c=recLabel(s,b,16);          expect(!strcmp(b,"STOP")  && c==pal::DIM,   "rec off -> STOP, dim"); }

    printf("\n%s  %d/%d checks passed\n", fails?"FAILED":"PASSED", checks-fails, checks);
    return fails?1:0;
}
