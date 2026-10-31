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
  s.detail.mac[0]=0;s.detail.mac[1]=0;s.detail.mac[2]=0x0c;
  s.detail.info.valid=true;s.detail.info.width=40;s.detail.info.phy=5;s.detail.info.beaconTu=100;s.detail.info.capabilities=0x431;s.detail.info.wps=true;s.detail.info.wpsLocked=0;snprintf(s.detail.info.country,3,"US");s.detail.observations=812;
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
  s.detailPage=1;uiproj::detail(a,s,c);
  for(int y=0;y<h;y+=16){scanr::S strip{pixels.data()+y*w,w,y,std::min(16,h-y)};detr::composeStrip(strip,w,h,a.scene());}
  png::write((std::string(argv[1])+"/detail-more-"+std::to_string(w)+".png").c_str(),pixels.data(),w,h,1);
  atnpres::AttuneStore settings;s.set.pcapEnabled=1;snprintf(s.pcapStatus,sizeof s.pcapStatus,"ON");s.pcapDrops=12;
  const int per=uieng::attuneRowsPerPage(h);uiproj::attune(settings,s,c,12/per,per);
  for(int y=0;y<h;y+=16){scanr::S strip{pixels.data()+y*w,w,y,std::min(16,h-y)};atnr::composeStrip(strip,w,h,settings.scene());}
  png::write((std::string(argv[1])+"/pcap-"+std::to_string(w)+".png").c_str(),pixels.data(),w,h,1);
  s.detailPage=2;s.clients.available=true;s.clients.total=7;s.clients.count=uieng::detailClients(w,h).nRows;
  for(int i=0;i<ui::CLIENT_ROWS;i++){
    auto& row=s.clients.rows[i];row.mac[0]=i?0x22:0;row.mac[1]=0;row.mac[2]=0x0c;row.mac[3]=0x34;row.mac[4]=0x56;row.mac[5]=0x78+i;
    row.hasRssi=!i;row.rssi=-51;row.frames=48+i;row.ageS=2+i;row.evidence=i?2:1;row.channel=6;
  }
  for(int state=0;state<3;state++){
    if(state==1){s.clients.count=0;s.clients.total=0;}
    if(state==2)s.clients.available=false;
    detpres::DetailStore cs;uiproj::detail(cs,s,c);
    for(int y=0;y<h;y+=16){scanr::S strip{pixels.data()+y*w,w,y,std::min(16,h-y)};detr::composeStrip(strip,w,h,cs.scene());}
    const char* tag=state==0?"clients":state==1?"clients-empty":"clients-unavailable";
    png::write((std::string(argv[1])+"/"+tag+"-"+std::to_string(w)+".png").c_str(),pixels.data(),w,h,1);
  }
  s.set.bleBridge=1;s.nNodes=1;s.nodes[0].id[0]=0xf3;s.nodes[0].id[1]=0xc5;s.nodes[0].id[2]=0x5c;s.nodes[0].live=true;s.nodes[0].lossPct=2;
  auto& nd=s.nodes[0].diag;nd.known=true;nd.active=true;nd.profile=1;nd.dwellMs=150;nd.capacity=192;nd.peak=51;nd.depth=0;nd.drops=9;nd.sendFailed=2;
  s.localDiag.valid=true;s.localDiag.active=true;s.localDiag.dwellMs=150;s.localDiag.started=145;s.localDiag.completed=144;s.localDiag.bridgeMode=1;s.localDiag.streaming=true;s.localDiag.connected=true;s.localDiag.bridgeQueued=3;s.localDiag.txRetries=2;
  const char* meshTags[]={"mesh-local","mesh-node","mesh-stale","mesh-unknown"};
  for(int state=0;state<4;state++){
   s.meshPage=state?1:0;nd.ageS=state==2?15:1;nd.known=state!=3;
   mshpres::MeshStore mesh;uiproj::mesh(mesh,s,c);
   for(int y=0;y<h;y+=16){scanr::S strip{pixels.data()+y*w,w,y,std::min(16,h-y)};mshr::composeStrip(strip,w,h,mesh.scene());}
   png::write((std::string(argv[1])+"/"+meshTags[state]+"-"+std::to_string(w)+".png").c_str(),pixels.data(),w,h,1);
  }
  const std::string file=std::string(argv[1])+"/review-"+std::to_string(w)+".png";
  if(!png::write(file.c_str(),sheet.data(),2*w,2*h,1))return 1;
 }
}
