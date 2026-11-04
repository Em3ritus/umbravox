// Preview general-purpose packs through the production parser and compositor.
// Run from the repository root; argument is an existing output directory.
#include "scan_fixture.h"
#include "packload.h"
#include "png_writer.h"
#include <fstream>
#include <iterator>
#include <string>
#include <vector>
#include <cstdio>
#include <cstring>
struct ThemeFrame:scanr::Sink {
    int w;std::vector<uint16_t> pixels;
    ThemeFrame(int W,int H):w(W),pixels(W*H){}
    void strip(int x,int y,int W,int H,const uint16_t* p)override{
        for(int row=0;row<H;row++)std::copy_n(p+row*W,W,pixels.data()+(y+row)*w+x);
    }
};
int main(int argc,char** argv){
    if(argc!=2){fprintf(stderr,"usage: general_themes_png OUTPUT_DIRECTORY\n");return 1;}
    for(const char* slug:{"phosphor","glacier","obsidian","cyber","ultraviolet","dune"}){
        const std::string folder=std::string("packs/")+slug;
        std::ifstream f(folder+"/theme.json");std::string json((std::istreambuf_iterator<char>(f)),{});
        themepack::Pack pack{};char why[96];
        if(!themepack::parse(json.data(),json.size(),pack,why,sizeof why)){fprintf(stderr,"%s: %s\n",slug,why);return 1;}
        if(themepack::sanitizeStatus(pack)){fprintf(stderr,"%s: status contrast requires adjustment\n",slug);return 1;}
        pal::clearPacks();int id=packload::load(folder.c_str(),why,sizeof why);
        if(id<0){fprintf(stderr,"%s: %s\n",slug,why);return 1;}
        if(strcmp(pal::header(id),"UMBRAVOX") || pal::centrepiece(id)!=pal::CP_RADAR)return 1;
        pal::setTheme(id);
        for(int w:{480,320}){
            int h=w==480?320:240;auto scene=scanfix::demo(id);
            scene.sd=ui::StorageState::Ready;scene.pmuOk=true;
            scene.wifi=81;scene.g5=32;scene.ble=73;
            // Same sparse synthetic observations for every palette.
            const int channels[21]={30,15,0,0,0,65,8,0,0,0,43,0,0, 14,55,0,0,18,0,0,70};
            scene.channels=channels;
            scanr::Compositor compositor;ThemeFrame frame(w,h);
            compositor.renderFull(w,h,scene,frame);
            std::string out=std::string(argv[1])+"/"+slug+"-"+std::to_string(w)+".png";
            if(!png::write(out.c_str(),frame.pixels.data(),w,h,1))return 1;
        }
        printf("%s: accepted, status colors unchanged, UMBRAVOX header, both sizes rendered\n",slug);
    }
}
