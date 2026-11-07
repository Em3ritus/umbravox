// Bounded, allocation-free observed AP/station relationships. No payload decoding.
// Parser input excludes FCS; firmware rejects receive errors before calling it.
#pragma once
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <algorithm>
namespace clients {
constexpr int CAPACITY=256, QUEUE=64, DRAIN=8;
constexpr uint32_t RETAIN_MS=300000; // five minutes since last observation
constexpr uint8_t DATA=1, ASSOC=2;
inline bool equal(const uint8_t* a,const uint8_t* b){return !memcmp(a,b,6);}
inline bool unicast(const uint8_t* a){uint8_t any=0;for(int i=0;i<6;i++)any|=a[i];return any && !(a[0]&1);}
struct Event {uint8_t ap[6]{},sta[6]{};uint32_t at=0;int8_t rssi=0;uint8_t channel=0,evidence=0;bool stationTx=false;};
inline bool parse(const uint8_t* f,size_t n,int8_t rssi,uint8_t channel,uint32_t now,Event& out){
 if(!f || n<24 || (f[0]&3))return false;
 const uint8_t type=(f[0]>>2)&3,sub=f[0]>>4,ds=f[1]&3;
 const uint8_t *ap=nullptr,*sta=nullptr;bool tx=false;uint8_t evidence=0;
 if(type==2){
  // Ignore IBSS/TDLS and four-address WDS: their addresses do not establish
  // this infrastructure AP/station relationship. QoS/HT headers must be whole.
  if(ds!=1 && ds!=2)return false;
  const size_t header=24+((sub&8)?2:0)+(((sub&8)&&(f[1]&0x80))?4:0);
  if(n<header || sub==13)return false;
  ap=f+(ds==1?4:10);sta=f+(ds==1?10:4);tx=ds==1;evidence=DATA;
 }else if(type==0 && (sub==1 || sub==3)){
  // A successful association/reassociation response, not merely a request.
  if(ds || (f[1]&0xc4) || (f[22]&15) || n<30 || f[26] || f[27] || !equal(f+10,f+16))return false;
  ap=f+10;sta=f+4;evidence=ASSOC;
 }else return false;
 if(!unicast(ap)||!unicast(sta)||equal(ap,sta))return false;
 out={};memcpy(out.ap,ap,6);memcpy(out.sta,sta,6);out.at=now;out.rssi=rssi;out.channel=channel;out.evidence=evidence;out.stationTx=tx;return true;
}
// Caller serializes push/pop. Firmware uses its existing callback mutex only
// for one queue operation, never a table scan or sort.
struct Ring {
 Event events[QUEUE]{};uint16_t head=0,size=0,peak=0;uint32_t drops=0;
 bool push(const Event& e){if(size==QUEUE){if(drops!=UINT32_MAX)drops++;return false;}events[(head+size)%QUEUE]=e;size++;if(size>peak)peak=size;return true;}
 bool pop(Event& e){if(!size)return false;e=events[head];head=(head+1)%QUEUE;size--;return true;}
};
struct Record {
 Event last{};uint32_t first=0,frames=0,rssiAt=0;int8_t stationRssi=0;uint8_t evidence=0;bool used=false,hasRssi=false;
};
struct Table {
 Record rows[CAPACITY]{};uint32_t evictions=0;
 void observe(const Event& e){
  int slot=-1,oldest=0;uint32_t age=0;
  for(int i=0;i<CAPACITY;i++){
   auto& r=rows[i];
   if(r.used && equal(r.last.ap,e.ap) && equal(r.last.sta,e.sta)){slot=i;break;}
   if(!r.used && slot<0)slot=i;
   if(r.used && uint32_t(e.at-r.last.at)>=age){age=e.at-r.last.at;oldest=i;}
  }
  if(slot<0){slot=oldest;if(age<RETAIN_MS && evictions!=UINT32_MAX)evictions++;rows[slot]={};}
  auto& r=rows[slot];
  if(!r.used || uint32_t(e.at-r.last.at)>=RETAIN_MS){r={};r.used=true;r.first=e.at;}
  r.last=e;r.evidence|=e.evidence;if(r.frames!=UINT32_MAX)r.frames++;
  if(e.stationTx){r.stationRssi=e.rssi;r.rssiAt=e.at;r.hasRssi=true;}
 }
 // Main-loop-only snapshot: newest first, deterministic MAC tie-break. Sorting
 // indices avoids copying the whole table or allocating on the 8 KiB stack.
 int page(const uint8_t* ap,uint32_t now,int& offset,int limit,Record* out)const{
  uint16_t ids[CAPACITY];int n=0;
  for(int i=0;i<CAPACITY;i++)if(rows[i].used && equal(rows[i].last.ap,ap) && uint32_t(now-rows[i].last.at)<RETAIN_MS)ids[n++]=i;
  std::sort(ids,ids+n,[&](uint16_t ia,uint16_t ib){
   const auto& a=rows[ia];const auto& b=rows[ib];const uint32_t aa=now-a.last.at,ba=now-b.last.at;
   return aa!=ba?aa<ba:memcmp(a.last.sta,b.last.sta,6)<0;
  });
  if(limit<=0)return n;
  if(offset<0)offset=0;if(offset>=n)offset=n?((n-1)/limit)*limit:0;
  for(int i=0;i<limit && offset+i<n;i++)out[i]=rows[ids[offset+i]];
  return n;
 }
};
struct Storage {Ring queue;Table table;};
}
