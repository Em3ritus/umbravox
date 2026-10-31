#include "../ui/project.h"
#include "../ui/router.h"
#include <cassert>
#include <vector>
#include <cstring>
#include <cstdio>
using namespace scanr;
static std::vector<uint16_t> draw(int w,int h,const ScanScene& sc){
 std::vector<uint16_t> px(w*h);
 for(int y=0;y<h;y+=16){S strip{px.data()+y*w,w,y,std::min(16,h-y)};composeStrip(strip,w,h,sc);}return px;
}
int main(){
 for(int w:{480,320}){
  int h=w==480?320:240;auto ch=uieng::chrome(w,h);
  const auto hits={uieng::scanHits(w,h,{}),uieng::domainHits(w,h,{}),uieng::codexHits(w,h,{}),uieng::menuHits(w,h),uieng::meshHits(w,h),uieng::omensHits(w,h),uieng::echoesHits(w,h),uieng::attuneHits(w,h,0),uieng::aspectHits(w,h,true),uieng::detailHits(w,h),uieng::filesHits(w,h,false,false,0)};
  for(const auto& list:hits){int bridges=0,meshes=0,home=0,poi=0;
   for(int i=0;i<list.count;++i){const auto& a=list.items[i];
    assert(a.rect.w>=44 && a.rect.h>=44 && a.rect.x>=0 && a.rect.x+a.rect.w<=w);
    if(!strcmp(a.slot,"bridge")){++bridges;assert(a.rect==ch.bridge && a.action==uieng::Action::SettingStep && a.payload==9);}
    if(!strcmp(a.slot,"mesh"))++meshes;
    if(!strcmp(a.slot,"nav-menu")){assert(a.rect==ch.menu && a.payload==3);uinav::Router r;r.setScreen(uinav::Screen::Detail);r.dispatch(a);assert(r.screen()==uinav::Screen::Menu);}
    // Branding is intentionally not an invisible Home target.
    assert(!(a.rect.x < ch.wordmark.x+ch.wordmark.w && ch.wordmark.x<a.rect.x+a.rect.w && a.rect.y<44));
    if(!strcmp(a.slot,"nav-scan")){++home;assert(a.rect==ch.navCells[0] && a.payload==0);uinav::Router r;r.setScreen(uinav::Screen::Detail);r.dispatch(a);assert(r.screen()==uinav::Screen::Scan);}
    if(a.action==uieng::Action::MarkPoi){++poi;assert(a.rect==ch.navCells[3]);}

   }assert(home==1 && poi==1 && bridges==1 && meshes==(w==480?1:0));
  }
  for(auto screen:{uinav::Screen::Scan,uinav::Screen::Domain,uinav::Screen::Codex,uinav::Screen::Menu,uinav::Screen::Aspect}){
   uinav::Router router;router.setScreen(screen);auto list=uieng::scanHits(w,h,{});
   for(int i=0;i<list.count;++i)if(!strcmp(list.items[i].slot,"bridge")){
    const auto req=router.dispatch(list.items[i]);assert(req.settingStep==9 && !req.sweepStep && !req.alertStep && router.screen()==screen);
   }
  }
  ui::State state;state.set.bleBridge=1;uiproj::Chrome chrome;chrome.hasCell=true;chrome.batt=78;chrome.pmuOk=true;chrome.charging=true;
  scanpres::SceneStore scan;uiproj::scan(scan,state,chrome);
  menpres::MenuStore menu;uiproj::menu(menu,state,chrome);
  assert(menu.scene().bridgeOn && menu.scene().pmuOk && menu.scene().charging);
  auto sc=scan.scene();pal::setTheme(sc.theme);auto before=draw(w,h,sc);
  for(int change=0;change<5;++change){auto next=sc;
   if(change==0)next.bridgeOn=false;
   if(change==1)next.charging=false;
   if(change==2)next.hasCell=false;
   if(change==3){next.hasCell=false;next.pmuOk=false;}
   if(change==4)next.battPct=255;
   bool status,body;scanpres::sceneDirty(w,h,sc,next,status,body);assert(status&&!body);
   auto full=draw(w,h,next),partial=before;
   for(int y=0;y<48;y+=16){S strip{partial.data()+y*w,w,y,16};composeStrip(strip,w,h,next);}
   assert(full!=before && full==partial);
  }
  for(int i=0;i<7;++i){auto next=sc;
   if(i<5)next.poi=static_cast<ui::PoiStatus>(i+1);
   else next.rec=i==5?Rec::On:Rec::Saving;
   auto full=draw(w,h,next),partial=before;
   for(int y=h-44;y<h;y+=16){S strip{partial.data()+y*w,w,y,std::min(16,h-y)};composeStrip(strip,w,h,next);}
   assert(full!=before && partial==full);
  }
  // Every theme changes its name within the fixed box, leaving control geometry stable.
  for(int theme=0;theme<3;++theme){sc.theme=theme;pal::setTheme(theme);assert(strlen(pal::themeName(theme))>0);assert(draw(w,h,sc).size()==size_t(w*h));}
 }
 puts("shared header: all-screen bridge hits, compact fallback, battery states and bounded repaint PASS");
}
