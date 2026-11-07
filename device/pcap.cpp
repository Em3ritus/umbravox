#include "pcap.h"
#include "storage_paths.h"
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
constexpr unsigned SNAP=1536,N=64,BATCH_BYTES=8192,COPY_LIMIT=16,FLUSH_MS=250,LIMIT=32*1024*1024;
struct Record {uint8_t data[16+SNAP];uint16_t size;};
struct Storage {Record queue[N];uint8_t batch[BATCH_BYTES];};
Storage* storage=nullptr;
Record* queue=nullptr;
unsigned stagedBytes=0,stagedFrames=0;
uint32_t stagedAt=0,flushes=0;
std::atomic<uint32_t> highWater{0};
unsigned head=0,tail=0,count=0;
std::atomic_flag lock=ATOMIC_FLAG_INIT;
std::atomic<bool> enabled{false};
std::atomic<uint32_t> lost{0};
uint32_t saved=0,bytes=0;
bool draining=false,broken=false;
const char* state="OFF";
char path[64]{};
void le(uint8_t* p,uint32_t v){for(int i=0;i<4;i++)p[i]=uint8_t(v>>(8*i));}
// Never wait behind main-loop bookkeeping from the Wi-Fi callback.
bool take(){return !lock.test_and_set(std::memory_order_acquire);}
void give(){lock.clear(std::memory_order_release);}
void fail(const char* why){enabled=false;draining=true;state=why;broken=true;}
}
bool active(){return enabled.load();}
bool pending(){return draining;}
uint32_t written(){return saved;}
uint32_t batches(){return flushes;}
uint32_t peakQueue(){return highWater.load();}
uint32_t dropped(){return lost.load();}
const char* status(){return state;}
const char* fileName(){return path;}
bool start(){
 if(active() || draining)return false;
 if(!sdlog::present()){state="NO CARD";return false;}
 if(!queue){
#ifdef ARDUINO
  storage=static_cast<Storage*>(heap_caps_malloc(sizeof(Storage),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
#else
  storage=static_cast<Storage*>(malloc(sizeof(Storage)));
#endif
  if(!storage){state="NO PSRAM";return false;}
  queue=storage->queue;
 }
 // New files only. Bounded name search; never overwrite an existing capture.
 if(!(sdlog::isDir(storagepaths::ROOT)||sdlog::makeDir(storagepaths::ROOT)) || !(sdlog::isDir(storagepaths::CAPTURES)||sdlog::makeDir(storagepaths::CAPTURES))){state="OPEN ERROR";return false;}
 static unsigned nextName=1;bool found=false;
 for(unsigned attempt=0;attempt<64 && nextName<=9999;++attempt){snprintf(path,sizeof path,"/umbravox/captures/capture%04u.pcap",nextName++);if(!sdlog::exists(path)){found=true;break;}}
 if(!found){state="NAME LIMIT";return false;}
 uint32_t before=0;if(!sdlog::openAppend(path,before)){state="OPEN ERROR";return false;}
 uint8_t header[24]{};le(header,0xa1b2c3d4);header[4]=2;header[6]=4;le(header+16,SNAP);le(header+20,105); // LINKTYPE_IEEE802_11, no FCS
 bool ok=before==0 && sdlog::writeAppend(reinterpret_cast<char*>(header),sizeof header);
 const bool closed=sdlog::closeAppend();
 if(!ok||!closed){sdlog::truncateTo(path,before);state="WRITE ERROR";return false;}
 if(!take()){state="BUSY";return false;}
 head=tail=count=0;stagedBytes=stagedFrames=0;flushes=0;highWater=0;lost=0;saved=0;bytes=24;broken=false;draining=false;state="ON";enabled=true;give();return true;
}
void stop(){enabled=false;if(!broken)state="SAVING";draining=true;}
void receive(const uint8_t* frame,size_t len,uint64_t us){
 if(!enabled.load() || !frame || len<10)return;
 if(!take()){++lost;return;}
 if(!enabled.load()){give();return;}
 if(count==N){++lost;give();return;}
 auto& r=queue[head];const unsigned cap=len>SNAP?SNAP:len;
 le(r.data,uint32_t(us/1000000));le(r.data+4,uint32_t(us%1000000));le(r.data+8,cap);le(r.data+12,len);
 memcpy(r.data+16,frame,cap);r.size=16+cap;head=(head+1)%N;++count;if(count>highWater.load())highWater=count;give();
}
void service(uint32_t nowMs){
 if(!active()&&!draining)return;
 if(broken){
  if(!take())return;
  lost+=count+stagedFrames;count=0;stagedBytes=stagedFrames=0;draining=false;give();return;
 }
 bool full=false;
 // Release the queue lock between records, never hold it for an SD operation.
 for(unsigned i=0;i<COPY_LIMIT;i++){
  if(!take())break;
  if(!count){give();break;}
  const auto& r=queue[tail];
  if(stagedBytes+r.size>BATCH_BYTES){full=true;give();break;}
  if(!stagedFrames)stagedAt=nowMs;
  memcpy(storage->batch+stagedBytes,r.data,r.size);stagedBytes+=r.size;++stagedFrames;
  tail=(tail+1)%N;--count;give();
 }
 if(stagedBytes && (full || stagedBytes==BATCH_BYTES || !active() || uint32_t(nowMs-stagedAt)>=FLUSH_MS)){
  const unsigned n=stagedFrames,add=stagedBytes;
  // This batch leaves staging exactly once, whether written or counted as lost.
  stagedBytes=stagedFrames=0;
  if(bytes+add>LIMIT){lost+=n;fail("32MB LIMIT");return;}
  uint32_t before=0;
  if(!sdlog::openAppend(path,before)){lost+=n;fail("OPEN ERROR");return;}
  const bool ok=before==bytes && sdlog::writeAppend(reinterpret_cast<char*>(storage->batch),add);
  const bool closed=sdlog::closeAppend();
  if(!ok||!closed){lost+=n;const bool restored=sdlog::truncateTo(path,before);fail(restored?"WRITE ERROR":"DAMAGED FILE");return;}
  bytes+=add;saved+=n;++flushes;
 }
 if(!active() && !stagedFrames && take()){
  if(!count){draining=false;state="OFF";}
  give();
 }
}
}
