#include "../device/pcap.h"
#include "../device/sdlog.h"
#include <map>
#include <string>
#include <vector>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <thread>
#include <atomic>
#include <fstream>
namespace {
std::map<std::string,std::string> files;
std::string current;
bool card=true,shortWrite=false,closeFail=false,rollbackFail=false,openFail=false;
unsigned writes=0,opens=0,closes=0,maxWrite=0;
uint32_t now=0;
void step(uint32_t ms=10){now+=ms;const unsigned w=writes;pcap::service(now);assert(writes-w<=1);}
void drain(){pcap::stop();unsigned loops=0;while(pcap::pending()){step();assert(++loops<100);}}
void start(){assert(pcap::start());assert(pcap::written()==0&&pcap::dropped()==0&&pcap::batches()==0&&pcap::peakQueue()==0);}
}
namespace sdlog {
bool present(){return card;}
bool exists(const char* f){return files.count(f);}
bool openAppend(const char* f,uint32_t& size){if(openFail)return false;assert(current.empty());++opens;current=f;size=files[f].size();return true;}
bool writeAppend(const char* p,uint32_t n){++writes;if(n>maxWrite)maxWrite=n;assert(!current.empty());files[current].append(p,shortWrite?n/2:n);return !shortWrite;}
bool closeAppend(){++closes;current.clear();return !closeFail;}
bool truncateTo(const char* f,uint32_t n){if(rollbackFail)return false;files[f].resize(n);return true;}
}
static uint32_t le(const std::string& s,size_t p){assert(p+4<=s.size());return uint8_t(s[p])|uint32_t(uint8_t(s[p+1]))<<8|uint32_t(uint8_t(s[p+2]))<<16|uint32_t(uint8_t(s[p+3]))<<24;}
static unsigned records(const std::string& f){
 assert(f.size()>=24&&le(f,0)==0xa1b2c3d4&&le(f,16)==1536&&le(f,20)==105);
 size_t off=24;unsigned n=0;
 while(off<f.size()){assert(f.size()-off>=16);auto cap=le(f,off+8);assert(cap<=1536&&cap<=le(f,off+12)&&le(f,off+4)<1000000);off+=16+cap;assert(off<=f.size());++n;}
 assert(off==f.size());return n;
}
int main(){
 card=false;assert(!pcap::start()&&!pcap::active());card=true;
 start();const std::string name=pcap::fileName();auto& f=files[name];assert(records(f)==0);
 std::vector<uint8_t> packet(2000,0);packet[0]=0x80;unsigned before=writes;
 for(int i=0;i<70;i++){packet[1]=i;pcap::receive(packet.data(),packet.size(),12345678+i);}
 assert(writes==before&&pcap::dropped()==6&&pcap::peakQueue()==64);
 // Full frames: one contiguous write for five records, never >8 KiB.
 step();assert(pcap::written()==5&&pcap::batches()==1&&writes==before+1&&f.size()==24+5*(16+1536));
 drain();assert(pcap::written()==64&&pcap::dropped()==6&&records(f)==64&&!pcap::active());
 size_t off=24;for(int i=0;i<64;i++){assert(le(f,off)==12&&le(f,off+4)==345678u+i&&le(f,off+12)==2000);assert(uint8_t(f[off+17])==i);off+=16+1536;}
 assert(maxWrite<=8192);
 // Identical 64-frame burst of small records: one flush vs baseline's 32.
 start();assert(name!=pcap::fileName());before=writes;const auto beforeOpen=opens,beforeClose=closes;
 for(int i=0;i<64;i++)pcap::receive(packet.data(),100,1000000+i);
 for(int i=0;i<4;i++)step();assert(writes==before); // 16 records staged per pass.
 drain();assert(pcap::written()==64&&pcap::dropped()==0&&pcap::batches()==1);
 assert(writes==before+1&&opens==beforeOpen+1&&closes==beforeClose+1);
 assert(records(files[pcap::fileName()])==64);
 puts("Small-frame fixture: 64 frames, 1 append/flush instead of baseline 32, 0 drops");
 // A pass removes exactly sixteen small records, not the entire queue.
 start();for(int i=0;i<64;i++)pcap::receive(packet.data(),100,1);
 step();for(int i=0;i<16;i++)pcap::receive(packet.data(),100,1);assert(pcap::dropped()==0);
 pcap::receive(packet.data(),100,1);assert(pcap::dropped()==1);drain();assert(pcap::written()==80);
 // Sparse traffic flush deadline, including uint32 uptime wrap.
 start();now=0xffffff80u;pcap::receive(packet.data(),100,1);pcap::service(now);before=writes;
 step(249);assert(writes==before&&pcap::written()==0);step(1);assert(writes==before+1&&pcap::written()==1);drain();
 // Stop flushes immediately, even with an unexpired staged tail.
 start();pcap::receive(packet.data(),100,1);step();assert(pcap::written()==0);drain();assert(pcap::written()==1&&!strcmp(pcap::status(),"OFF"));
 // Every failure accounts for the staged batch AND any queued remainder.
 for(int failure=0;failure<4;failure++){
  start();const std::string path=pcap::fileName();
  for(int i=0;i<24;i++)pcap::receive(packet.data(),100,1);
  step();assert(pcap::written()==0); // 16 staged, eight still queued.
  if(failure==0)shortWrite=true;
  if(failure==1)closeFail=true;
  if(failure==2){shortWrite=true;rollbackFail=true;}
  if(failure==3)openFail=true;
  step(250);assert(!pcap::active());step();assert(!pcap::pending()&&pcap::written()==0&&pcap::dropped()==24);
  if(failure==2)assert(!strcmp(pcap::status(),"DAMAGED FILE"));else assert(files[path].size()==24);
  shortWrite=closeFail=rollbackFail=openFail=false;
 }
 // Error on an immediate full batch leaves a queue that must also be counted.
 start();for(int i=0;i<64;i++)pcap::receive(packet.data(),2000,1);
 shortWrite=true;step();step();assert(pcap::dropped()==64&&!pcap::pending());shortWrite=false;
 openFail=true;assert(!pcap::start());openFail=false;
 start();pcap::receive(nullptr,20,1);pcap::receive(packet.data(),9,1);drain();assert(pcap::written()==0);
 // Size cap always leaves a whole-record file.
 start();while(pcap::active()){for(int i=0;i<5;i++)pcap::receive(packet.data(),2000,1);step();}
 while(pcap::pending())step();assert(files[pcap::fileName()].size()<=32*1024*1024&&!strcmp(pcap::status(),"32MB LIMIT"));
 assert(records(files[pcap::fileName()])==pcap::written());
 // Producer never calls SD, and all offered frames are written or counted lost.
 start();std::atomic<bool> done{false};
 std::thread producer([&]{for(int i=0;i<10000;i++)pcap::receive(packet.data(),100,1000000+i);done=true;});
 while(!done.load()){step();std::this_thread::yield();}producer.join();drain();
 assert(pcap::written()+pcap::dropped()==10000&&records(files[pcap::fileName()])==pcap::written());
 // Valid beacon fixture for independent tcpdump decoding.
 start();std::vector<uint8_t> beacon(49,0);beacon[0]=0x80;for(int i=4;i<10;i++)beacon[i]=255;
 beacon[12]=beacon[18]=0x0c;beacon[32]=100;beacon[34]=1;beacon[37]=11;memcpy(beacon.data()+38,"TEST-SURVEY",11);
 pcap::receive(beacon.data(),beacon.size(),12345678);drain();
 const auto& capture=files[pcap::fileName()];std::ofstream out("/tmp/umbravox-pcap-test.pcap",std::ios::binary);out.write(capture.data(),capture.size());out.close();
 assert(current.empty()&&maxWrite<=8192);
 puts("PCAP: binary format, batch bounds, deadlines/wrap, overflow, stop/drain, I/O failures, rollback, size cap, concurrency PASS");
}
