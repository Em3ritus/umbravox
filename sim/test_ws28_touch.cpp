#include "../device/cst3530_sample.h"
#include "../ui/display_controls.h"
#include <cassert>
#include <cstdio>
using namespace cst3530;
int main(){
 uint16_t x=999,y=999;uint8_t p[9]{};
 assert(decode(nullptr,9,x,y)==Result::Fail);
 for(size_t n=0;n<9;n++)assert(decode(p,n,x,y)==Result::Fail);
 assert(decode(p,9,x,y)==Result::Lift);
 p[3]=1;p[8]=0x10;p[4]=239;p[5]=63;p[7]=0x10;
 assert(decode(p,9,x,y)==Result::Touch && x==239 && y==319);
 p[3]=5;assert(decode(p,9,x,y)==Result::Touch);
 p[3]=6;assert(decode(p,9,x,y)==Result::Fail);
 p[3]=1;p[4]=240;assert(decode(p,9,x,y)==Result::Fail);
 p[4]=239;p[5]=64;assert(decode(p,9,x,y)==Result::Fail);
 p[8]=0;assert(decode(p,9,x,y)==Result::Lift);
 for(int nx=0;nx<240;nx++)for(int ny=0;ny<320;ny++){
  p[3]=1;p[8]=0x10;p[4]=nx;p[5]=ny&255;p[7]=((ny>>8)<<4)|(nx>>8);
  assert(decode(p,9,x,y)==Result::Touch && x==nx && y==ny);
  int sx,sy;landscape(x,y,sx,sy);assert(sx==ny&&sy==239-nx&&sx>=0&&sx<320&&sy>=0&&sy<240);
 }
 displayctl::State s;s.tab=1;s.sensitivity=-1;
 for(auto wh:{std::pair<int,int>{320,240},{480,320}}){auto h=displayctl::hits(wh.first,wh.second,s,false);
  for(int i=0;i<h.count;i++)assert(h.items[i].action!=uieng::Action::DisplayControl || h.items[i].payload!=16);
 }
 puts("WS28 touch: short/invalid packets, release, full coordinate grid, rotation, unsupported sensitivity PASS");
}
