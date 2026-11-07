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
 { // PAIRED PHONES card (bridge tab, 4th card): count, armed confirm, and a tap target.
  Data d;d.settings.bleBridge=1;char b[32];
  assert(!strcmp(title(11),"PAIRED PHONES"));
  d.phones=-1;value(d,11,b,sizeof b);assert(!strcmp(b,"UNKNOWN"));
  d.phones=0;value(d,11,b,sizeof b);assert(!strcmp(b,"NONE"));
  d.phones=1;value(d,11,b,sizeof b);assert(!strcmp(b,"1 PHONE"));
  d.phones=3;value(d,11,b,sizeof b);assert(!strcmp(b,"3 PHONES"));
  d.forgetArmed=true;value(d,11,b,sizeof b);assert(!strcmp(b,"TAP AGAIN"));      // armed beats the count
  for(int w:{320,480}){State v{2,-1};auto hs=hits(w,w==320?240:320,v);int found=0;
   for(int i=0;i<hs.count;i++)if(hs.items[i].action==uieng::Action::SettingsControl&&hs.items[i].payload==FORGET_PHONES){found++;assert(hs.items[i].rect.w>=44&&hs.items[i].rect.h>=44);}
   assert(found==1);                                                           // exactly one forget target, on the bridge tab
   for(int tab:{0,1,3}){State o{tab,-1};auto oh=hits(w,w==320?240:320,o);for(int i=0;i<oh.count;i++)assert(!(oh.items[i].payload==FORGET_PHONES&&oh.items[i].action==uieng::Action::SettingsControl));}}
  // Two-tap confirm: the first tap arms, a second inside ARM_MS forgets; a late second tap only re-arms.
  ForgetArm a;assert(!a.armed(1000));
  assert(!a.tap(1000)&&a.armed(1000)&&a.armed(1000+ARM_MS-1)&&!a.armed(1000+ARM_MS));
  assert(a.tap(1000+ARM_MS-1)&&!a.armed(1000+ARM_MS-1));                         // confirmed, disarmed
  assert(!a.tap(9000)&&!a.tap(9000+ARM_MS)&&a.armed(9000+ARM_MS));               // too late: re-armed, nothing forgotten
  ForgetArm w2;w2.tap(0xFFFFFFF0u);assert(w2.tap(0x00000010u));                  // across the 32-bit wrap
 }
 { // LINK STATUS row: the pairing code outranks the connection state, and only while the bridge is on.
  Data d;d.settings.bleBridge=1;char b[32];
  value(d,10,b,sizeof b);assert(!strcmp(b,"NO PHONE"));
  d.connected=true;value(d,10,b,sizeof b);assert(!strcmp(b,"CONNECTED"));
  d.pin=4217;value(d,10,b,sizeof b);assert(!strcmp(b,"PIN 004217"));
  d.pin=0;value(d,10,b,sizeof b);assert(!strcmp(b,"PIN 000000"));
  d.pin=-1;d.streaming=true;value(d,10,b,sizeof b);assert(!strcmp(b,"STREAMING"));
  d.pin=123456;d.settings.bleBridge=0;value(d,10,b,sizeof b);assert(!strcmp(b,"OFF"));
 }
 { // ALERT SOUND (recording tab, 4th card): volume 0 OFF .. 3 LOUD, default NORMAL, stepped like every other editor.
  Data d;char b[32];
  assert(validEditor(7)&&knob(7)==14&&!strcmp(title(7),"ALERT SOUND"));
  const char* want[]={"OFF","QUIET","NORMAL","LOUD"};
  for(int k=0;k<4;k++){d.settings.sound=(uint16_t)k;value(d,7,b,sizeof b);assert(!strcmp(b,want[k]));}
  ui::Settings s;assert(s.sound==2);
  ui::applyKnobStep(s,14,1);assert(s.sound==3);ui::applyKnobStep(s,14,1);assert(s.sound==3);   // stops at LOUD
  ui::applyKnobStep(s,14,-1);ui::applyKnobStep(s,14,-1);ui::applyKnobStep(s,14,-1);assert(s.sound==0);ui::applyKnobStep(s,14,-1);assert(s.sound==0);   // and at OFF
  State v;v.editor=7;s.sound=0;apply(v,s,32);assert(s.sound==2);                                // the card's own default
  s.sound=9;ui::clampSettings(s);assert(s.sound==2);                                            // junk from flash
  State t{1,-1};auto hs=hits(480,320,t);int found=0;for(int i=0;i<hs.count;i++)found+=hs.items[i].payload==17&&hs.items[i].action==uieng::Action::SettingsControl;assert(found==1);   // the card opens its editor
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
   if(ed<0)assert(cards==(tab==0?4:tab==1?4:2));
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
