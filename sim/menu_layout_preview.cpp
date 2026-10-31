// Production native navigation renders. Fixture data only; no device access.
#include "../ui/scan_render.h"
#include "../ui/menu_render.h"
#include "../ui/aspect_render.h"
#include "png_writer.h"
#include <vector>
#include <string>
#include <cassert>
#include <fstream>
using namespace scanr;
int main(int argc,char** argv){
 if(argc!=2)return 2;
 for(int theme:{0,2})for(int w:{480,320}){
  int h=w==480?320:240;pal::setTheme(theme);
  ScanScene sc;sc.theme=theme;sc.wifi=143;sc.g5=32;sc.ble=73;sc.ch="CH 48";sc.chBand="5 GHz";sc.now=1000;
  const FieldBlip contacts[]={{20,30,0,900},{80,65,1,900},{130,70,2,900},{190,40,0,900},{245,85,1,900},{310,50,2,900}};
  const int channels[]={25,55,8,0,20,85,10,0,0,70,10,0,15,5,30,0,10,15,0,80,15};
  sc.blips=contacts;sc.nBlips=6;sc.channels=channels;sc.nChannels=21;
  for(int screen=0;screen<3;screen++){
   std::vector<uint16_t> pixels(w*h);
   for(int y=0;y<h;y+=16){S strip{pixels.data()+y*w,w,y,std::min(16,h-y)};
    sc.sd=ui::StorageState::Ready;sc.gps={true,3,12,300};sc.bridgeOn=true;sc.pmuOk=true;
    if(screen==1){menr::MenuScene m;m.theme=theme;m.sd=sc.sd;m.plain=true;m.gps=sc.gps;m.bridgeOn=true;m.pmuOk=true;menr::composeStrip(strip,w,h,m);}
    else if(screen==2){aspr::AspectScene a;a.theme=theme;a.sd=sc.sd;a.plain=true;a.gps=sc.gps;a.bridgeOn=true;a.pmuOk=true;a.themeName=pal::themeName(theme);a.sweepName="RADAR";a.alertName=theme==2?"VORTEX":"WARD";a.alertLive=theme==2;aspr::composeStrip(strip,w,h,a);}
    else composeStrip(strip,w,h,sc);
   }
   std::string name=std::string(argv[1])+"/"+(screen==2?"display":screen?"menu":"scan")+"-"+std::to_string(w)+(theme?"-muffin":"")+".png";
   if(!png::write(name.c_str(),pixels.data(),w,h,1))return 1;
  }
 }
}
