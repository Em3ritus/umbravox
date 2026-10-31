#include "scan_fixture.h"
#include <fstream>
#include <iterator>
#include <cassert>
#include <vector>
#include <string>
using namespace scanr;
struct Fb:Sink{int w,h;std::vector<uint16_t> p;Fb(int W,int H):w(W),h(H),p(W*H){}void strip(int x,int y,int W,int H,const uint16_t* b)override{assert(x==0&&y>=0&&y+H<=h&&W==w);std::copy_n(b,W*H,p.data()+y*w);}};
int main(){std::ifstream file("../packs/z-ember/theme.json");std::string j((std::istreambuf_iterator<char>(file)),{});themepack::Pack p{};char why[96];assert(themepack::parse(j.data(),j.size(),p,why,sizeof why));assert(p.centre==themepack::CENTRE_RADAR&&p.diamond);assert(!themepack::sanitizeStatus(p));
 auto bad=j;auto pos=bad.find("\"badge\": \"diamond\"");assert(pos!=std::string::npos);bad.replace(pos,18,"\"badge\": \"invalid\"");themepack::Pack q{};assert(!themepack::parse(bad.data(),bad.size(),q,why,sizeof why));
 // Style is data-driven, not keyed to the SLICK name.
 snprintf(p.name,sizeof p.name,"CUSTOM RADAR");int id=pal::addPack(p,nullptr);assert(id>=0&&pal::centrepiece(id)==pal::CP_RADAR&&pal::diamondBadge(id));assert(!pal::diamondBadge(0)&&pal::centrepiece(0)==pal::CP_HEPTAGRAM);pal::setTheme(id);
 std::ifstream mf("../packs/z-baph0mau5/theme.json");std::string mj((std::istreambuf_iterator<char>(mf)),{});themepack::Pack mp{};assert(themepack::parse(mj.data(),mj.size(),mp,why,sizeof why));assert(mp.centre==themepack::CENTRE_RADAR_EMBLEM);mp.centre=themepack::CENTRE_MASCOT;snprintf(mp.name,sizeof mp.name,"TEST MASCOT");assert(themepack::admit("z-baph0mau5",mp,nullptr,0)==themepack::ADMIT_OK);int mascot=pal::addPack(mp,nullptr);assert(mascot>=0&&pal::centrepiece(mascot)==pal::CP_MASCOT);
 std::vector<uint8_t> artwork[4];themepack::Emblem emblems[4]{};int logos[4];
 const char* folders[]={"../packs/z-skuld/","../packs/slick/","../packs/z-baph0mau5/","../packs/z-em3ritus/"};
 for(int i=0;i<4;i++){std::ifstream jf(std::string(folders[i])+"theme.json");std::string text((std::istreambuf_iterator<char>(jf)),{});themepack::Pack pack{};assert(themepack::parse(text.data(),text.size(),pack,why,sizeof why));assert(pack.centre==themepack::CENTRE_RADAR_EMBLEM);
 if(i==1){assert(pack.artOpacity==100&&pack.artScale==64);for(const char* badValue:{"101","-1"}){auto bad=text;auto at=bad.find("\"artOpacity\": 100");assert(at!=std::string::npos);bad.replace(at,17,std::string("\"artOpacity\": ")+badValue);themepack::Pack invalid{};assert(!themepack::parse(bad.data(),bad.size(),invalid,why,sizeof why));}}
std::ifstream bf(std::string(folders[i])+"emblem.bin",std::ios::binary);artwork[i].assign(std::istreambuf_iterator<char>(bf),{});assert(themepack::parseEmblem(artwork[i].data(),artwork[i].size(),emblems[i],why,sizeof why));assert(emblems[i].w==64&&emblems[i].h==64);logos[i]=pal::addPack(pack,&emblems[i]);assert(logos[i]>=0&&pal::centrepiece(logos[i])==pal::CP_RADAR_EMBLEM);}
 std::ifstream sf("../packs/z-em3ritus/theme.json");std::string sj((std::istreambuf_iterator<char>(sf)),{});themepack::Pack sp{};assert(themepack::parse(sj.data(),sj.size(),sp,why,sizeof why));assert(sp.centre==themepack::CENTRE_RADAR_EMBLEM);sp.centre=themepack::CENTRE_SIGIL;snprintf(sp.name,sizeof sp.name,"TEST SIGIL");int sigil=pal::addPack(sp,nullptr);assert(sigil>=0&&pal::centrepiece(sigil)==pal::CP_SIGIL);
 Compositor comp;for(int theme:{id,int(pal::TH_BAPH0MAU5),mascot,int(pal::TH_COSMICMUFFIN),logos[0],logos[1],logos[2],logos[3],sigil}){pal::setTheme(theme);for(auto wh:{std::pair<int,int>{480,320},{320,240}}){int w=wh.first,h=wh.second;auto sc=scanfix::demo(theme);sc.animateSweep=true;sc.now=1000;Fb first(w,h),full(w,h),partial(w,h);comp.renderFull(w,h,sc,first);for(int phase=0;phase<72;phase++){partial.p=first.p;sc.now=3600+phase*50;comp.renderFull(w,h,sc,full);comp.renderRegion(w,h,sc,radarBounds(w,h),partial);assert(full.p==partial.p);}assert(first.p!=full.p);
 auto hs=heroSub(uieng::scan(w,h).hero);assert(hs.spectrum.w<=(w>=400?168:140)&&hs.barMax==(w>=400?16:8));
 }
 }
 // Every supported landscape/portrait geometry must avoid the trig fallback.
 for(auto wh:{std::pair<int,int>{320,240},{480,320},{320,480},{240,320}}){
   const auto b=radarBounds(wh.first,wh.second);const int r=(b.w-5)/2;
   const auto hero=uieng::scan(wh.first,wh.second).hero;const auto hs=heroSub(hero);
   const int cy=b.y+r+2,top=hero.y+4,bottom=hs.barBase-hs.barMax-2;
   assert(cy==(top+bottom)/2-(wh.first>=400?6:2));
   assert(b.y>=hero.y && b.y+b.h<=hs.barBase-hs.barMax);
   if(wh.second==240)assert(r==55);if(wh.first==480)assert(r==85);
   for(int k=1;k<=3;k++){int n=0;assert(radargeometry::ring(r*k/3,n)&&n>0);}
 }
 // Artwork resampling is identical whether composed full-height or in strips,
 // including radii above the source size where a row must cover multiple pixels.
 for(int r:{39,55,58,85,98})for(int style=0;style<4;style++){
   const int w=240,h=240;std::vector<uint16_t> full(w*h),partial(w*h);
   S whole{full.data(),w,0,h};crewArt(whole,120,120,r,style);
   for(int y=0;y<h;y+=16){S strip{partial.data()+y*w,w,y,16};crewArt(strip,120,120,r,style);}
   assert(full==partial);
 }
 // Guarded strip rendering across every sweep angle and portrait-size radii.
 for(int r:{22,69,85,102})for(unsigned phase=0;phase<72;phase++){const int w=320,h=480;std::vector<uint16_t> guard(w*16+2,0xABCD);for(int y=0;y<h;y+=16){S strip{guard.data()+1,w,y,16};radarTrail(strip,160,260,r,phase);assert(guard.front()==0xABCD&&guard.back()==0xABCD);}}
 puts("clean radar: parser, reusable style, phase repaint, clipping and compact spectrum PASS");}
