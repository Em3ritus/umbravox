// Host-only portrait design study. Not the device's portrait implementation.
// Reuses production palette, glyphs, drawing primitives and theme parser.
#include "scan_fixture.h"
#include "png_writer.h"
#include <fstream>
#include <iterator>
#include <vector>
#include <string>
#include <cassert>
using namespace scanr;
static uint32_t previewPhase=27;
struct Layout {int w,h,top=88,foot,bodyH;uieng::Rect tiles[3];};
Layout layout(int w,int h){Layout g{w,h,88,h-44,h-132,{}};for(int i=0;i<3;i++)g.tiles[i]={8+i*(w-16)/3,96,(w-16)/3-4,52};return g;}
void circle(S& s,int x,int y,int r,uint16_t c){for(int a=0;a<1440;a++){double t=a*3.14159265/720;pt(s,x+int(r*cos(t)),y+int(r*sin(t)),c);}}
void button(S& s,uieng::Rect r,const char* label,bool active=false){box(s,r.x,r.y,r.w,r.h,pal::PANEL);centered(s,r.x+r.w/2,r.y+(r.h-7)/2,label,1,active?pal::BRIGHT:pal::DIM);if(active)box(s,r.x+8,r.y+r.h-4,r.w-16,2,pal::WITCH);}
void chrome(S& s,const Layout& g,int active){box(s,0,s.y0,g.w,s.h,pal::VOID);box(s,0,0,g.w,88,pal::PANEL);
 for(int k=0;k<3;k++)box(s,15,13+k*5,15,1,pal::TEXT);centered(s,22,32,"MENU",1,pal::DIM);
 diamondInto(s,57,21,7,pal::WITCH);txt(s,72,15,"SLICK",2,pal::BRIGHT);
 int sd=g.w-70,bat=g.w-35;frame(s,sd,16,12,15,pal::GREEN);line(s,sd+3,19,sd+9,25,pal::GREEN);
 frame(s,bat,18,23,11,pal::TEXT);box(s,bat+24,21,2,5,pal::TEXT);box(s,bat+2,20,15,7,pal::GREEN);
 const char* labels[]={"GPS 3D","NODES 2","BRIDGE"};for(int i=0;i<3;i++)button(s,{i*g.w/3,44,(i+1)*g.w/3-i*g.w/3,44},labels[i],i==2);
 const char* nav[]={"SCAN","DOMAIN","CODEX","POI","REC"};for(int i=0;i<5;i++)button(s,{i*g.w/5,g.foot,(i+1)*g.w/5-i*g.w/5,44},nav[i],i==active);
}
void rows(S& s,int w,int y,int limit){const char* names[]={"LAFAYETTE_LN","KATY AND HANNAH WIFI","<HIDDEN>","GUEST NETWORK"};for(int i=0;i<limit;i++){int top=y+i*56;box(s,8,top,w-16,52,pal::PANEL);box(s,8,top,2,52,i%2?pal::ICE:pal::MAGENTA);txt(s,16,top+6,names[i%4],1,pal::BRIGHT);char b[64];snprintf(b,sizeof b,"%d dBm  CH %d  WPA2",-41-i*9,i%2?48:6);txt(s,16,top+20,b,1,pal::DIM);snprintf(b,sizeof b,"00:30:44:12:34:%02X",i+1);txt(s,16,top+34,b,1,pal::TEXT);}}
void portrait(S& s,const Layout& g,int screen,int theme){int w=g.w,h=g.h;chrome(s,g,screen<3?screen:-1);
 if(screen==0){const char* names[]={"WIFI","5 GHZ","BLE"};const char* nums[]={"169","32","73"};uint16_t colors[]={pal::MAGENTA,pal::ICE,pal::GREEN};for(int i=0;i<3;i++){auto r=g.tiles[i];box(s,r.x,r.y,r.w,r.h,pal::PANEL);box(s,r.x,r.y,2,r.h,colors[i]);txt(s,r.x+8,r.y+6,names[i],1,pal::DIM);txt(s,r.x+8,r.y+24,nums[i],2,pal::BRIGHT);}
 txt(s,12,158,"LIVE FIELD",1,pal::DIM);txt(s,w-68,158,"CH 48",1,pal::BRIGHT);
 int bottom=g.foot-50,cy=(174+bottom)/2,r=std::min((w-32)/2,(bottom-174)/2)-4;
 radarTrail(s,w/2,cy,r,previewPhase);for(int k=1;k<=3;k++)circle(s,w/2,cy,r*k/3,pal::EDGE);box(s,w/2-4,cy,9,1,pal::BRIGHT);box(s,w/2,cy-4,1,9,pal::BRIGHT);
 for(int i=0;i<6;i++){auto b=scanfix::BLIPS[i];auto m=placeMarker(b.bearing,b.dist,b.band,0,r);box(s,w/2+m.dx-2,cy+m.dy-2,4,4,colors[b.band]);}
 int pitch=std::min(8,(w-24)/21),sx=(w-pitch*21)/2;for(int i=0;i<21;i++){int bh=scanfix::CHANNELS[i]*16/100;box(s,sx+i*pitch,g.foot-22-bh,pitch-2,bh,colors[i<13?0:1]);}txt(s,sx,g.foot-15,"2.4 GHZ",1,pal::DIM);txt(s,sx+13*pitch,g.foot-15,"5 GHZ",1,pal::DIM);
 }else if(screen==1){txt(s,12,100,"DOMAIN",2,pal::BRIGHT);int mapH=h>=400?112:64,cy=122+mapH/2,r=mapH/2-12;circle(s,w/2,cy,r,pal::EDGE);circle(s,w/2,cy,r/2,pal::EDGE);centered(s,w/2,120,"N",1,pal::DIM);box(s,w/2+8,cy-5,3,3,pal::ICE);box(s,w/2-10,cy+5,3,3,pal::MAGENTA);txt(s,14,cy,"-",2,pal::TEXT);txt(s,w-30,cy,"+",2,pal::TEXT);txt(s,12,114+mapH,"25M  /  12 LOCATED",1,pal::DIM);int y=132+mapH;rows(s,w,y,(g.foot-y)/56);
 }else if(screen==2){const char* tabs[]={"WIFI","5 GHZ","BLE","LOCATED"};for(int i=0;i<4;i++)button(s,{8+i*(w-16)/4,96,(w-16)/4,44},tabs[i],i==0);rows(s,w,148,(g.foot-148)/56);
 }else{const char* names[]={"PROBES","NODES","ALERTS","FILES","SETTINGS","DISPLAY"};int rh=(g.bodyH-16)/3;for(int i=0;i<6;i++)button(s,{8+(i%2)*(w-12)/2,96+(i/2)*rh,(w-20)/2,rh-6},names[i]);}
 (void)theme;
}
int main(int argc,char** argv){if(argc!=3)return 2;std::ifstream file(argv[1]);std::string json((std::istreambuf_iterator<char>(file)),{});themepack::Pack pack{};char why[96];assert(themepack::parse(json.data(),json.size(),pack,why,sizeof why));assert(!themepack::sanitizeStatus(pack));int theme=pal::addPack(pack,nullptr);assert(theme>=0);pal::setTheme(theme);
 for(int w:{320,240}){int h=w==320?480:320;auto g=layout(w,h);assert(w/5>=44);for(int screen=0;screen<4;screen++){std::vector<uint16_t> pixels(w*h);for(int y=0;y<h;y+=16){S strip{pixels.data()+y*w,w,y,std::min(16,h-y)};portrait(strip,g,screen,theme);}const char* labels[]={"scan","domain","codex","menu"};std::string f=std::string(argv[2])+"/portrait-"+labels[screen]+"-"+std::to_string(w)+".png";assert(png::write(f.c_str(),pixels.data(),w,h,1));}}
 for(int w:{480,320}){int h=w==480?320:240;auto sc=scanfix::demo(theme);sc.bridgeOn=true;sc.sd=ui::StorageState::Ready;sc.pmuOk=true;std::vector<uint16_t> pixels(w*h);for(int y=0;y<h;y+=16){S strip{pixels.data()+y*w,w,y,std::min(16,h-y)};composeStrip(strip,w,h,sc);}std::string f=std::string(argv[2])+"/slick-landscape-"+std::to_string(w)+".png";assert(png::write(f.c_str(),pixels.data(),w,h,1));}
 for(int frame=0;frame<72;frame++)for(bool tall:{false,true}){
  int w=tall?320:480,h=tall?480:320;previewPhase=frame;auto g=layout(w,h);auto sc=scanfix::demo(theme);sc.animateSweep=true;sc.now=(72+frame)*50;sc.bridgeOn=true;sc.sd=ui::StorageState::Ready;sc.pmuOk=true;
  std::vector<uint16_t> pixels(w*h);for(int y=0;y<h;y+=16){S strip{pixels.data()+y*w,w,y,std::min(16,h-y)};if(tall)portrait(strip,g,0,theme);else composeStrip(strip,w,h,sc);}
  char name[64];snprintf(name,sizeof name,"/%s-frame-%02d.png",tall?"portrait":"landscape",frame);assert(png::write((std::string(argv[2])+name).c_str(),pixels.data(),w,h,1));
 }
 puts("SLICK: production parser, status contrast and previews PASS");
}
