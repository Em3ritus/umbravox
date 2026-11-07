#include "../ui/project.h"
#include "png_writer.h"
#include "scan_fixture.h"
#include <vector>
#include <string>
int main(int argc,char** argv){
 if(argc!=2)return 2;
 pal::setTheme(0);
 for(int w:{480,320}){
  const int h=w==480?320:240;
  ui::State s;s.set.bleBridge=1;s.set.bridgeApp=1;s.nets24=81;s.nets5=32;s.ble=73;s.chan=48;s.on5=true;uiproj::Chrome c;c.gps={true,3,12,300};c.rec=scanr::Rec::Off;
  atnpres::AttuneStore store;const int per=uieng::attuneRowsPerPage(h);
  uiproj::attune(store,s,c,8/per,per);
  std::vector<uint16_t> pixels(w*h);
  for(int y=0;y<h;y+=16){scanr::S strip{pixels.data()+y*w,w,y,std::min(16,h-y)};atnr::composeStrip(strip,w,h,store.scene());}
  std::string file=std::string(argv[1])+"/bridge-"+std::to_string(w)+".png";
  if(!png::write(file.c_str(),pixels.data(),w,h,1))return 1;
  for(int enabled:{0,1}){
   s.set.bleBridge=enabled;
   scanpres::SceneStore scan;uiproj::scan(scan,s,c);
   for(int y=0;y<h;y+=16){scanr::S strip{pixels.data()+y*w,w,y,std::min(16,h-y)};scanr::composeStrip(strip,w,h,scan.scene());}
   file=std::string(argv[1])+"/scan-bridge-"+(enabled?"on-":"off-")+std::to_string(w)+".png";
   if(!png::write(file.c_str(),pixels.data(),w,h,1))return 1;
  }

  for(int theme=0;theme<3;++theme){
   pal::setTheme(theme);auto sc=scanfix::demo(theme);sc.bridgeOn=true;sc.pmuOk=true;sc.hasCell=true;sc.battPct=78;sc.charging=true;
   for(int y=0;y<h;y+=16){scanr::S strip{pixels.data()+y*w,w,y,std::min(16,h-y)};scanr::composeStrip(strip,w,h,sc);}
   file=std::string(argv[1])+"/header-theme-"+std::to_string(theme)+"-"+std::to_string(w)+".png";
   if(!png::write(file.c_str(),pixels.data(),w,h,1))return 1;
  }
  pal::setTheme(0);
 }
}
