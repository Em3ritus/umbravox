#include "../device/storage_paths.h"
#include "../device/filescan.h"
#include "../ui/files_present.h"
#include "png_writer.h"
#include <cassert>
#include <map>
#include <vector>
#include <string>
#include <set>
using namespace capturepaths;
struct Entry{std::string name;bool dir;uint32_t size;};
static std::map<std::string,std::vector<Entry>> tree;
static uint32_t clockMs=0,stepMs=0,seen=0;static std::set<std::string> visited;
static bool isDir(const char* p){return tree.count(p);}
static bool list(const char* p,bool(*cb)(const char*,bool,uint32_t,void*),void* c,uint32_t max){
 auto it=tree.find(p);if(it==tree.end())return false;visited.insert(p);uint32_t n=0;
 for(auto& e:it->second){if(n++>=max)break;clockMs+=stepMs;++seen;if(!cb(e.name.c_str(),e.dir,e.size,c))break;}return true;
}
static uint32_t now(){return clockMs;}
static bool exists(const char*){return true;}
static bool noDir(const char*){return false;}
static int removed=0;static bool removeFile(const char*){++removed;return true;}
int main(int argc,char**argv){
 const char* valid[]={"/WD-0001.csv","/WD-0001-clients.csv","/WD-0001-probes.csv","/WD-0001.gpx","/capture0001.pcap","/umbravox/sessions/WD-20261031-123456/wifi.csv","/umbravox/sessions/WD-20261031-123456/clients.csv","/umbravox/sessions/WD-20261031-123456/probes.csv","/umbravox/sessions/WD-20261031-123456/track.gpx","/UMBRAVOX/CAPTURES/CAPTURE0010.PCAP"};
 for(const char* p:valid){assert(classify(p)!=None);sdcmd::Path out;assert(sdcmd::sessionFile(p,out));assert(!sdcmd::delPolicy(p,"",out));}
 for(const char* p:{"/umbravox/sessions/../wifi.csv","/umbravox/sessions/WD-/wifi.csv","/umbravox/sessions/WD-1/../wifi.csv","/umbravox/sessions/WD-1/wifi.csv.tmp","/umbravox/sessions/WD-1/sub/wifi.csv","/other/capture0001.pcap","/umbravox/captures/WD-1.csv","//WD-1.csv","/diag.log","/poi.csv","/WD--.csv","/umbravox/sessions/WD-1"})assert(classify(p)==None);
 char p[64],b[64];assert(storagepaths::session(p,sizeof p,"/WD-20261031-123456.csv"));assert(!strcmp(p,valid[5]));
 assert(storagepaths::companion(b,sizeof b,p,"track.gpx")&&!strcmp(b,valid[8]));assert(storagepaths::companion(b,sizeof b,"/WD-0001.csv","clients.csv")&&!strcmp(b,valid[1]));
 assert(!storagepaths::session(b,8,"/WD-0001.csv"));assert(!storagepaths::companion(b,8,p,"track.gpx"));
 identity(valid[1],b,sizeof b);assert(!strcmp(b,"WD-0001"));identity(valid[5],b,sizeof b);assert(!strcmp(b,"WD-20261031-123456"));
 assert(filescan::newer(valid[5],"/WD-20261030-235959.csv"));
 ActiveFiles active;for(int i=0;i<5;i++)active.set(i,valid[i]);
 filescan::RemoveIo rm{exists,noDir,removeFile};
 for(const char* path:valid){sdcmd::Path out;const bool blocked=active.contains(path);assert(bool(sdcmd::delPolicy(path,active,out))==blocked);assert(bool(filescan::deleteCheck(path,active))==blocked);assert((filescan::removeLog(path,active,rm)==filescan::RM_REFUSED)==blocked);}
 assert(removed==5);assert(active.contains("/wd-0001.CSV"));active.set(1,"");assert(!filescan::deleteCheck(valid[1],active));
 tree["/"]={{"WD-0001.csv",false,100},{"WD-0001.gpx",false,200},{"capture0001.pcap",false,300},{"unrelated.csv",false,99},{"themes",true,0}};
 tree["/umbravox/sessions"]={{"WD-20261031-123456",true,0},{"../escape",true,0}};
 tree["/umbravox/sessions/WD-20261031-123456"]={{"wifi.csv",false,400},{"clients.csv",false,500},{"probes.csv",false,600},{"track.gpx",false,700},{"unrelated.txt",false,9},{"sub",true,0}};
 tree["/umbravox/captures"]={{"capture0002.pcap",false,800}};
 static ui::FileList rows;filescan::Browser browser;const filescan::BrowseIo io{isDir,list,now};browser.scan(rows,io);
 assert(rows.n==8&&rows.found==8&&!rows.partial&&rows.card);assert(visited.size()==4);
 ActiveFiles all;all.set(0,valid[5]);all.set(1,valid[8]);all.set(2,valid[6]);all.set(3,valid[7]);all.set(4,"/umbravox/captures/capture0002.pcap");filescan::markActive(rows,all);int protectedCount=0;for(int i=0;i<rows.n;i++)protectedCount+=rows.rows[i].active;assert(protectedCount==5);
 // Shared budget includes nested directory entries, not a fresh quota per folder.
 tree["/umbravox/sessions/WD-20261031-123456"].clear();for(int i=0;i<600;i++)tree["/umbravox/sessions/WD-20261031-123456"].push_back({"ignored",false,0});seen=0;browser.scan(rows,io);assert(rows.partial&&seen<=512);
 stepMs=600;clockMs=0;seen=0;browser.scan(rows,io);assert(rows.partial&&seen<=2);stepMs=0;
 // Production renderer and geometry at both sizes, including long confirm paths.
 pal::setTheme(0);for(int w:{320,480}){int h=w==320?240:320;auto l=uieng::files(w,h);
  auto hits=uieng::filesHits(w,h,false,true,l.nRows,10,4);for(int i=0;i<hits.count;i++){auto a=hits.items[i].rect;assert(a.w>=44&&a.h>=44&&a.x>=0&&a.y>=0&&a.x+a.w<=w&&a.y+a.h<=h);for(int j=0;j<i;j++){auto c=hits.items[j].rect;assert(!(a.x<c.x+c.w&&c.x<a.x+a.w&&a.y<c.y+c.h&&c.y<a.y+a.h));}}
  for(int confirm=0;confirm<2;confirm++){
   flspres::FilesStore store;store.begin(0,0);store.gps({true,3,12,100});store.storage(ui::StorageState::Ready);store.list(true,8,0,0,false);store.bar("IN USE",pal::AMBER);store.modal(confirm,valid[6]+1,"12K");
   for(int i=5;i<5+l.nRows;i++)store.addRow(valid[i]+1,"12K",true);
   std::vector<uint16_t> pixels(w*h);for(int y=0;y<h;y+=16){scanr::S strip{pixels.data()+y*w,w,y,std::min(16,h-y)};flsr::composeStrip(strip,w,h,store.scene());}
   if(argc==2){std::string file=std::string(argv[1])+(confirm?"/confirm-":"/files-")+std::to_string(w)+".png";assert(png::write(file.c_str(),pixels.data(),w,h,1));}
  }
 }
 puts("storage paths, mixed legacy/new browser, shared walk bounds, active protection and render geometry PASS");
}
