#pragma once
#include "layout_engine.h"
#include "model.h"
#include <algorithm>
namespace displayctl {
// Transient UI only. Brightness/idle/appearance retain the existing preferences.
struct State {
 int tab=0, editor=-1, sensitivity=12;
 bool error=false, down=false;
 int x=-1,y=-1;
 struct Dot {int16_t x=0,y=0; bool start=false;};
 Dot trail[32]{}; int count=0;
};
struct Layout {uieng::Rect tabs[3],cards[4],minus,plus,done,reset,pad;};
inline Layout layout(int w,int h){
 Layout l;for(int i=0;i<3;i++)l.tabs[i]={i*w/3,44,(i+1)*w/3-i*w/3,44};
 const int cw=(w-22)/2,rh=(h-152)/2;
 for(int i=0;i<4;i++)l.cards[i]={8+(i%2)*(cw+6),94+(i/2)*(rh+6),cw,rh};
 l.minus={8,h-96,64,44};l.plus={80,h-96,64,44};l.reset={152,h-96,88,44};l.done={w-72,h-96,64,44};
 l.pad={8,80,w-16,h-180};return l;
}
inline bool contains(uieng::Rect r,int x,int y){return x>=r.x&&x<r.x+r.w&&y>=r.y&&y<r.y+r.h;}
inline bool same(const State& a,const State& b){
 if(a.tab!=b.tab||a.editor!=b.editor||a.sensitivity!=b.sensitivity||a.error!=b.error||a.down!=b.down||a.x!=b.x||a.y!=b.y||a.count!=b.count)return false;
 for(int i=0;i<a.count;i++)if(a.trail[i].x!=b.trail[i].x||a.trail[i].y!=b.trail[i].y||a.trail[i].start!=b.trail[i].start)return false;
 return true;
}
inline void sample(State& s,int w,int h,bool down,int x,int y){
 const bool was=s.down;s.down=down;if(down){s.x=x;s.y=y;}
 if(s.editor!=4||!down||!contains(layout(w,h).pad,x,y))return;
 if(was&&s.count&&s.trail[s.count-1].x==x&&s.trail[s.count-1].y==y)return;
 if(s.count==32){for(int i=1;i<32;i++)s.trail[i-1]=s.trail[i];--s.count;}
 s.trail[s.count++]={int16_t(x),int16_t(y),!was};
}
// 0..2 tabs; 10..21 cards; 30 minus,31 plus,32 defaults,33 done.
inline uieng::HitList hits(int w,int h,const State& s,bool alertLive){
 using namespace uieng;HitList hits;addChromeHits(hits,w,h);auto l=layout(w,h);
 if(s.editor>=0){
  if(s.editor!=4){hits.add("display-minus",l.minus,Action::DisplayControl,30,nullptr,"Display control");hits.add("display-plus",l.plus,Action::DisplayControl,31,nullptr,"Display control");}
  hits.add("display-reset",l.reset,Action::DisplayControl,32,nullptr,"Display control");hits.add("display-done",l.done,Action::DisplayControl,33,nullptr,"Display control");return hits;
 }
 const char* tabs[]={"display-tab","touch-tab","appearance-tab"};
 for(int i=0;i<3;i++)hits.add(tabs[i],l.tabs[i],Action::DisplayControl,i,nullptr,"Display control");
 const char* cards[]={"control-0","control-1","control-2","control-3"};
 for(int i=0;i<4;i++){
  if((s.tab==0&&i==3)||(s.tab==1&&(i==1||i==3))||(s.tab==2&&i==2&&!alertLive))continue;
  hits.add(cards[i],l.cards[i],Action::DisplayControl,10+s.tab*4+i,nullptr,"Display control");
 }return hits;
}
inline void brightness(ui::Settings& s,int dir){
 static constexpr uint8_t levels[]={1,3,5,10,20,30,40,50,60,70,80,90,100};int i=0;
 while(i<12&&levels[i]<s.bright)++i;s.bright=levels[std::max(0,std::min(12,i+dir))];
}
inline void step(State& v,ui::Settings& s,int dir){
 if(v.editor==0)brightness(s,dir);
 if(v.editor==1)s.idleIdx=(s.idleIdx+ui::IDLE_N+dir)%ui::IDLE_N;
 if(v.editor==2)s.idleMode=(s.idleMode+ui::IDLE_MODE_N+dir)%ui::IDLE_MODE_N;
 if(v.editor==6)v.sensitivity=std::max(8,std::min(30,v.sensitivity+2*dir));
}
}
