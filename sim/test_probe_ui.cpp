#include "../ui/project.h"
#include <cassert>
#include <string>
#include <vector>
#include <cstdio>
struct FB:scanr::Sink{
 int w,h;bool oob=false;std::vector<uint16_t> px;
 FB(int W,int H):w(W),h(H),px(W*H){}
 void strip(int x,int y,int W,int H,const uint16_t* p)override{for(int j=0;j<H;j++)for(int i=0;i<W;i++){if(x+i<0||x+i>=w||y+j<0||y+j>=h)oob=true;else px[(y+j)*w+x+i]=p[j*W+i];}}
};
int main(){
 pal::setTheme(0);ui::State s;s.echoTotal=8;s.echoClients=2;s.nEchoes=2;
 auto& e=s.echoes[0];snprintf(e.ssid,sizeof e.ssid,"12345678901234567890123456789012");e.mac[0]=0x22;e.mac[5]=0x55;e.randomised=true;e.observations=31;e.rssi=-52;e.chan=48;e.linked=true;e.ap[0]=0x10;e.ap[5]=0x44;
 s.echoes[1]=e;s.echoes[1].wildcard=true;s.echoes[1].linked=false;
 uiproj::Chrome c;echpres::EchoStore a,b;uiproj::echoes(a,s,c);
 assert(strlen(a.scene().rows[0].seeking)==32&&strlen(a.scene().rows[0].who)==17);
 assert(a.scene().rows[0].count==31&&a.scene().rows[0].channel==48&&a.scene().rows[0].rssi==-52);
 assert(std::string(a.scene().rows[0].link)=="SEEN WITH AP 10:00:00:00:00:44");assert(a.scene().rows[1].wildcard&&!a.scene().rows[1].link[0]);
 s.echoDrops=2;s.scroll=2;uiproj::echoes(b,s,c);bool status,body;echpres::sceneDirty(a.scene(),b.scene(),status,body);assert(body&&!status);
 echr::Compositor comp;
 for(int w:{320,480}){int h=w==480?320:240;auto l=uieng::echoes(w,h);assert(l.rowH>=48&&l.nRows==(w==480?2:1));
  FB before(w,h),after(w,h);comp.renderFull(w,h,a.scene(),before);comp.renderFull(w,h,b.scene(),after);comp.renderRegion(w,h,b.scene(),l.body,before);assert(!before.oob&&before.px==after.px);
 }
 s.detail.valid=true;s.detailPage=1;s.detail.nameEvidence=2;detpres::DetailStore ds;uiproj::detail(ds,s,c);assert(std::string(ds.scene().facts[6])=="SSID RESOLVED: ASSOC EXCHANGE");
 s.detail.nameEvidence=3;uiproj::detail(ds,s,c);assert(std::string(ds.scene().facts[6])=="SSID EVIDENCE CONFLICT");
 puts("Probe UI: full SSID/MAC, wildcard, observed AP link, pagination, repaint and name provenance PASS");
}
