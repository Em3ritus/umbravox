#include "../ui/project.h"
#include "png_writer.h"
#include <vector>
#include <string>
int main(int argc,char** argv){
 if(argc!=2)return 2;
 pal::setTheme(0);
 for(int w:{480,320}){
  const int h=w==480?320:240;
  ui::State s;s.set.bleBridge=1;uiproj::Chrome c;c.gps={true,3,12,300};c.rec=scanr::Rec::Off;
  atnpres::AttuneStore store;const int per=uieng::attuneRowsPerPage(h);
  uiproj::attune(store,s,c,8/per,per);
  std::vector<uint16_t> pixels(w*h);
  for(int y=0;y<h;y+=16){scanr::S strip{pixels.data()+y*w,w,y,std::min(16,h-y)};atnr::composeStrip(strip,w,h,store.scene());}
  std::string file=std::string(argv[1])+"/bridge-"+std::to_string(w)+".png";
  if(!png::write(file.c_str(),pixels.data(),w,h,1))return 1;
 }
}
