// Review-only proposal: real palette/font/SCAN compositor, no firmware routing changes.
#include "../ui/scan_render.h"
#include "png_writer.h"
#include <vector>
#include <string>
#include <cassert>
#include <fstream>
using namespace scanr;
struct Header {uieng::Rect menu,title,gps,mesh,bridge,battery;};
Header header(int w){
 if(w>=400)return {{0,0,44,44},{44,0,164,44},{208,0,56,44},{264,0,48,44},{312,0,64,44},{376,0,w-376,44}};
 return {{0,0,44,44},{44,0,104,44},{148,0,48,44},{},{196,0,60,44},{256,0,w-256,44}};
}
void chrome(S& s,int w,int h,bool menu,uint8_t theme){
 auto c=header(w);box(s,0,0,w,44,pal::PANEL);box(s,0,h-44,w,44,pal::PANEL);
 box(s,8,43,w-16,1,pal::EDGE);box(s,8,h-45,w-16,1,pal::EDGE);
 for(int y:{8,13,18})box(s,14,y,16,2,menu?pal::BRIGHT:pal::TEXT);
 centered(s,22,28,"MENU",1,menu?pal::BRIGHT:pal::TEXT);
 if(menu)box(s,10,39,24,2,pal::WITCH_HI);
 const char* title=pal::themeName(theme);int scale=fitScale(title,c.title.w-8,2);
 centered(s,c.title.x+c.title.w/2,(44-7*scale)/2,title,scale,pal::BRIGHT);
 auto status=[&](uieng::Rect r,const char* label,const char* value,uint16_t color){
  if(!r.w)return;assert(r.w>=44);centered(s,r.x+r.w/2,7,label,1,pal::DIM);centered(s,r.x+r.w/2,23,value,1,color);
 };
 status(c.gps,"GPS","3D",pal::GREEN);status(c.mesh,"MESH","0",pal::TEXT);
 status(c.bridge,"BRIDGE","ON",pal::BRIGHT);box(s,c.bridge.x+(c.bridge.w-35)/2,35,35,2,pal::WITCH_HI);
 status(c.battery,"BAT","NO BAT",pal::DIM);
 const char* labels[]={"SCAN","DOMAIN","CODEX","POI","REC"};
 for(int i=0;i<5;i++){
  const int compact[]={0,60,140,208,260,320};
  int x=w>=400?i*w/5:compact[i],right=w>=400?(i+1)*w/5:compact[i+1];
  int ww=right-x;assert(ww>=44);
  int ns=fitScale(labels[i],ww-8,2);int width=Font::width(labels[i],ns);
  centered(s,x+ww/2,h-44+(44-7*ns)/2,labels[i],ns,(!menu&&i==0)?pal::BRIGHT:pal::TEXT);
  if(!menu&&i==0)box(s,x+(ww-width)/2,h-8,width,2,pal::WITCH_HI);
 }
}
void menuBody(S& s,int w,int h){
 const char* title[]={"PROBES","MESH","ALERTS","FILES","SETTINGS","DISPLAY"};
 const char* sub[]={"REQUESTED NETWORKS","NODES + DIAGNOSTICS","SIGNATURE MATCHES","RECORDINGS","RADIO + CAPTURE","THEMES + LABELS"};
 int cols=w>=400?3:2,rows=6/cols,gap=w>=400?6:2;
 int bw=w-16,bh=h-104;
 for(int i=0;i<6;i++){
  int col=i%cols,row=i/cols;
  int x=8+col*(bw+gap)/cols,y=52+row*(bh+gap)/rows;
  int right=8+(col+1)*(bw+gap)/cols-gap,bottom=52+(row+1)*(bh+gap)/rows-gap;
  int ww=right-x,hh=bottom-y;assert(ww>=44&&hh>=44);
  box(s,x,y,ww,hh,pal::PANEL);frame(s,x,y,ww,hh,pal::EDGE);
  int scale=fitScale(title[i],ww-12,2);int ty=y+(hh-7*scale)/2;
  if(w>=400)ty-=8;
  centered(s,x+ww/2,ty,title[i],scale,pal::TEXT);
  if(w>=400)centered(s,x+ww/2,ty+7*scale+10,sub[i],1,pal::DIM);
 }
}
int main(int argc,char** argv){
 if(argc!=2)return 2;
 for(int theme:{0,2})for(int w:{480,320}){
  int h=w==480?320:240;pal::setTheme(theme);
  ScanScene sc;sc.theme=theme;sc.wifi=143;sc.g5=32;sc.ble=73;sc.ch="CH 48";sc.chBand="5 GHz";sc.now=1000;
  const FieldBlip contacts[]={{20,30,0,900},{80,65,1,900},{130,70,2,900},{190,40,0,900},{245,85,1,900},{310,50,2,900}};
  const int channels[]={25,55,8,0,20,85,10,0,0,70,10,0,15,5,30,0,10,15,0,80,15};
  sc.blips=contacts;sc.nBlips=6;sc.channels=channels;sc.nChannels=21;
  for(int screen=0;screen<2;screen++){
   std::vector<uint16_t> pixels(w*h);
   for(int y=0;y<h;y+=16){S strip{pixels.data()+y*w,w,y,std::min(16,h-y)};
    if(screen){box(strip,0,0,w,h,pal::VOID);menuBody(strip,w,h);}else composeStrip(strip,w,h,sc);
    chrome(strip,w,h,screen,theme);
   }
   std::string name=std::string(argv[1])+"/"+(screen?"menu":"scan")+"-"+std::to_string(w)+(theme?"-muffin":"")+".png";
   if(!png::write(name.c_str(),pixels.data(),w,h,1))return 1;
  }
 }
}
