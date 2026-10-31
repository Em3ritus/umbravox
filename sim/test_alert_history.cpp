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
 pal::setTheme(0);
 for(int w:{480,320}){int h=w==480?320:240;
  for(int state=0;state<3;state++){
   alertlog::clear();s=ui::State{};s.set.alertMin=state==1?ui::CONF_OFF:ui::CONF_MED;
   if(state==2)alertlog::pushTest(1000);
   alertlog::fill(s,0,2000);omnpres::OmenStore store;uiproj::Chrome c{};uiproj::omens(store,s,c);
   assert(store.scene().level==s.set.alertMin);
   if(state==2)assert(store.scene().rows[0].test && !strcmp(store.scene().rows[0].omen,"TEST ALERT") && !strcmp(store.scene().rows[0].source,"SIMULATED - NO RF"));
   auto hits=uieng::omensHits(w,h);int tests=0;for(int i=0;i<hits.count;i++)tests+=hits.items[i].action==uieng::Action::AlertTest;assert(tests==1);
   std::vector<uint16_t> pixels(w*h);for(int y=0;y<h;y+=16){scanr::S strip{pixels.data()+y*w,w,y,std::min(16,h-y)};omnr::composeStrip(strip,w,h,store.scene());}
   if(argc==2){std::string f=std::string(argv[1])+"/alerts-"+std::to_string(state)+"-"+std::to_string(w)+".png";assert(png::write(f.c_str(),pixels.data(),w,h,1));}
  }
 }
 omnpres::OmenStore a,b;a.begin(0,0);b.begin(0,0);b.level(ui::CONF_OFF);bool status=false,body=false;omnpres::sceneDirty(a.scene(),b.scene(),status,body);assert(body&&!status);
 puts("alert history: synthetic identity, isolation, repeat, clear, level projection and renders PASS");
}
