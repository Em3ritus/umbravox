#include "../ui/project.h"
#include <cassert>
#include <vector>
#include <string>
#include <cstdio>
struct FB : scanr::Sink {
 int w,h;bool oob=false;std::vector<uint16_t> pixels;
 FB(int W,int H):w(W),h(H),pixels(W*H){}
 void strip(int x,int y,int W,int H,const uint16_t* p)override{
  for(int j=0;j<H;j++)for(int i=0;i<W;i++)if(x+i<0||x+i>=w||y+j<0||y+j>=h)oob=true;else pixels[(y+j)*w+x+i]=p[j*W+i];
 }
};
int main(){
 pal::setTheme(0);ui::State s;s.detail.valid=true;s.detailPage=2;s.detail.band=ui::BAND_24;s.clients.available=true;s.clients.total=10;s.clients.count=1;
 auto& row=s.clients.rows[0];row.mac[0]=0x22;row.mac[5]=1;row.evidence=2;row.ageS=4;row.frames=7;row.channel=6;
 uiproj::Chrome c;detpres::DetailStore a,b;uiproj::detail(a,s,c);
 assert(a.scene().page==2 && std::string(a.scene().clientLines[0][0]).find("RSSI ?")!=std::string::npos);
 assert(std::string(a.scene().clientLines[0][2]).find("LOCAL MAC / VENDOR UNKNOWN")!=std::string::npos);
 s.clients.rows[0].ageS=5;uiproj::detail(b,s,c);bool status,body;detpres::sceneDirty(a.scene(),b.scene(),status,body);assert(body&&!status);
 detr::Compositor comp;
 for(int w:{320,480}){
  const int h=w==480?320:240;const auto g=uieng::detailClients(w,h);assert(g.nRows==(w==480?2:1));
  for(int i=0;i<g.nRows;i++){const auto r=g.rows[i];assert(r.h>=32&&r.x>=0&&r.y>=0&&r.x+r.w<=w&&r.y+r.h<=g.prev.y);}
  const auto hits=uieng::detailHits(w,h,true),old=uieng::detailHits(w,h,false);assert(hits.count==old.count+2);
  for(int i=0;i<hits.count;i++){
   const auto r=hits.items[i].rect;assert(r.w>=44&&r.h>=44&&r.x>=0&&r.y>=0&&r.x+r.w<=w&&r.y+r.h<=h);
   for(int j=0;j<i;j++){const auto q=hits.items[j].rect;assert(r.x>=q.x+q.w||q.x>=r.x+r.w||r.y>=q.y+q.h||q.y>=r.y+r.h);}
  }
  FB visible(w,h),full(w,h);comp.renderFull(w,h,a.scene(),visible);assert(!visible.oob);
  auto empty=s;empty.clients.total=empty.clients.count=0;detpres::DetailStore gone;uiproj::detail(gone,empty,c);
  comp.renderFull(w,h,gone.scene(),full);comp.renderRegion(w,h,gone.scene(),uieng::detail(w,h).body,visible);assert(visible.pixels==full.pixels&&!visible.oob);
  auto info=s;info.detailPage=1;detpres::DetailStore more;uiproj::detail(more,info,c);
  comp.renderFull(w,h,more.scene(),visible);comp.renderRegion(w,h,a.scene(),uieng::detail(w,h).body,visible);comp.renderFull(w,h,a.scene(),full);assert(visible.pixels==full.pixels);
 }
 puts("Client UI: full MAC, unknown station RSSI, local address, scene changes, paging geometry and repaint equivalence PASS");
}
