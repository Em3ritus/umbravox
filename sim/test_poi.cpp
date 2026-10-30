#include "../device/poi.h"
#include "../device/sdlog.h"
#include "../ui/router.h"
#include "../ui/project.h"
#include <cassert>
#include <string>
#include <cstdio>
namespace {bool card=true,failWrite=false,failClose=false,failRollback=false;int opens=0,closes=0;std::string data;}
namespace sdlog {
bool present(){return card;}
bool openAppend(const char* name,uint32_t& size){assert(std::string(name)=="/poi.csv");++opens;size=data.size();return true;}
bool writeAppend(const char* p,uint32_t n){if(failWrite){data.append(p,n/2);return false;}data.append(p,n);return true;}
bool closeAppend(){++closes;return !failClose;}
bool truncateTo(const char*,uint32_t size){if(failRollback)return false;data.resize(size);return true;}
}
template<class Engine> void footerFeedback(int w,int h){
 Engine engine;engine.reset(w,h);engine.enter();engine.back().begin(0,0);engine.commit();while(engine.pending())engine.nextSlice(100);
 engine.back().begin(0,1);engine.back().poi(ui::PoiStatus::Saved);engine.commit();assert(engine.pending());
 while(engine.pending()){auto r=engine.nextSlice(100);assert(r.y0>=((h-44)/16)*16 && r.y1<=h);}
 engine.back().begin(0,5000);engine.commit();assert(engine.pending());
 while(engine.pending()){auto r=engine.nextSlice(100);assert(r.y0>=((h-44)/16)*16 && r.y1<=h);}
}
int main(){
 using ui::PoiStatus;
 ui::Gnss::Fix f;f.valid=true;f.lat7=-1234567;f.lon7=-876321234;f.atMs=1000;
 assert(!poi::request(f,6000,0,"") && poi::status(6000)==PoiStatus::NoFix && !opens);
 f.atMs=2001;assert(poi::request(f,7000,0,"/session.csv"));assert(!opens);
 f.lat7=123456789; // Pending record owns the fix at the tap.
 assert(!poi::request(f,7100,123,"") && !opens);
 poi::service(7200);assert(poi::status(7200)==PoiStatus::Saved && opens==1 && closes==1);
 assert(data.find(",7000,-0.1234567,-87.6321234,4999,\"/session.csv\"\n")!=std::string::npos);
 assert(poi::status(11200)==PoiStatus::Ready);
 const auto first=data;
 card=false;f.atMs=12000;assert(!poi::request(f,12000,0,"") && poi::status(12000)==PoiStatus::NoCard && data==first);
 card=true;f.atMs=13000;assert(poi::request(f,13000,1793318400,"/a\"b.csv"));poi::service(13001);
 assert(data.find("1793318400,13000,12.3456789,-87.6321234,0,\"/a\"\"b.csv\"")!=std::string::npos);
 assert(data.find("gps_utc_epoch",1)==std::string::npos); // One header only.
 const auto good=data;
 f.atMs=14000;failWrite=true;assert(poi::request(f,14000,0,""));poi::service(14001);
 assert(poi::status(14001)==PoiStatus::WriteError && data==good && closes==3);
 failWrite=false;failClose=true;f.atMs=15000;assert(poi::request(f,15000,0,""));poi::service(15001);
 assert(poi::status(15001)==PoiStatus::WriteError && data==good);
 failClose=false;f.atMs=16000;assert(poi::request(f,16000,0,""));card=false;poi::service(16001);
 assert(poi::status(16001)==PoiStatus::NoCard && data==good);card=true;
 f.valid=false;assert(!poi::request(f,17000,0,"") && poi::status(17000)==PoiStatus::NoFix);f.valid=true;
 for(int w:{480,320}){
  const int height=w==480?320:240;
  footerFeedback<scanpres::Engine>(w,height);footerFeedback<menpres::MenuEngine>(w,height);
  footerFeedback<dompres::DomainEngine>(w,height);footerFeedback<codpres::CodexEngine>(w,height);
  footerFeedback<mshpres::MeshEngine>(w,height);footerFeedback<omnpres::OmenEngine>(w,height);
  footerFeedback<echpres::EchoEngine>(w,height);footerFeedback<atnpres::AttuneEngine>(w,height);
  footerFeedback<asppres::AspectEngine>(w,height);footerFeedback<detpres::DetailEngine>(w,height);
  footerFeedback<flspres::FilesEngine>(w,height);
  const auto h=uieng::menuHits(w,w==480?320:240);uinav::Router router;router.setScreen(uinav::Screen::Menu);
  bool found=false;
  for(int i=0;i<h.count;++i)if(h.items[i].action==uieng::Action::MarkPoi){found=true;assert(router.dispatch(h.items[i]).markPoi && router.screen()==uinav::Screen::Menu);}
  assert(found);
  menr::MenuScene before,after;after.poi=PoiStatus::Saved;bool status,body;menpres::sceneDirty(before,after,status,body);assert(!status&&!body);
 }
 // Failed rollback latches writes off so another mark cannot append to corrupt data.
 failWrite=true;failRollback=true;f.atMs=18000;assert(poi::request(f,18000,0,""));poi::service(18001);
 f.atMs=19000;assert(!poi::request(f,19000,0,""));assert(poi::status(19000)==PoiStatus::WriteError);
 puts("POI: freshness, frozen fix, no-card, CSV escaping, verified close, rollback, one-shot routing PASS");
}
