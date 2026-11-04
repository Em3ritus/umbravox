#include "../ui/attune_present.h"
#include "png_writer.h"
#include <cassert>
#include <vector>
#include <string>
using namespace settingsctl;
static bool overlap(uieng::Rect a,uieng::Rect b){return a.x<b.x+b.w&&b.x<a.x+a.w&&a.y<b.y+b.h&&b.y<a.y+a.h;}
static std::vector<uint16_t> render(int w,int h,const atnr::AttuneScene& sc){std::vector<uint16_t> p(w*h);for(int y=0;y<h;y+=16){scanr::S s{p.data()+y*w,w,y,std::min(16,h-y)};atnr::composeStrip(s,w,h,sc);}return p;}
int main(int argc,char**argv){
 pal::setTheme(0);const char* names[]={"scanning","recording","bridge","nodes"};
 { // LINK STATUS row: the pairing code outranks the connection state, and only while the bridge is on.
  Data d;d.settings.bleBridge=1;char b[32];
  value(d,10,b,sizeof b);assert(!strcmp(b,"NO PHONE"));
  d.connected=true;value(d,10,b,sizeof b);assert(!strcmp(b,"CONNECTED"));
  d.pin=4217;value(d,10,b,sizeof b);assert(!strcmp(b,"PIN 004217"));
  d.pin=0;value(d,10,b,sizeof b);assert(!strcmp(b,"PIN 000000"));
  d.pin=-1;d.streaming=true;value(d,10,b,sizeof b);assert(!strcmp(b,"STREAMING"));
  d.pin=123456;d.settings.bleBridge=0;value(d,10,b,sizeof b);assert(!strcmp(b,"OFF"));
 }
 for(int w:{320,480}){int h=w==320?240:320;
  for(int tab=0;tab<4;tab++)for(int ed=-1;ed<16;ed++){
   if(ed>=0&&!validEditor(ed))continue;
   State v{tab,ed};auto hs=hits(w,h,v);int cards=0;
   for(int i=0;i<hs.count;i++){auto r=hs.items[i].rect;assert(r.w>=44&&r.h>=44&&r.x>=0&&r.y>=0&&r.x+r.w<=w&&r.y+r.h<=h);
    for(int j=0;j<i;j++)assert(!overlap(r,hs.items[j].rect));
    assert(hs.items[i].action!=uieng::Action::SettingReset);
    if(hs.items[i].action==uieng::Action::SettingsControl&&hs.items[i].payload>=10&&hs.items[i].payload<=25)cards++;
   }
   if(ed<0)assert(cards==(tab==0?4:tab==1?3:2));
   Data d;d.view=v;d.settings.activeDiscovery=1;d.settings.dwellMs=150;d.settings.bleBridge=1;strcpy(d.name,"Umbravox-C55C");
   atnpres::AttuneStore store;store.begin(0,0);store.header(true,true,false);store.gps({true,3,12,300});store.storage(ui::StorageState::Ready);store.controls(d);auto a=store.scene();auto b=a;b.control.view.editor=ed<0?0:-1;b.control.connected=true;
   bool status=false,body=false;atnpres::sceneDirty(a,b,status,body);assert(body&&!status);
   auto full=render(w,h,b),partial=render(w,h,a);
   for(int y=32;y<h-32;y+=16){scanr::S strip{partial.data()+y*w,w,y,16};atnr::composeStrip(strip,w,h,b);}assert(full==partial);
   if(argc==2&&ed<0){auto p=render(w,h,a);std::string file=std::string(argv[1])+"/"+names[tab]+"-"+std::to_string(w)+".png";assert(png::write(file.c_str(),p.data(),w,h,1));}
   if(argc==2&&tab==0&&ed==9){auto p=render(w,h,a);std::string file=std::string(argv[1])+"/app-editor-"+std::to_string(w)+".png";assert(png::write(file.c_str(),p.data(),w,h,1));}
  }
 }
 for(int id=0;id<16;id++)if(validEditor(id)){
  State v;ui::Settings s,original=s;apply(v,s,10+id);assert(v.editor==id&&ui::settingsEqual(s,original));
  for(int dir:{-1,1}){auto expected=s;ui::applyKnobStep(expected,knob(id),dir);apply(v,s,dir<0?30:31);assert(ui::settingsEqual(s,expected));}
  s.bright=37;s.theme=1;s.idleIdx=1;apply(v,s,32);assert(s.bright==37&&s.theme==1&&s.idleIdx==1);
  auto saved=s;apply(v,s,33);assert(v.editor==-1&&ui::settingsEqual(s,saved));
 }
 State v;ui::Settings s;for(int p:{20,21,25,100,-1}){apply(v,s,p);assert(v.editor==-1);}
 v.editor=13;s.dwellMs=150;s.nodeDwellMs=220;apply(v,s,31);assert(s.dwellMs==150&&s.nodeDwellMs==230);
 v.editor=9;s.bleBridge=1;s.bridgeApp=1;apply(v,s,31);assert(s.bleBridge==2&&s.bridgeApp==2);apply(v,s,32);assert(s.bleBridge==1&&s.bridgeApp==1);
 Data a,b;assert(same(a,b));b.connected=true;assert(!same(a,b));b=a;strcpy(b.name,"Umbravox-1234");assert(!same(a,b));b=a;strcpy(b.pcap,"SD ERROR");assert(!same(a,b));
 puts("settings controls: all categories/editors, geometry, actions, local defaults, live status and repaint PASS");
}
