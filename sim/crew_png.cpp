#include "scan_fixture.h"
#include "png_writer.h"
#include <fstream>
#include <iterator>
#include <vector>
#include <string>
#include <cassert>
using namespace scanr;
int main(int argc,char**argv){assert(argc==5);int id=1;if(std::string(argv[1])!="builtin"){
 std::ifstream file(argv[1]);std::string j((std::istreambuf_iterator<char>(file)),{});themepack::Pack p{};char why[128]{};if(!themepack::parse(j.data(),j.size(),p,why,sizeof why)){fprintf(stderr,"%s\n",why);return 1;}printf("Parsed %s; status adjustments: %d\n",p.name,themepack::sanitizeStatus(p));
 snprintf(p.name,sizeof p.name,"%s",argv[2]);

 id=pal::addPack(p,nullptr);assert(id>=0);
 }pal::setTheme(id);
 for(int w:{480,320}){int h=w==480?320:240;auto sc=scanfix::demo(id);sc.bridgeOn=true;sc.sd=ui::StorageState::Ready;sc.pmuOk=true;std::vector<uint16_t> px(w*h);for(int y=0;y<h;y+=16){S strip{px.data()+y*w,w,y,std::min(16,h-y)};composeStrip(strip,w,h,sc);}std::string path=std::string(argv[3])+"/"+argv[4]+"-"+std::to_string(w)+".png";assert(png::write(path.c_str(),px.data(),w,h,1));}
}
