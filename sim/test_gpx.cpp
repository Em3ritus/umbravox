#include "../device/gpx.h"
#include "../device/sdlog.h"
#include <map>
#include <string>
#include <cassert>
#include <fstream>
#include <iostream>
std::map<std::string,std::string> files;
std::string current;
bool card=true,failWrite=false,failClose=false,failTruncate=false;
int opens=0;
namespace sdlog {
bool present(){return card;}
bool exists(const char* p){return files.count(p);}
uint32_t sizeOf(const char* p){return files[p].size();}
bool openAppend(const char* p,uint32_t& n){if(!card)return false;current=p;n=files[p].size();opens++;return true;}
bool writeAppend(const char* p,uint32_t n){if(failWrite){files[current].append(p,n/2);failWrite=false;return false;}files[current].append(p,n);return true;}
bool closeAppend(){const bool ok=!failClose;failClose=false;return ok;}
bool truncateTo(const char* p,uint32_t n){if(failTruncate||!card)return false;files[p].resize(n);return true;}
}
ui::Gnss::Fix fix(uint32_t at){ui::Gnss::Fix f;f.valid=true;f.atMs=at;f.lat7=-123456789;f.lon7=1234567890;return f;}
void tick(const char* p,uint32_t now,uint32_t epoch=1793318400){gpx::tick(p,fix(now),now,epoch);}
void stop(uint32_t now){gpx::tick("",{},now,0);}
size_t occurrences(const std::string& s,const std::string& v){size_t n=0,p=0;while((p=s.find(v,p))!=std::string::npos){n++;p+=v.size();}return n;}
void complete(const char* p){assert(files[p].find("<gpx ")!=std::string::npos);assert(files[p].rfind("</trkseg></trk></gpx>\n")==files[p].size()-std::string("</trkseg></trk></gpx>\n").size());}
int main(){
 gpx::tick("/WD-0001.csv",{},0,0);gpx::tick("/WD-0001.csv",{},5000,0);assert(files.empty()&&std::string(gpx::status())=="WAIT GPS");
 tick("/WD-0001.csv",6000);assert(gpx::points()==1);complete("/WD-0001.gpx");
 const auto before=files["/WD-0001.gpx"];const int opened=opens;
 for(int t=6001;t<7000;t++)gpx::tick("/WD-0001.csv",fix(6000),t,1793318400);assert(opens==opened);
 for(int t=7000;t<=10000;t+=1000)tick("/WD-0001.csv",t);
 assert(files["/WD-0001.gpx"]==before);stop(10001);assert(gpx::points()==5);complete("/WD-0001.gpx");
 assert(occurrences(files["/WD-0001.gpx"],"<trkpt ")==5);
 assert(occurrences(files["/WD-0001.gpx"],"<time>")==5 && files["/WD-0001.gpx"].find("Z</time>")!=std::string::npos);
 assert(files["/WD-0001.gpx"].find("lat=\"-12.3456789\" lon=\"123.4567890\"")!=std::string::npos);
 // Gap splits segments; missing wall clock omits time, including valid 0,0.
 tick("/WD-0002.csv",20000,0);gpx::tick("/WD-0002.csv",fix(20000),25000,0);
 auto zero=fix(26000);zero.lat7=zero.lon7=0;gpx::tick("/WD-0002.csv",zero,26000,0);stop(27000);
 assert(occurrences(files["/WD-0002.gpx"],"<trkseg>")==2);assert(files["/WD-0002.gpx"].find("<time>")==std::string::npos);complete("/WD-0002.gpx");
 // Strict freshness cutoff and invalid coordinates never exported.
 gpx::tick("/WD-0003.csv",fix(30000),35000,0);auto bad=fix(35001);bad.lat7=INT32_MIN;gpx::tick("/WD-0003.csv",bad,35001,0);stop(35002);assert(!files.count("/WD-0003.gpx"));
 // Rollover drains old file and starts new one on the following loop pass.
 tick("/WD-0004.csv",40000);tick("/WD-0005.csv",41000);complete("/WD-0004.gpx");assert(!files.count("/WD-0005.gpx"));tick("/WD-0005.csv",41001);stop(41002);complete("/WD-0005.gpx");
 // Existing file is never overwritten, including a restart with same CSV.
 const auto old=files["/WD-0005.gpx"];tick("/WD-0005.csv",42000);stop(42001);assert(files["/WD-0005.gpx"]==old&&gpx::takeError());
 // Short append rolls back to the last complete GPX and stops further writes.
 tick("/WD-0006.csv",50000);tick("/WD-0006.csv",55000);const auto good=files["/WD-0006.gpx"];failWrite=true;tick("/WD-0006.csv",60000);assert(files["/WD-0006.gpx"]==good&&gpx::takeError());assert(!gpx::takeError());const int blocked=opens;tick("/WD-0006.csv",65000);assert(opens==blocked);stop(65001);
 // Close verification failure also restores the complete previous document.
 tick("/WD-0007.csv",70000);tick("/WD-0007.csv",75000);const auto good7=files["/WD-0007.gpx"];failClose=true;tick("/WD-0007.csv",80000);assert(files["/WD-0007.gpx"]==good7&&gpx::takeError());stop(80001);
 // No-card and truncation failures are latched, not retried every frame.
 tick("/WD-0008.csv",90000);card=false;tick("/WD-0008.csv",95000);assert(gpx::takeError());card=true;stop(95001);
 tick("/WD-0009.csv",100000);tick("/WD-0009.csv",105000);const auto good9=files["/WD-0009.gpx"];failTruncate=true;tick("/WD-0009.csv",110000);assert(files["/WD-0009.gpx"]==good9&&gpx::takeError());failTruncate=false;stop(110001);
 // Millisecond wrap preserves sampling and batch deadlines.
 tick("/WD-0010.csv",0xfffffff0u);tick("/WD-0010.csv",0xfffffff0u+1000u);tick("/WD-0010.csv",0xfffffff0u+5000u);assert(gpx::points()==3);stop(6000);complete("/WD-0010.gpx");
 const char* nested="/umbravox/sessions/WD-20261031-123456/wifi.csv";
 tick(nested,120000);assert(!strcmp(gpx::activeFile(),"/umbravox/sessions/WD-20261031-123456/track.gpx"));
 tick(nested,121000);assert(gpx::activeFile()[0]);stop(121001);assert(!gpx::activeFile()[0]);complete("/umbravox/sessions/WD-20261031-123456/track.gpx");
 std::ofstream("/tmp/umbravox-gpx-time.gpx")<<files["/WD-0001.gpx"];
 std::ofstream("/tmp/umbravox-gpx-gap.gpx")<<files["/WD-0002.gpx"];
 std::cout<<"GPX session, batching, timestamps, stale gaps, rollover, failures and wrap PASS\n";
}
