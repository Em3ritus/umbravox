#include "../ui/aspect_present.h"
#include "png_writer.h"
#include <vector>
#include <string>
int main(int argc,char**argv){if(argc!=2)return 2;pal::setTheme(0);for(int w:{480,320}){int h=w==480?320:240;for(int pg=0;pg<6;pg++){
 asppres::AspectStore store;store.begin(0,0);store.gps({true,3,12,300});store.header(true,true,false);store.storage(ui::StorageState::Ready);store.appearance("UMBRAVOX","RADAR","WARD",false,0);store.labels(true);
 displayctl::State v;v.tab=pg<3?pg:1;if(pg>=3)v.editor=pg==3?4:pg==4?0:6;
 if(pg==3){for(int x=80;x<180;x+=4)displayctl::sample(v,w,h,true,x,98);displayctl::sample(v,w,h,false,-1,-1);}
 ui::Settings settings;store.controls(v,settings);std::vector<uint16_t> pixels(w*h);for(int y=0;y<h;y+=16){scanr::S s{pixels.data()+y*w,w,y,std::min(16,h-y)};aspr::composeStrip(s,w,h,store.scene());}
 const char* names[]={"display","touch","appearance","touch-test","brightness","sensitivity"};std::string file=std::string(argv[1])+"/"+names[pg]+"-"+std::to_string(w)+".png";if(!png::write(file.c_str(),pixels.data(),w,h,1))return 1;
 }} }
