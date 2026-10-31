// Synthetic node diagnostics previews at both native landscape sizes.
#include "../ui/mesh_render.h"
#include "png_writer.h"
#include <vector>
struct Frame:mshr::Sink{int w;std::vector<uint16_t> pixels;Frame(int W,int H):w(W),pixels(W*H){}void strip(int x,int y,int W,int H,const uint16_t* p)override{for(int row=0;row<H;row++)std::copy_n(p+row*W,W,pixels.data()+(y+row)*w+x);}};
int main(){pal::setTheme(pal::TH_SKULD);mshr::MeshNode node{{0x00,0x00,0x01},2,3,true};auto& d=node.diag;
 d.known=true;d.active=false;d.profile=1;d.dwellMs=220;d.ageS=2;d.hasReport=true;d.reportAgeS=4;
 d.assignedCount=10;const uint8_t chans[]={1,3,5,7,9,11,36,44,149,157};std::copy_n(chans,10,d.assignedChannels);
 d.depth=12;d.capacity=192;d.peak=48;d.drops=3;d.sendFailed=2;d.reportRejected=1;
 for(int w:{320,480}){int h=w==320?240:320;for(int page:{1,2}){mshr::MeshScene s;s.theme=pal::TH_SKULD;s.rows=&node;s.nRows=1;s.nodes=1;s.page=page;s.targetDwell=150;s.targetProfile=1;s.sd=ui::StorageState::Ready;
  mshr::Compositor c;Frame f(w,h);c.renderFull(w,h,s,f);char name[80];snprintf(name,sizeof name,"nodes-%d-%s.png",w,page==1?"radio":"link");if(!png::write(name,f.pixels.data(),w,h,1))return 1;
 }}
}
