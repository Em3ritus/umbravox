#include "../device/evidence_csv.h"
#include "../device/sdlog.h"
#include <map>
#include <string>
#include <cassert>
#include <iostream>
#include <fstream>
std::map<std::string,std::string> files;std::string path;
bool card=true,shortWrite=false,closeFail=false;unsigned opens=0,visits=0;
namespace sdlog {
bool present(){return card;}bool exists(const char* p){return files.count(p);}
uint32_t sizeOf(const char* p){return files[p].size();}
bool openAppend(const char* p,uint32_t& n){if(!card)return false;path=p;n=files[p].size();opens++;return true;}
bool writeAppend(const char* p,uint32_t n){if(shortWrite){files[path].append(p,n/2);shortWrite=false;return false;}files[path].append(p,n);return true;}
bool closeAppend(){bool ok=!closeFail;closeFail=false;return ok;}
bool truncateTo(const char* p,uint32_t n){if(!card)return false;files[p].resize(n);return true;}
}
clients::Record c; evidence::ProbeRow p;
size_t source(unsigned slot,char* out,size_t cap,uint32_t now,uint32_t epoch){
 visits++;assert(slot<768);
 if(slot==0)return evidencecsv::client(out,cap,c,now,epoch,3,4);
 if(slot==256)return evidencecsv::probe(out,cap,p,now,epoch,5,6);
 return 0;
}
size_t dense(unsigned slot,char* out,size_t cap,uint32_t now,uint32_t epoch){
 visits++;return slot<256?evidencecsv::client(out,cap,c,now,epoch,0,0):evidencecsv::probe(out,cap,p,now,epoch,0,0);
}
void run(const char* name,uint32_t at,unsigned ticks=800){for(unsigned i=0;i<ticks;i++){unsigned before=opens,v=visits;evidencecsv::tick(name,at+i,1793318400,source);assert(opens-before<=1&&visits-v<=1);}}
int main(){
 c.used=true;c.last.ap[0]=0x10;c.last.sta[0]=2;c.last.channel=6;c.last.at=1000;c.first=100;c.frames=9;c.evidence=3;
 p.used=true;p.last.sta[0]=2;p.last.channel=11;p.last.rssi=-73;p.last.at=900;p.first=50;p.count=8;
 const uint8_t ssid[]={'=', '"', ',', 0, 0xff};memcpy(p.last.name,ssid,5);p.last.len=5;
 char row[512];auto n=evidencecsv::client(row,sizeof row,c,1000,0,3,4);
 assert(n&&std::string(row).find(",6,,,DATA+ASSOC_OK,100,1000,9,3,4\r\n")!=std::string::npos);
 assert(evidencecsv::client(row,sizeof row,c,301000,0,0,0)==0);
 c.hasRssi=true;c.stationRssi=-62;c.rssiAt=800;
 evidencecsv::client(row,sizeof row,c,1000,0,0,0);assert(std::string(row).find(",6,-62,200,")!=std::string::npos);
 evidencecsv::probe(row,sizeof row,p,1000,0,5,6);assert(std::string(row).find("3D222C00FF")!=std::string::npos);assert(std::string(row).find("\"'=\"\",..\"")!=std::string::npos);
 run("/WD-0001.csv",1000);assert(evidencecsv::rows()==2);assert(files.size()==2);
 auto old=files;unsigned opened=opens;run("/WD-0001.csv",2000,10);assert(opens==opened);
 run("/WD-0001.csv",13000);assert(evidencecsv::rows()==4);evidencecsv::stop();
 assert(files["/WD-0001-probes.csv"].find("2026-")!=std::string::npos);
 // Existing files aren't overwritten, including restart with same stem.
 old=files;run("/WD-0001.csv",15000);assert(evidencecsv::takeError()&&files==old);evidencecsv::stop();
 // Failure rolls partial append back and latches; no repeated writes.
 run("/WD-0002.csv",20000);auto good=files["/WD-0002-clients.csv"];shortWrite=true;run("/WD-0002.csv",32000);assert(evidencecsv::takeError());assert(files["/WD-0002-clients.csv"]==good);opened=opens;run("/WD-0002.csv",45000);assert(opens==opened);evidencecsv::stop();
 run("/WD-0003.csv",50000);good=files["/WD-0003-clients.csv"];closeFail=true;run("/WD-0003.csv",62000);assert(evidencecsv::takeError()&&files["/WD-0003-clients.csv"]==good);evidencecsv::stop();
 // Stop midway drains accepted rows, never invents a full final snapshot.
 run("/WD-0004.csv",70000,2);run("",70002,4);assert(evidencecsv::rows()==1&&std::string(evidencecsv::status())=="OFF");
 // Rollover drains old batches before accepting a different filename.
 run("/WD-0005.csv",80000,2);run("/WD-0006.csv",80002);assert(files.count("/WD-0005-clients.csv")&&files.count("/WD-0006-probes.csv"));evidencecsv::stop();
 card=false;run("/WD-0007.csv",90000);assert(evidencecsv::takeError());card=true;evidencecsv::stop();
 // Wildcard has empty raw name, and no inferred AP/SSID association column.
 p.last.len=0;evidencecsv::probe(row,sizeof row,p,1000,0,0,0);assert(std::string(row).find("WILDCARD,\"'\",,")!=std::string::npos);
 // All 32 raw bytes fit, including quote expansion.
 p.last.len=32;memset(p.last.name,'"',32);assert(evidencecsv::probe(row,sizeof row,p,1000,0,0,0)>0);
 // Full tables fill and flush multiple batches without skipping retry slots.
 c.last.at=100000;c.rssiAt=100000;
 for(unsigned i=0;i<1100;i++){unsigned before=opens,v=visits;evidencecsv::tick("/WD-0008.csv",100000+i,0,dense);assert(opens-before<=1&&visits-v<=1);}
 assert(evidencecsv::rows()==768);evidencecsv::stop();
 // Slot reuse is serialized from current contents, never cached by slot number.
 c.last.sta[5]=0xab;c.first=120000;c.last.at=120000;
 run("/WD-0009.csv",120000);evidencecsv::stop();assert(files["/WD-0009-clients.csv"].find("02:00:00:00:00:AB")!=std::string::npos);
 std::ofstream("/tmp/umbravox-clients.csv")<<files["/WD-0001-clients.csv"];
 std::ofstream("/tmp/umbravox-probes.csv")<<files["/WD-0001-probes.csv"];
 std::cout<<"Evidence CSV formatting, lossless SSIDs, bounds, batching, stop, rollover and failure recovery PASS\n";
}
