#include "../device/evidence_csv.h"
#include "../device/sdlog.h"
#include <map>
#include <string>
#include <cassert>
#include <iostream>
#include <fstream>
const unsigned MAXP=evidencecsv::MAX_PARTS;
size_t allocBytes=0;unsigned allocCalls=0;bool allocFail=false;
void* testBuf=nullptr;
void* testAlloc(size_t n){allocCalls++;allocBytes=n;free(testBuf);testBuf=allocFail?nullptr:calloc(1,n);return testBuf;}
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
 // An unchanged record is not written again: only new or changed evidence adds a row.
 run("/WD-0001.csv",13000);assert(evidencecsv::rows()==2);evidencecsv::stop();
 assert(files["/WD-0001-probes.csv"].find("2026-")!=std::string::npos);
 // Existing files aren't overwritten, including restart with same stem.
 old=files;run("/WD-0001.csv",15000);assert(evidencecsv::takeError()&&files==old);assert(evidencecsv::failReason()[0]);evidencecsv::stop();
 // Failure rolls partial append back and latches; no repeated writes.
 run("/WD-0002.csv",20000);auto good=files["/WD-0002-clients.csv"];c.frames++;shortWrite=true;run("/WD-0002.csv",32000);assert(evidencecsv::takeError());assert(files["/WD-0002-clients.csv"]==good);opened=opens;run("/WD-0002.csv",45000);assert(opens==opened);evidencecsv::stop();
 run("/WD-0003.csv",50000);good=files["/WD-0003-clients.csv"];c.frames++;closeFail=true;run("/WD-0003.csv",62000);assert(evidencecsv::takeError()&&files["/WD-0003-clients.csv"]==good);evidencecsv::stop();
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
 // Snapshot de-duplication. Time-only changes (snapshot_ms, rssi_age_ms, loss counters) never re-write a row.
 c.last.sta[5]=0;c.first=100;c.last.at=1000;c.frames=9;c.hasRssi=true;c.stationRssi=-62;c.rssiAt=800;
 p.last.len=5;p.last.sta[5]=0;p.first=50;p.last.at=900;p.count=8;
 run("/WD-0010.csv",200000);assert(evidencecsv::rows()==2);
 run("/WD-0010.csv",213000);assert(evidencecsv::rows()==2);
 run("/WD-0010.csv",226000);assert(evidencecsv::rows()==2);
 // New evidence on a retained record writes exactly one more row for it.
 c.frames++;c.last.at=1500;
 run("/WD-0010.csv",239000);assert(evidencecsv::rows()==3);
 run("/WD-0010.csv",252000);assert(evidencecsv::rows()==3);
 p.count++;p.last.at=1600;p.last.rssi=-70;
 run("/WD-0010.csv",265000);assert(evidencecsv::rows()==4);
 // A different record that reuses the slot is written even if its counters match the old one.
 p.last.sta[5]=0x77;
 run("/WD-0010.csv",278000);assert(evidencecsv::rows()==5);
 run("/WD-0010.csv",291000);assert(evidencecsv::rows()==5);
 evidencecsv::stop();
 {auto& t=files["/WD-0010-probes.csv"];size_t lines=0;for(char ch:t)lines+=ch=='\n';assert(lines==1+3);} // header + 3 probe rows
 // A record that expires and returns (same fields) is written again.
 {c.last.at=299000;c.used=false;run("/WD-0013.csv",300000);assert(evidencecsv::rows()==1);c.used=true;run("/WD-0013.csv",313000);assert(evidencecsv::rows()==2);evidencecsv::stop();}
 // A new session starts a fresh, complete file even though the records did not change.
 run("/WD-0011.csv",400000);assert(evidencecsv::rows()==2);evidencecsv::stop();
 {auto& t=files["/WD-0011-clients.csv"];assert(t.find("\r\n")!=std::string::npos);}
 const char* nested="/umbravox/sessions/WD-20261031-123456/wifi.csv";
 run(nested,130000,2);assert(!strcmp(evidencecsv::activeFile(0),"/umbravox/sessions/WD-20261031-123456/clients.csv"));
 assert(!strcmp(evidencecsv::activeFile(1),"/umbravox/sessions/WD-20261031-123456/probes.csv"));
 run("",130002,1);assert(evidencecsv::activeFile(0)[0]&&evidencecsv::activeFile(1)[0]);
 run("",130003,1);assert(evidencecsv::activeFile(1)[0]);run("",130004,1);assert(!evidencecsv::activeFile(0)[0]&&!evidencecsv::activeFile(1)[0]);
 assert(files.count("/umbravox/sessions/WD-20261031-123456/clients.csv")&&files.count("/umbravox/sessions/WD-20261031-123456/probes.csv"));
 // Rollover at the file limit: the export continues in -2, -3 ... instead of stopping, never splits a row, and every
 // part starts with the header. The limit is lowered for the test (the device keeps 16 MiB).
 {
  auto sweep=[&](const char* nm,uint32_t at){c.frames++;c.last.at=at;p.count++;p.last.at=at;run(nm,at+500);};
  auto dataRows=[&](const std::string& t){size_t n=0;for(char ch:t)n+=ch=='\n';return n-1;}; // minus the header line
  evidencecsv::setFileCap(2000);evidencecsv::takeError();
  for(unsigned k=0;k<30;k++)sweep("/WD-0030.csv",1000000+k*13000);
  evidencecsv::stop();
  assert(!evidencecsv::takeError());
  assert(files.count("/WD-0030-probes.csv")&&files.count("/WD-0030-probes-2.csv")&&files.count("/WD-0030-clients-2.csv"));
  size_t probeRows=0,clientRows=0;
  for(unsigned part=1;part<=MAXP;part++){
   std::string pn=part==1?"/WD-0030-probes.csv":"/WD-0030-probes-"+std::to_string(part)+".csv",cn=part==1?"/WD-0030-clients.csv":"/WD-0030-clients-"+std::to_string(part)+".csv";
   if(files.count(pn)){assert(files[pn].size()<=2000&&files[pn].rfind("snapshot_ms,",0)==0);probeRows+=dataRows(files[pn]);}
   if(files.count(cn)){assert(files[cn].size()<=2000&&files[cn].rfind("snapshot_ms,",0)==0);clientRows+=dataRows(files[cn]);}
  }
  assert(probeRows==30&&clientRows==30); // one row per sweep and kind, none lost or duplicated across parts
  // With room for one row per part, the parts run out and the export stops with the limit message.
  evidencecsv::setFileCap(400);
  for(unsigned k=0;k<40&&!evidencecsv::takeError();k++)sweep("/WD-0031.csv",2000000+k*13000);
  assert(std::string(evidencecsv::failReason()).find("16 MiB limit")!=std::string::npos);
  assert(!files.count("/WD-0031-probes-9.csv"));
  evidencecsv::stop();evidencecsv::setFileCap(16u*1024*1024);
 }
 // The change signatures come from the injected allocator (PSRAM on the device): 768 slots x 4 bytes.
 // If it cannot allocate, the export still works and simply re-writes every record each sweep.
 c.used=true;c.last.at=499000;c.hasRssi=false;p.used=true;p.last.at=499000;
 evidencecsv::setAllocator(testAlloc);allocFail=true;allocCalls=0;
 run("/WD-0020.csv",500000);assert(evidencecsv::rows()==2);run("/WD-0020.csv",513000);assert(evidencecsv::rows()==4);evidencecsv::stop();
 assert(allocCalls==1&&allocBytes==768*4);
 evidencecsv::setAllocator(testAlloc);allocFail=false;allocCalls=0;
 run("/WD-0021.csv",600000);run("/WD-0021.csv",613000);assert(evidencecsv::rows()==2&&allocCalls==1&&allocBytes==768*4);evidencecsv::stop();
 evidencecsv::setAllocator(nullptr);free(testBuf);testBuf=nullptr; // drop the module's reference before freeing the test buffer
 std::ofstream("/tmp/umbravox-clients.csv")<<files["/WD-0001-clients.csv"];
 std::ofstream("/tmp/umbravox-probes.csv")<<files["/WD-0001-probes.csv"];
 std::cout<<"Evidence CSV formatting, lossless SSIDs, bounds, batching, stop, rollover and failure recovery PASS\n";
}
