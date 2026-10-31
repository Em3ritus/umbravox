#include "pcap.h"
#include "sdlog.h"
#include <atomic>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#ifdef ARDUINO
#include <esp_heap_caps.h>
#endif
namespace pcap {
namespace {
constexpr unsigned SNAP=1536,N=16,LIMIT=32*1024*1024;
struct Record {uint8_t data[16+SNAP];uint16_t size;};
Record* queue=nullptr;
unsigned head=0,tail=0,count=0;
std::atomic_flag lock=ATOMIC_FLAG_INIT;
std::atomic<bool> enabled{false};
std::atomic<uint32_t> lost{0};
uint32_t saved=0,bytes=0;
bool draining=false,broken=false;
const char* state="OFF";
char path[40]{};
void le(uint8_t* p,uint32_t v){for(int i=0;i<4;i++)p[i]=uint8_t(v>>(8*i));}
// Never wait behind main-loop bookkeeping from the Wi-Fi callback.
bool take(){return !lock.test_and_set(std::memory_order_acquire);}
void give(){lock.clear(std::memory_order_release);}
void fail(const char* why){enabled=false;draining=true;state=why;broken=true;}
}
bool active(){return enabled.load();}
bool pending(){return draining;}
uint32_t written(){return saved;}
uint32_t dropped(){return lost.load();}
const char* status(){return state;}
const char* fileName(){return path;}
bool start(){
 if(active() || draining)return false;
 if(!sdlog::present()){state="NO CARD";return false;}
 if(!queue){
#ifdef ARDUINO
  queue=static_cast<Record*>(heap_caps_malloc(sizeof(Record)*N,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
#else
  queue=static_cast<Record*>(malloc(sizeof(Record)*N));
#endif
  if(!queue){state="NO PSRAM";return false;}
 }
 // New files only. Bounded name search; never overwrite an existing capture.
 static unsigned nextName=1;bool found=false;
 for(unsigned attempt=0;attempt<64 && nextName<=9999;++attempt){snprintf(path,sizeof path,"/capture%04u.pcap",nextName++);if(!sdlog::exists(path)){found=true;break;}}
 if(!found){state="NAME LIMIT";return false;}
 uint32_t before=0;if(!sdlog::openAppend(path,before)){state="OPEN ERROR";return false;}
 uint8_t header[24]{};le(header,0xa1b2c3d4);header[4]=2;header[6]=4;le(header+16,SNAP);le(header+20,105); // LINKTYPE_IEEE802_11, no FCS
 bool ok=before==0 && sdlog::writeAppend(reinterpret_cast<char*>(header),sizeof header);
 const bool closed=sdlog::closeAppend();
 if(!ok||!closed){sdlog::truncateTo(path,before);state="WRITE ERROR";return false;}
 if(!take()){state="BUSY";return false;}
 head=tail=count=0;lost=0;saved=0;bytes=24;broken=false;draining=false;state="ON";enabled=true;give();return true;
}
void stop(){enabled=false;if(!broken)state="SAVING";draining=true;}
void receive(const uint8_t* frame,size_t len,uint64_t us){
 if(!enabled.load() || !frame || len<10)return;
 if(!take()){++lost;return;}
 if(!enabled.load()){give();return;}
 if(count==N){++lost;give();return;}
 auto& r=queue[head];const unsigned cap=len>SNAP?SNAP:len;
 le(r.data,uint32_t(us/1000000));le(r.data+4,uint32_t(us%1000000));le(r.data+8,cap);le(r.data+12,len);
 memcpy(r.data+16,frame,cap);r.size=16+cap;head=(head+1)%N;++count;give();
}
void service(){
 if(!active()&&!draining)return;
 static Record batch[2];unsigned n=0;
 if(!take())return;
 if(broken){lost+=count;count=0;}
 while(n<2 && count){batch[n++]=queue[tail];tail=(tail+1)%N;--count;}
 if(!enabled.load() && !count && !n){draining=false;if(!broken)state="OFF";give();return;}
 give();if(!n)return;
 unsigned add=0;for(unsigned i=0;i<n;i++)add+=batch[i].size;
 if(bytes+add>LIMIT){lost+=n;fail("32MB LIMIT");return;}
 uint32_t before=0;
 if(!sdlog::openAppend(path,before)){lost+=n;fail("OPEN ERROR");return;}
 bool ok=before==bytes;
 for(unsigned i=0;ok&&i<n;i++)ok=sdlog::writeAppend(reinterpret_cast<char*>(batch[i].data),batch[i].size);
 const bool closed=sdlog::closeAppend();
 if(!ok||!closed){lost+=n;const bool restored=sdlog::truncateTo(path,before);fail(restored?"WRITE ERROR":"DAMAGED FILE");return;}
 bytes+=add;saved+=n;
}
}
