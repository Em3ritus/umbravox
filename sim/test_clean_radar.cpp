#include "scan_fixture.h"
#include <fstream>
#include <iterator>
#include <cassert>
#include <vector>
#include <string>
using namespace scanr;
struct Fb:Sink{int w,h;std::vector<uint16_t> p;Fb(int W,int H):w(W),h(H),p(W*H){}void strip(int x,int y,int W,int H,const uint16_t* b)override{assert(x==0&&y>=0&&y+H<=h&&W==w);std::copy_n(b,W*H,p.data()+y*w);}};
int main(){std::ifstream file("../packs/slick/theme.json");std::string j((std::istreambuf_iterator<char>(file)),{});themepack::Pack p{};char why[96];assert(themepack::parse(j.data(),j.size(),p,why,sizeof why));assert(p.centre==themepack::CENTRE_RADAR&&p.diamond);assert(!themepack::sanitizeStatus(p));
 auto bad=j;auto pos=bad.find("\"badge\": \"diamond\"");assert(pos!=std::string::npos);bad.replace(pos,18,"\"badge\": \"invalid\"");themepack::Pack q{};assert(!themepack::parse(bad.data(),bad.size(),q,why,sizeof why));
 // Style is data-driven, not keyed to the SLICK name.
 snprintf(p.name,sizeof p.name,"CUSTOM RADAR");int id=pal::addPack(p,nullptr);assert(id>=0&&pal::centrepiece(id)==pal::CP_RADAR&&pal::diamondBadge(id));assert(!pal::diamondBadge(0)&&pal::centrepiece(0)==pal::CP_HEPTAGRAM);pal::setTheme(id);
 Compositor comp;for(auto wh:{std::pair<int,int>{480,320},{320,240}}){int w=wh.first,h=wh.second;auto sc=scanfix::demo(id);sc.animateSweep=true;sc.now=1000;Fb first(w,h),full(w,h),partial(w,h);comp.renderFull(w,h,sc,first);for(int phase=0;phase<72;phase++){partial.p=first.p;sc.now=3600+phase*50;comp.renderFull(w,h,sc,full);comp.renderRegion(w,h,sc,radarBounds(w,h),partial);assert(full.p==partial.p);}assert(first.p!=full.p);
 auto hs=heroSub(uieng::scan(w,h).hero);assert(hs.spectrum.w<=168&&hs.barMax==16);
 }
 // Guarded strip rendering across every sweep angle and portrait-size radii.
 for(int r:{22,69,102})for(unsigned phase=0;phase<72;phase++){const int w=320,h=480;std::vector<uint16_t> guard(w*16+2,0xABCD);for(int y=0;y<h;y+=16){S strip{guard.data()+1,w,y,16};radarTrail(strip,160,260,r,phase);assert(guard.front()==0xABCD&&guard.back()==0xABCD);}}
 puts("clean radar: parser, reusable style, phase repaint, clipping and compact spectrum PASS");}
