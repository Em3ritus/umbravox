#pragma once
#include "display_controls.h"
#include "nav.h"
#include "scan_render.h"
#include "sub_render.h"
namespace settingsctl {
struct State { int tab=0, editor=-1; };
struct Data {
 State view; ui::Settings settings; bool connected=false,streaming=false;
 int32_t pin=-1; // pairing code to show (0..999999) while a phone is pairing; -1 = none
 int phones=-1;   // bonded phones, -1 = not known yet
 bool paused=false;        // active discovery is saved ON but held off while recording
 bool forgetArmed=false;   // first tap of the two-tap "forget phones" confirm is waiting
 uint8_t netId=0; char name[24]="NOT STARTED", pcap[24]="OFF";
};
constexpr int FORGET_PHONES=40;          // SettingsControl payload of the PAIRED PHONES card
constexpr uint32_t ARM_MS=5000;          // second tap must land within this
// Two-tap confirm for a destructive action. tap() returns true when this tap confirms it.
struct ForgetArm {
 bool on=false;uint32_t at=0;
 bool armed(uint32_t now)const{return on&&(int32_t)(now-at)<(int32_t)ARM_MS;}
 bool tap(uint32_t now){if(armed(now)){on=false;return true;}on=true;at=now;return false;}
};
inline bool validEditor(int id){return id>=0&&id<16&&id!=10&&id!=11&&id!=14&&id!=15;}
inline int knob(int id){static constexpr int map[]={12,1,2,3,6,13,0,14,8,9,-1,-1,10,11,-1,-1};return id>=0&&id<16?map[id]:-1;}
inline displayctl::Layout layout(int w,int h){return displayctl::layout(w,h);}
inline uieng::Rect tabRect(int w,int i){return {i*w/4,44,(i+1)*w/4-i*w/4,44};}
inline uieng::HitList hits(int w,int h,const State& v){
 using namespace uieng;HitList out;addChromeHits(out,w,h);auto l=layout(w,h);
 if(validEditor(v.editor)){
  out.add("setting-minus",l.minus,Action::SettingsControl,30,nullptr,"Decrease");
  out.add("setting-plus",l.plus,Action::SettingsControl,31,nullptr,"Increase");
  out.add("setting-default",l.reset,Action::SettingsControl,32,nullptr,"Default for this setting");
  out.add("setting-done",l.done,Action::SettingsControl,33,nullptr,"Return to category");return out;
 }
 static const char* tabs[]={"settings-scanning","settings-recording","settings-bridge","settings-nodes"};
 for(int i=0;i<4;i++)out.add(tabs[i],tabRect(w,i),Action::SettingsControl,i,nullptr,"Settings category");
 static const char* cards[]={"setting-card0","setting-card1","setting-card2","setting-card3"};
 const int tab=std::max(0,std::min(3,v.tab));
 for(int i=0;i<4;i++){
  const int id=tab*4+i;
  if(id==14)out.add(cards[i],l.cards[i],Action::Nav,int(ScreenId::Mesh),nullptr,"Open nodes");
  else if(id==11)out.add(cards[i],l.cards[i],Action::SettingsControl,FORGET_PHONES,nullptr,"Forget paired phones");
  else if(validEditor(id))out.add(cards[i],l.cards[i],Action::SettingsControl,10+id,nullptr,"Open setting editor");
 }return out;
}
// Opening a card is navigation only. Changes reuse the production setting path.
inline void apply(State& v,ui::Settings& s,int p){
 if(p>=0&&p<4){v.tab=p;v.editor=-1;return;}
 if(p>=10&&p<=25&&validEditor(p-10)){v.editor=p-10;return;}
 if(p==33){v.editor=-1;return;}
 if(!validEditor(v.editor))return;
 if(p==30||p==31){ui::applyKnobStep(s,knob(v.editor),p==30?-1:1);return;}
 if(p!=32)return;
 const ui::Settings d;
 switch(v.editor){
 case 0:s.activeDiscovery=d.activeDiscovery;break;case 1:s.dwellMs=d.dwellMs;break;
 case 2:s.bleDuty=d.bleDuty;break;case 3:s.bands=d.bands;break;
 case 4:s.autoLog=d.autoLog;break;case 5:s.pcapEnabled=d.pcapEnabled;break;
 case 6:s.alertMin=d.alertMin;break;case 7:s.sound=d.sound;break;case 8:s.bleBridge=d.bleBridge;break;
 case 9:s.bridgeApp=d.bridgeApp;if(s.bleBridge)s.bleBridge=s.bridgeApp;break;
 case 12:s.meshProfile=d.meshProfile;break;case 13:s.nodeDwellMs=d.nodeDwellMs;break;
 }
}
inline const char* title(int id){static const char* t[]={"DISCOVERY","CHANNEL DWELL","BLE SCAN DUTY","WIFI BANDS","AUTO RECORD","RAW PCAP","ALERT LEVEL","ALERT SOUND","BLE BRIDGE","PHONE APP","LINK STATUS","PAIRED PHONES","SCAN PROFILE","NODE DWELL","NODES","NETWORK"};return t[id];}
inline const char* hint(int id){static const char* t[]={"Active sends discovery probes","Time on each WiFi channel","Requested BLE scan window","Coordinator radio bands","Record after a stable GPS fix","Raw WiFi frames to SD","On-screen warning threshold","Beep volume (0 is silent)","Advertise to your phone","Changing app disconnects phone","Current phone connection","Tap twice to forget them","Coordinator node policy","Requested remote-node dwell","Status, enrollment, controls","Current node network ID"};return t[id];}
inline void value(const Data& d,int id,char* b,int n){const auto& s=d.settings;
 switch(id){
 case 0:snprintf(b,n,"%s",s.activeDiscovery?(d.paused?"PAUSED REC":"ACTIVE"):"PASSIVE");break;
 case 1:snprintf(b,n,"%u MS",s.dwellMs);break;case 2:snprintf(b,n,"%u%%",s.bleDuty);break;
 case 3:snprintf(b,n,"%s",s.bands==1?"2.4 GHZ":s.bands==2?"5 GHZ":"2.4 + 5 GHZ");break;
 case 4:snprintf(b,n,"%s",s.autoLog?"ON":"OFF");break;case 5:snprintf(b,n,"%s",d.pcap);break;
 case 6:snprintf(b,n,"%s",(s.alertMin==0?"LOW":s.alertMin==1?"MEDIUM":s.alertMin==2?"HIGH":"OFF"));break;case 7:snprintf(b,n,"%s",s.sound==0?"OFF":s.sound==1?"QUIET":s.sound==2?"NORMAL":"LOUD");break;
 case 8:snprintf(b,n,"%s",s.bleBridge?"ON":"OFF");break;
 case 9:snprintf(b,n,"%s",s.bridgeApp==2?"BISCUIT":"WARDRIVEGO");break;
 case 10:if(s.bleBridge&&d.pin>=0)snprintf(b,n,"PIN %06ld",(long)(d.pin>999999?999999:d.pin));else snprintf(b,n,"%s",!s.bleBridge?"OFF":d.streaming?"STREAMING":d.connected?"CONNECTED":"NO PHONE");break;
 case 11:if(d.forgetArmed)snprintf(b,n,"TAP AGAIN");else if(d.phones<0)snprintf(b,n,"UNKNOWN");else if(d.phones==0)snprintf(b,n,"NONE");else if(d.phones==1)snprintf(b,n,"1 PHONE");else snprintf(b,n,"%d PHONES",d.phones>99?99:d.phones);break;
 case 12:snprintf(b,n,"%s",s.meshProfile?"DISCOVERY":"TRACKING");break;
 case 13:snprintf(b,n,"%u MS",s.nodeDwellMs);break;case 14:snprintf(b,n,"OPEN NODES");break;
 case 15:if(d.netId)snprintf(b,n,"%u / PRIVATE",d.netId);else snprintf(b,n,"0 / OPEN");break;
 }
}
inline bool sameSettings(const ui::Settings& a,const ui::Settings& b){return a.activeDiscovery==b.activeDiscovery&&a.dwellMs==b.dwellMs&&a.bleDuty==b.bleDuty&&a.bands==b.bands&&a.autoLog==b.autoLog&&a.pcapEnabled==b.pcapEnabled&&a.alertMin==b.alertMin&&a.sound==b.sound&&a.bleBridge==b.bleBridge&&a.bridgeApp==b.bridgeApp&&a.meshProfile==b.meshProfile&&a.nodeDwellMs==b.nodeDwellMs;}
inline bool same(const Data& a,const Data& b){return a.view.tab==b.view.tab&&a.view.editor==b.view.editor&&sameSettings(a.settings,b.settings)&&a.connected==b.connected&&a.paused==b.paused&&a.streaming==b.streaming&&a.pin==b.pin&&a.phones==b.phones&&a.forgetArmed==b.forgetArmed&&a.netId==b.netId&&!strcmp(a.name,b.name)&&!strcmp(a.pcap,b.pcap);}
inline void draw(scanr::S& s,int w,int h,const Data& d){
 using namespace scanr;const auto l=layout(w,h);const int ed=d.view.editor;
 if(validEditor(ed)){
  char v[32];value(d,ed,v,sizeof v);centered(s,w/2,56,title(ed),1,pal::DIM);
  centered(s,w/2,85,v,fitScale(v,w-24,3),pal::BRIGHT);
  centered(s,w/2,119,hint(ed),fitScale(hint(ed),w-16,1),pal::DIM);
  if(h>=320)centered(s,w/2,148,"Changes apply live and save automatically",1,pal::DIM);
  subr::stepBtn(s,l.minus,"-",true);subr::stepBtn(s,l.plus,"+",true);
  for(int i=0;i<2;i++){auto r=i?l.done:l.reset;frame(s,r.x,r.y,r.w,r.h,pal::EDGE_LIT);centered(s,r.x+r.w/2,r.y+17,i?"DONE":"DEFAULT",1,pal::BRIGHT);}return;
 }
 const char* tabs[]={"SCANNING","RECORDING","BRIDGE","NODES"};const int tab=std::max(0,std::min(3,d.view.tab));
 for(int i=0;i<4;i++){auto r=tabRect(w,i);box(s,r.x,r.y,r.w,r.h,pal::PANEL);centered(s,r.x+r.w/2,61,tabs[i],1,i==tab?pal::BRIGHT:pal::DIM);if(i==tab)box(s,r.x+8,84,r.w-16,2,pal::WITCH_HI);}
 for(int i=0;i<4;i++){auto r=l.cards[i];int id=tab*4+i;bool live=validEditor(id)||id==7||id==11||id==14;char v[32];value(d,id,v,sizeof v);
  box(s,r.x,r.y,r.w,r.h,pal::PANEL);frame(s,r.x,r.y,r.w,r.h,pal::EDGE);txt(s,r.x+8,r.y+7,title(id),1,pal::DIM);
  txt(s,r.x+8,r.y+(r.h>=70?28:24),v,fitScale(v,r.w-16,2),(id==10&&d.settings.bleBridge&&d.pin>=0)||(id==11&&d.forgetArmed)?pal::AMBER:!live?pal::DIM:id==0&&d.settings.activeDiscovery&&!d.paused?pal::AMBER:id==8&&d.settings.bleBridge?pal::GREEN:pal::BRIGHT);
  if(r.h>=70){const char* hn=(id==8&&d.settings.bleBridge&&d.name[0])?d.name:hint(id);txt(s,r.x+8,r.y+r.h-21,hn,fitScale(hn,r.w-16,1),pal::DIM);}
  if(live)box(s,r.x+8,r.y+r.h-3,r.w-16,1,pal::WITCH_HI);
 }
}
}
