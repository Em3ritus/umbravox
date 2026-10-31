#include "../ui/aspect_present.h"
#include <cassert>
#include <vector>
#include <cstdio>
using namespace displayctl;
static bool overlap(uieng::Rect a,uieng::Rect b){return a.x<b.x+b.w&&b.x<a.x+a.w&&a.y<b.y+b.h&&b.y<a.y+a.h;}
static std::vector<uint16_t> draw(int w,int h,const aspr::AspectScene& sc){std::vector<uint16_t> p(w*h);for(int y=0;y<h;y+=16){scanr::S s{p.data()+y*w,w,y,std::min(16,h-y)};aspr::composeStrip(s,w,h,sc);}return p;}
int main(){
 for(int w:{320,480}){int h=w==320?240:320;State v;ui::Settings settings;
  for(int tab=0;tab<3;tab++)for(int editor:{-1,0,1,2,4,6,8,9,10})for(bool live:{false,true}){
   v.tab=tab;v.editor=editor;auto hs=hits(w,h,v,live);
   for(int i=0;i<hs.count;i++){auto r=hs.items[i].rect;assert(r.w>=44&&r.h>=44&&r.x>=0&&r.y>=0&&r.x+r.w<=w&&r.y+r.h<=h);
    for(int j=0;j<i;j++)assert(!overlap(r,hs.items[j].rect));
    if(hs.items[i].action==uieng::Action::DisplayControl){int p=hs.items[i].payload;assert(p!=13&&p!=15&&p!=17);if(!live)assert(p!=20);}
   }
   if(editor==4){auto pad=layout(w,h).pad;for(int i=0;i<hs.count;i++)assert(!overlap(pad,hs.items[i].rect));}
   asppres::AspectStore store;store.begin(0,0);store.appearance("UMBRAVOX","RADAR","WARD",live,0);store.controls(v,settings);auto a=store.scene();auto b=a;
   b.control.error=true;b.control.sensitivity=22;b.brightness=30;b.control.tab=(tab+1)%3;
   auto full=draw(w,h,b),partial=draw(w,h,a);bool status,body;asppres::sceneDirty(a,b,status,body);assert(!status&&body);
   for(int y=32;y<h-32;y+=16){scanr::S strip{partial.data()+y*w,w,y,16};aspr::composeStrip(strip,w,h,b);}assert(full==partial);
  }
  v.editor=0;for(int i=0;i<30;i++)step(v,settings,-1);assert(settings.bright==1);for(int i=0;i<30;i++)step(v,settings,1);assert(settings.bright==100);
  v.editor=1;settings.idleIdx=0;step(v,settings,-1);assert(settings.idleIdx==ui::IDLE_N-1);step(v,settings,1);assert(settings.idleIdx==0);
  v.editor=2;settings.idleMode=0;step(v,settings,-1);assert(settings.idleMode==2);step(v,settings,1);assert(settings.idleMode==0);
  v.editor=6;for(int i=0;i<30;i++)step(v,settings,-1);assert(v.sensitivity==8);for(int i=0;i<30;i++)step(v,settings,1);assert(v.sensitivity==30);
  v.editor=4;v.count=0;auto pad=layout(w,h).pad;for(int i=0;i<100;i++)sample(v,w,h,true,pad.x+2+i,pad.y+4);assert(v.count==32);int last=v.x;sample(v,w,h,false,-1,-1);assert(!v.down&&v.x==last);sample(v,w,h,true,0,0);assert(v.count==32);
 }
 puts("display controls: both sizes, all tabs/editors, hit isolation, repaint, bounds and trail PASS");
}
