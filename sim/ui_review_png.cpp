// Native production compositor previews. Fixtures only; no device access.
#include "../ui/project.h"
#include "png_writer.h"
#include <vector>
#include <string>
int main(int argc,char** argv) {
 if(argc!=2)return 2;
 pal::setTheme(0);
 for(int w:{480,320}) {
  int h=w==480?320:240;
  ui::State s;s.nSights=3;s.totalSights=115;s.plotTotal=115;s.plotBeyond=40;s.nPlots=5;s.plotRadius=42;
  const char* names[]={"KATY AND HANNAH WIFI - GUEST 12345","LAFAYETTE_LN",""};
  for(int i=0;i<3;++i){snprintf(s.sights[i].ssid,sizeof s.sights[i].ssid,"%s",names[i]);
   uint8_t mac[]={0xA0,0xB1,0xC2,0xD3,0xE4,(uint8_t)(0xF0+i)};
   std::copy_n(mac,6,s.sights[i].mac);s.sights[i].band=i%2;s.sights[i].rssi=-43-i*9;s.sights[i].chan=i%2?48:6;}
  s.plots[0]={42,0,0,0};s.plots[1]={0,-42,1,0};s.plots[2]={-21,0,0,0};s.plots[3]={0,30,1,0};s.plots[4]={12,-12,0,0};
  s.detail.valid=true;snprintf(s.detail.ssid,sizeof s.detail.ssid,"%s",names[0]);std::copy_n(s.sights[0].mac,6,s.detail.mac);
  s.detail.band=0;s.detail.chan=6;s.detail.rssi=-43;s.detail.auth=ui::AUTH_WPA2|ui::AUTH_WPA3|ui::AUTH_PSK|ui::AUTH_SAE|ui::AUTH_CCMP|ui::AUTH_ESS;
  s.detail.located=true;s.detail.lat7=418812345;s.detail.lon7=-876321234;s.detail.bestRssi=-38;s.detail.firstAgoS=243;s.detail.lastAgoS=2;
  s.detail.histN=28;for(int i=0;i<28;++i)s.detail.hist[i]=-82+i+(i%5);
  s.set.labels=1;
  uiproj::Chrome c;c.gps={true,3,12,300};c.rec=scanr::Rec::Off;
  dompres::DomainStore d;codpres::CodexStore x;detpres::DetailStore a;menpres::MenuStore m;
  uiproj::domain(d,s,c,w,h);uiproj::codex(x,s,c,w,h);uiproj::detail(a,s,c);uiproj::menu(m,s,c);
  std::vector<uint16_t> sheet(4*w*h),pixels(w*h);
  const char* tags[]={"domain","detail","codex","menu"};
  for(int tile=0;tile<4;++tile) {
   for(int y=0;y<h;y+=16){scanr::S strip{pixels.data()+y*w,w,y,std::min(16,h-y)};
    switch(tile){case 0:domr::composeStrip(strip,w,h,d.scene());break;case 1:detr::composeStrip(strip,w,h,a.scene());break;
     case 2:codr::composeStrip(strip,w,h,x.scene());break;case 3:menr::composeStrip(strip,w,h,m.scene());break;}}
   const std::string file=std::string(argv[1])+"/"+tags[tile]+"-"+std::to_string(w)+".png";
   if(!png::write(file.c_str(),pixels.data(),w,h,1))return 1;
   for(int y=0;y<h;++y)std::copy_n(pixels.data()+y*w,w,sheet.data()+((tile/2)*h+y)*2*w+(tile%2)*w);
  }
  const std::string file=std::string(argv[1])+"/review-"+std::to_string(w)+".png";
  if(!png::write(file.c_str(),sheet.data(),2*w,2*h,1))return 1;
 }
}
