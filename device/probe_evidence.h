// Pure bounded management-frame evidence. Input excludes FCS. Caller serializes
// the queue; table/correlation work is main-loop only. No injected traffic.
#pragma once
#include "clients.h"
namespace evidence {
constexpr int PROBES=512, APS=128, QUEUE=64, DRAIN=8;
constexpr uint32_t EXCHANGE_MS=5000, HIDDEN_MS=300000;
enum Kind:uint8_t { Probe, Advertisement, Request, Response };
enum Source:uint8_t { Unknown, ApName, Association };
struct Event {
 uint8_t sta[6]{},ap[6]{},name[32]{},len=0,channel=0,sub=0;
 int8_t rssi=0;uint32_t at=0;Kind kind=Probe;bool success=false;
};
inline bool tags(const uint8_t* f,size_t n,size_t pos,Event& e){
 bool found=false;
 while(pos<n){
  if(n-pos<2)return false;const uint8_t id=f[pos++],len=f[pos++];
  if(len>n-pos)return false;
  if(id==0){if(found||len>32)return false;found=true;e.len=len;if(len)memcpy(e.name,f+pos,len);}
  pos+=len;
 }
 return found;
}
inline bool hasName(const Event& e){for(int i=0;i<e.len;i++)if(e.name[i])return true;return false;}
inline void display(char* out,const uint8_t* name,uint8_t len){for(int i=0;i<len;i++)out[i]=name[i]>=32&&name[i]<127?char(name[i]):'.';out[len]=0;}
inline bool parse(const uint8_t* f,size_t n,int8_t rssi,uint8_t ch,uint32_t now,Event& out){
 if(!f||n<24||n>4096||(f[0]&15)||(f[1]&0xc7)||(f[22]&15))return false;
 Event e;e.sub=f[0]>>4;e.rssi=rssi;e.channel=ch;e.at=now;
 if(!clients::unicast(f+10))return false;
 memcpy(e.sta,f+10,6);
 if(e.sub==4){e.kind=Probe;if(!tags(f,n,24,e))return false;}
 else if(e.sub==5||e.sub==8){
  if(n<36||!clients::equal(f+10,f+16))return false;
  e.kind=Advertisement;memcpy(e.ap,f+16,6);if(!tags(f,n,36,e))return false;
  if(!hasName(e))e.len=0;
 }else if(e.sub==0||e.sub==2){
  const size_t fixed=e.sub==0?28:34;
  if(n<fixed||!clients::unicast(f+4)||!clients::equal(f+4,f+16)||clients::equal(f+4,f+10))return false;
  e.kind=Request;memcpy(e.ap,f+4,6);if(!tags(f,n,fixed,e)||!hasName(e))return false;
 }else if(e.sub==1||e.sub==3){
  if(n<30||!clients::unicast(f+4)||!clients::equal(f+10,f+16)||clients::equal(f+4,f+10))return false;
  e.kind=Response;memcpy(e.ap,f+10,6);memcpy(e.sta,f+4,6);e.success=f[26]==0&&f[27]==0;
 }else return false;
 out=e;return true;
}
// Which parsed frames are worth a queue slot. A named beacon is a visible AP
// (already in the main table) and observe() ignores it, so on a busy channel they
// would crowd out the probe requests and association exchanges this feature is
// for. Hidden-AP beacons, probe responses (which can reveal a hidden name), probe
// requests and association frames are all kept.
inline bool wanted(const Event& e){return !(e.kind==Advertisement&&e.sub==8&&e.len);}
struct Ring {
 Event rows[QUEUE]{};uint16_t head=0,size=0;uint32_t dropped=0,probes=0;
 bool push(const Event& e){
  if(e.kind==Probe&&probes!=UINT32_MAX)probes++;
  if(size==QUEUE){if(dropped!=UINT32_MAX)dropped++;return false;}
  rows[(head+size)%QUEUE]=e;size++;return true;
 }
 bool pop(Event& e){if(!size)return false;e=rows[head];head=(head+1)%QUEUE;size--;return true;}
};
struct ProbeRow {Event last{};uint32_t first=0,count=0;bool used=false;};
struct Ap {
 uint8_t mac[6]{},name[32]{},len=0;Source source=Unknown;
 bool used=false,hidden=false,pending=false,conflict=false;uint32_t at=0,hiddenAt=0;
 Event request{};
};
struct Store {
 ProbeRow probes[PROBES]{};Ap aps[APS]{};mutable uint16_t order[PROBES]{};uint16_t count=0;uint32_t evicted=0,conflicts=0;
 Ap* find(const uint8_t* mac){for(auto& a:aps)if(a.used&&clients::equal(a.mac,mac))return &a;return nullptr;}
 const Ap* find(const uint8_t* mac)const{return const_cast<Store*>(this)->find(mac);}
 Ap& acquire(const Event& e){
  if(auto* a=find(e.ap)){if(uint32_t(e.at-a->at)>=HIDDEN_MS){*a={};a->used=true;memcpy(a->mac,e.ap,6);}a->at=e.at;return *a;}
  int slot=-1,old=0;uint32_t age=0;
  for(int i=0;i<APS;i++){if(!aps[i].used){slot=i;break;}if(uint32_t(e.at-aps[i].at)>=age){age=e.at-aps[i].at;old=i;}}
  auto& a=aps[slot<0?old:slot];a={};a.used=true;a.at=e.at;memcpy(a.mac,e.ap,6);return a;
 }
 bool name(Ap& a,const Event& e,Source src){
  if(a.len){if(a.len!=e.len||memcmp(a.name,e.name,e.len)){if(conflicts!=UINT32_MAX)conflicts++;a.conflict=true;return false;}if(src==ApName)a.source=ApName;return false;}
  memcpy(a.name,e.name,e.len);a.len=e.len;a.source=src;return a.hidden;
 }
 // Returns a resolved BSSID only on new evidence; caller may fill an existing
 // hidden AP's name, never synthesize an AP or its RSSI/location/capture count.
 bool observe(const Event& e){
  if(e.kind==Probe){
   int slot=-1,old=0;uint32_t age=0;
   for(int i=0;i<PROBES;i++){
    auto& r=probes[i];
    if(r.used&&clients::equal(r.last.sta,e.sta)&&r.last.len==e.len&&!memcmp(r.last.name,e.name,e.len)){slot=i;break;}
    if(!r.used&&slot<0)slot=i;
    if(r.used&&uint32_t(e.at-r.last.at)>=age){age=e.at-r.last.at;old=i;}
   }
   if(slot<0){slot=old;probes[slot]={};if(evicted!=UINT32_MAX)evicted++;}
   auto& r=probes[slot];if(!r.used){r.used=true;r.first=e.at;if(count<PROBES)count++;}
   r.last=e;if(r.count!=UINT32_MAX)r.count++;return false;
  }
  if(e.kind==Advertisement){
   if(!e.len){auto& a=acquire(e);a.hidden=true;a.hiddenAt=e.at;return a.len!=0;}
   auto* a=find(e.ap);if(!a)return false;a->at=e.at;return name(*a,e,ApName);
  }
  auto* a=find(e.ap);if(!a||!a->hidden||uint32_t(e.at-a->hiddenAt)>=HIDDEN_MS)return false;
  if(e.kind==Request){a->request=e;a->pending=true;return false;}
  if(!a->pending||!clients::equal(a->request.sta,e.sta)||e.sub!=a->request.sub+1||!e.channel||e.channel!=a->request.channel)return false;
  a->pending=false;
  if(!e.success||uint32_t(e.at-a->request.at)>=EXCHANGE_MS)return false;
  return name(*a,a->request,Association);
 }
 bool resolve(const uint8_t* mac,const char* current,char* out)const{
  const auto* a=find(mac);if(!a||!a->hidden||!a->len||a->conflict||!current||current[0])return false;
  display(out,a->name,a->len);return true;
 }
 int page(uint32_t now,int& offset,int limit,uint16_t* out,uint16_t& askers)const{
  uint16_t* ids=order;int n=0;for(int i=0;i<PROBES;i++)if(probes[i].used)ids[n++]=i;
  std::sort(ids,ids+n,[&](int a,int b){const int c=memcmp(probes[a].last.sta,probes[b].last.sta,6);return c?c<0:a<b;});
  askers=0;for(int i=0;i<n;i++)if(!i||!clients::equal(probes[ids[i]].last.sta,probes[ids[i-1]].last.sta))askers++;
  std::sort(ids,ids+n,[&](int a,int b){const uint32_t aa=now-probes[a].last.at,ba=now-probes[b].last.at;return aa!=ba?aa<ba:a<b;});
  if(offset<0)offset=0;if(offset>=n)offset=n?((n-1)/limit)*limit:0;
  for(int i=0;i<limit&&offset+i<n;i++)out[i]=ids[offset+i];return n;
 }
};
struct Storage {Ring queue;Store store;};
}
