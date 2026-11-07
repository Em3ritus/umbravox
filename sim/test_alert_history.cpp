#include "../device/alertlog.h"
#include "../ui/project.h"
#include "png_writer.h"
#include <cassert>
#include <vector>
#include <string>
int main(int argc,char**argv){
 alertlog::clear();ui::State s;assert(alertlog::count()==0);
 alertlog::pushTest(1000);alertlog::fill(s,0,2000);
 assert(s.nOmens==1 && s.omens[0].test && s.omens[0].threat==ui::TH_NONE);
 assert(!s.omens[0].located && s.omens[0].epoch==0 && s.omens[0].agoS==1);
 alertlog::pushTest(2000);assert(alertlog::count()==1);
 ui::AlertRec real;real.threat=ui::TH_CAMERA;real.conf=ui::CONF_HIGH;snprintf(real.name,sizeof real.name,"real candidate");
 alertlog::push(real,2100,100,true,12,34);alertlog::fill(s,0,2200);
 assert(s.nOmens==2 && !s.omens[0].test && s.omens[1].test);
 alertlog::pushTest(2300);assert(alertlog::count()==2);alertlog::fill(s,0,2300);assert(s.omens[0].test && !s.omens[1].test);
 alertlog::clear();alertlog::fill(s,0,2400);assert(s.nOmens==0);
 for(int i=0;i<60;i++){snprintf(real.name,sizeof real.name,"candidate %d",i);alertlog::push(real,100000+i,0,false,0,0);}
 assert(alertlog::count()==48);alertlog::pushTest(200000);assert(alertlog::count()==48);
 for(int i=0;i<5;i++){snprintf(real.name,sizeof real.name,"later %d",i);alertlog::push(real,200001+i,0,false,0,0);}
 alertlog::pushTest(200010);alertlog::fill(s,0,200010);assert(s.omens[0].test && !strcmp(s.omens[1].name,"later 4") && alertlog::count()==48);
 int synthetic=0;for(int i=0;i<48;i+=6){alertlog::fill(s,i,200010);for(int j=0;j<s.nOmens;j++)synthetic+=s.omens[j].test;}assert(synthetic==1);
 alertlog::clear();
 // History retains an owned reason and stable identity; same names cannot merge distinct MACs.
 real.address=ui::AlertAddress::WifiBssid;real.mac[0]=0xB4;real.mac[1]=0x1E;real.mac[2]=0x52;
 char reason[]="OUI";real.why=reason;alertlog::push(real,300000,0,false,0,0);
 alertlog::fill(s,0,300001);const auto first=s.omens[0].id;reason[0]='X';
 ui::Omen found;assert(alertlog::find(first,300002,found)&&!strcmp(found.why,"OUI"));
 real.mac[5]=1;alertlog::push(real,300003,0,false,0,0);assert(alertlog::count()==2);
 alertlog::fill(s,0,300004);assert(s.omens[0].id!=first);
 omnpres::OmenStore detail;detail.begin(0,0);detail.detail(found,0);
 assert(strstr(detail.scene().detail,"B4:1E:52:00:00:00")&&strstr(detail.scene().detail,"Flock Safety")&&strstr(detail.scene().detail,"B41E52/24"));
 found.address=ui::AlertAddress::BleRandom;detail.detail(found,0);assert(!strstr(detail.scene().detail,"Flock Safety")&&strstr(detail.scene().detail,"RANDOM ADDRESS"));
 alertlog::clear();assert(!alertlog::find(first,300005,found));
 pal::setTheme(0);
 for(int w:{480,320}){int h=w==480?320:240;
  for(int state=0;state<4;state++){
   alertlog::clear();s=ui::State{};s.set.alertMin=state==1?ui::CONF_OFF:ui::CONF_MED;
   if(state==2)alertlog::pushTest(1000);
   if(state==3){real.why="OUI";snprintf(real.name,sizeof real.name,"candidate device");alertlog::push(real,1000,0,false,0,0);}
   alertlog::fill(s,0,2000);omnpres::OmenStore store;uiproj::Chrome c{};uiproj::omens(store,s,c);
   assert(store.scene().level==s.set.alertMin);
   if(state==2)assert(store.scene().rows[0].test && !strcmp(store.scene().rows[0].omen,"TEST ALERT") && !strcmp(store.scene().rows[0].source,"SIMULATED - NO RF"));
   auto hits=uieng::omensHits(w,h);int tests=0;for(int i=0;i<hits.count;i++)tests+=hits.items[i].action==uieng::Action::AlertTest;assert(tests==1);
   std::vector<uint16_t> pixels(w*h);for(int y=0;y<h;y+=16){scanr::S strip{pixels.data()+y*w,w,y,std::min(16,h-y)};omnr::composeStrip(strip,w,h,store.scene());}
   if(argc==2){std::string f=std::string(argv[1])+"/alerts-"+std::to_string(state)+"-"+std::to_string(w)+".png";assert(png::write(f.c_str(),pixels.data(),w,h,1));}
  }
 }
 for(int w:{480,320}){int h=w==480?320:240;
  ui::Omen o{};o.threat=ui::TH_CAMERA;o.conf=ui::CONF_LOW;o.address=ui::AlertAddress::WifiBssid;o.mac[0]=0xB4;o.mac[1]=0x1E;o.mac[2]=0x52;snprintf(o.why,sizeof o.why,"OUI");
  omnpres::OmenStore store;store.begin(0,0);store.detail(o,0);int pages=omnr::detailPageCount(w,h,store.scene().detail);assert(pages>=1);
  const char* keys[]={"123","456","789"};auto hits=uieng::omensHits(w,h,keys,3);int rows=0;for(int i=0;i<hits.count;i++)if(hits.items[i].action==uieng::Action::AlertOpen){assert(hits.items[i].rect.h>=44);assert(!strcmp(hits.items[i].key,keys[rows++]));}assert(rows==uieng::omens(w,h).nRows);
  for(int i=0;i<hits.count;i++)for(int j=i+1;j<hits.count;j++)assert(strcmp(hits.items[i].slot,hits.items[j].slot));
  auto dh=uieng::omensHits(w,h,nullptr,0,true);for(int i=0;i<dh.count;i++)assert(dh.items[i].action!=uieng::Action::AlertTest && dh.items[i].action!=uieng::Action::AlertOpen);
  for(int page=0;page<pages;page++){store.detail(o,page);std::vector<uint16_t> pixels(w*h);for(int y=0;y<h;y+=16){scanr::S strip{pixels.data()+y*w,w,y,std::min(16,h-y)};omnr::composeStrip(strip,w,h,store.scene());}
   if(argc==2){auto f=std::string(argv[1])+"/alert-detail-"+std::to_string(w)+"-"+std::to_string(page)+".png";assert(png::write(f.c_str(),pixels.data(),w,h,1));}}
 }
 omnpres::OmenStore a,b;a.begin(0,0);b.begin(0,0);b.level(ui::CONF_OFF);bool status=false,body=false;omnpres::sceneDirty(a.scene(),b.scene(),status,body);assert(body&&!status);
 puts("alert history: synthetic identity, isolation, repeat, clear, level projection and renders PASS");
}
