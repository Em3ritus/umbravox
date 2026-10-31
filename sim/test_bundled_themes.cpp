#include "scan_fixture.h"
#include "../ui/muffin.h"
#include "../ui/muffinseal.h"
#include <fstream>
#include <iterator>
#include <string>
#include <vector>
#include <cassert>
#include <cstdio>
struct Fb:scanr::Sink {int w;std::vector<uint16_t> p;Fb(int W,int H):w(W),p(W*H){}void strip(int x,int y,int W,int H,const uint16_t* b)override{for(int r=0;r<H;r++)std::copy_n(b+r*W,W,p.data()+(y+r)*w+x);}};
int main(){
 const char* folders[]={"slick","z-baph0mau5","z-em3ritus","z-ember","z-skuld","phosphor","glacier","obsidian","cyber","ultraviolet","dune"};
 const char* names[]={"SLICK","BAPH0MAU5","EM3RITUS","EMBER","SKULD","PHOSPHOR","GLACIER","OBSIDIAN","CYBER","ULTRAVIOLET","DUNE"};
 scanr::Compositor comp;
 for(int i=0;i<11;i++){
  pal::clearPacks();const int id=3+i;assert(pal::themeCount()==14);assert(!strcmp(pal::themeName(id),names[i]));
  if(id==pal::TH_EMBER || id>=pal::TH_PHOSPHOR)assert(!strcmp(pal::header(id),"UMBRAVOX"));
  assert(pal::resolveSaved(names[i],0)==id && pal::resolveSaved("",id)==id);
  std::string base=std::string("../packs/")+folders[i]+"/";
  std::ifstream jf(base+"theme.json");std::string json((std::istreambuf_iterator<char>(jf)),{});themepack::Pack pack{};char why[96];assert(themepack::parse(json.data(),json.size(),pack,why,sizeof why));
  themepack::sanitizeStatus(pack);pal::setTheme(id);
  uint16_t actual[]={pal::VOID,pal::PANEL,pal::GRID,pal::EDGE,pal::EDGE_LIT,pal::DIM,pal::TEXT,pal::BRIGHT,pal::WITCH,pal::WITCH_HI,pal::BONE,pal::SWEEP,pal::SWEEP_HI,pal::LIVE,pal::LIVE_HI,pal::MAGENTA,pal::ICE,pal::GREEN,pal::AMBER,pal::RED};
  for(int k=0;k<20;k++)assert(actual[k]==pack.color[k]);
  assert(pal::startSweep(id)==pack.sweep && pal::sweepMask(id)==pack.sweeps && pal::packRite(id)==pack.rite);
  assert(pal::diamondBadge(id)==pack.diamond && pal::artOpacity(id)==pack.artOpacity && pal::artScale(id)==pack.artScale);
  std::ifstream bf(base+"emblem.bin",std::ios::binary);std::vector<uint8_t> raw((std::istreambuf_iterator<char>(bf)),{});themepack::Emblem e{};const auto* builtin=pal::emblem(id);
  if(raw.empty())assert(!builtin);else{
   assert(themepack::parseEmblem(raw.data(),raw.size(),e,why,sizeof why));assert(builtin && builtin->w==e.w && builtin->h==e.h && builtin->ncol==e.ncol);
   for(int k=0;k<e.ncol;k++)assert(builtin->pal[k]==e.pal[k]);
   for(int k=0;k<e.w*e.h;k++)assert(themepack::pixel(*builtin,k)==themepack::pixel(e,k));
  }
  for(auto wh:{std::pair<int,int>{320,240},{480,320}}){pal::clearPacks();pal::setTheme(id);Fb before(wh.first,wh.second),after(wh.first,wh.second);auto scene=scanfix::demo(id);comp.renderFull(wh.first,wh.second,scene,before);
   assert(pal::addPack(pack,raw.empty()?nullptr:&e)==id);assert(pal::themeCount()==14);pal::setTheme(id);comp.renderFull(wh.first,wh.second,scene,after);assert(before.p==after.p);
  }
  pack.color[themepack::R_WITCH]^=0xFFFF;assert(pal::addPack(pack,raw.empty()?nullptr:&e)==id);pal::setTheme(id);assert(pal::WITCH==pack.color[themepack::R_WITCH] && pal::themeCount()==14);
 }
 // Old SD copies must not restore a personal masthead on a general theme.
 pal::clearPacks();themepack::Pack legacy{};snprintf(legacy.name,sizeof legacy.name,"EMBER");snprintf(legacy.header,sizeof legacy.header,"EMBER");
 assert(pal::addPack(legacy,nullptr)==pal::TH_EMBER);assert(!strcmp(pal::header(pal::TH_EMBER),"UMBRAVOX"));
 pal::clearPacks();assert(pal::themeCount()==14);
 const auto& muffin=ui::muffinEmblem();for(int k=0;k<ui::MUFFIN_W*ui::MUFFIN_H;k++)assert(themepack::pixel(muffin,k)==ui::MUFFIN_PX[k]);
 puts("bundled themes: fourteen without SD; palettes/artwork exact; stable saved IDs; overrides without duplicates; both-size pixel parity PASS");
}
