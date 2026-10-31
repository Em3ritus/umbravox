#include "../device/probe_evidence.h"
#include <cassert>
#include <vector>
#include <cstdio>
using namespace evidence;
uint8_t ap[6]={0x10,2,3,4,5,6},sta[6]={0x22,3,4,5,6,7};
std::vector<uint8_t> frame(int sub,const std::vector<uint8_t>& name={}){
 const int fixed=sub==4?24:sub==8||sub==5?36:sub==0?28:sub==2?34:30;
 std::vector<uint8_t> f(fixed);f[0]=sub<<4;
 memcpy(f.data()+4,sub==8||sub==5||sub==1||sub==3?sta:ap,6);
 memcpy(f.data()+10,sub==8||sub==5||sub==1||sub==3?ap:sta,6);memcpy(f.data()+16,ap,6);
 if(sub!=1&&sub!=3){f.push_back(7);f.push_back(1);f.push_back('U');f.push_back(0);f.push_back(name.size());f.insert(f.end(),name.begin(),name.end());}
 return f;
}
Event event(int sub,const std::vector<uint8_t>& name,uint32_t at){auto f=frame(sub,name);Event e;assert(parse(f.data(),f.size(),-51,48,at,e));return e;}
int main(){
 Event e;std::vector<uint8_t> full(32,'X');auto p=frame(4,full);assert(parse(p.data(),p.size(),-51,48,123,e)&&e.len==32&&e.channel==48);char displayName[33];display(displayName,e.name,e.len);assert(strlen(displayName)==32);
 for(size_t n=0;n<p.size();n++)assert(!parse(p.data(),n,-51,6,0,e));
 p.push_back(0);p.push_back(0);assert(!parse(p.data(),p.size(),0,0,0,e));
 p=frame(4,std::vector<uint8_t>(33,'X'));assert(!parse(p.data(),p.size(),0,0,0,e));
 for(int flags:{1,2,4,64,128}){p=frame(4,{'A'});p[1]=flags;assert(!parse(p.data(),p.size(),0,0,0,e));}
 p=frame(4,{'A'});p[10]|=1;assert(!parse(p.data(),p.size(),0,0,0,e));
 p=frame(8,{'A'});p[16]^=2;assert(!parse(p.data(),p.size(),0,0,0,e));
 Store s;auto wild=event(4,{},0);s.observe(wild);auto named=event(4,full,0);s.observe(named);s.observe(named);assert(s.count==2);
 uint16_t ids[6],askers=0;int offset=0;assert(s.page(1,offset,6,ids,askers)==2&&askers==1);assert(ids[0]!=ids[1]); // equal timestamps retained
 auto binary=event(4,{1},1);s.observe(binary);binary=event(4,{2},2);s.observe(binary);assert(s.count==4); // display '.' never dedup key
 // A probe request cannot name any AP, even with matching addresses/name.
 assert(!s.find(ap));s.observe(event(8,{},100));assert(s.find(ap)->hidden&&!s.find(ap)->len);
 s.observe(event(4,{'S'},101));assert(!s.find(ap)->len);
 // Association request alone, failed response, wrong peer/type and deadline.
 s.observe(event(0,{'S'},200));assert(!s.find(ap)->len);auto response=event(1,{},201);response.success=false;assert(!s.observe(response));response.success=true;assert(!s.observe(response));
 s.observe(event(0,{'S'},300));response=event(1,{},301);response.sta[5]^=1;assert(!s.observe(response));response=event(1,{},302);response.channel=6;assert(!s.observe(response));response=event(1,{},302);response.ap[5]^=1;assert(!s.observe(response));response=event(3,{},302);assert(!s.observe(response));response=event(1,{},5300);assert(!s.observe(response));assert(!s.find(ap)->len);
 s.observe(event(0,{'S'},6000));response=event(1,{},10999);assert(s.observe(response)&&s.find(ap)->source==Association&&s.find(ap)->len==1);
 assert(!s.observe(response)); // consume matching response once
 char resolved[33];assert(s.resolve(ap,"",resolved)&&!strcmp(resolved,"S"));assert(!s.resolve(ap,"KNOWN",resolved));uint8_t missing[6]={0x44};assert(!s.resolve(missing,"",resolved));
 // AP confirms same name (stronger source), conflicting name never overwrites.
 s.observe(event(5,{'S'},11000));assert(s.find(ap)->source==ApName);
 s.observe(event(5,{'T'},11001));assert(s.find(ap)->conflict&&s.conflicts==1&&s.find(ap)->name[0]=='S');assert(!s.resolve(ap,"",resolved));
 // AP-origin resolution accepts reordered SSID tags and zero-filled hidden tags.
 Store a;a.observe(event(8,{0,0,0},0));assert(a.find(ap)->hidden);assert(a.observe(event(5,full,1))&&a.find(ap)->source==ApName);
 Store noHidden;noHidden.observe(event(0,{'X'},0));assert(!noHidden.observe(event(1,{},1))&&!noHidden.find(ap));
 Store stale;stale.observe(event(8,{},0));stale.observe(event(0,{'X'},HIDDEN_MS));assert(!stale.observe(event(1,{},HIDDEN_MS+1))&&!stale.find(ap)->len);
 // Request/response correlation is wrap-safe.
 Store wrap;wrap.observe(event(8,{},0xfffff000));wrap.observe(event(2,{'R'},0xfffffff0));assert(wrap.observe(event(3,{},20)));
 Store bounded;for(int i=0;i<PROBES+1;i++){auto q=event(4,{},i);q.sta[4]=i>>8;q.sta[5]=i;bounded.observe(q);}assert(bounded.count==PROBES&&bounded.evicted==1);offset=999;assert(bounded.page(600,offset,6,ids,askers)==PROBES&&offset<PROBES);
 Ring ring;for(int i=0;i<QUEUE;i++)assert(ring.push(wild));assert(!ring.push(wild)&&ring.dropped==1&&ring.probes==QUEUE+1);for(int i=0;i<QUEUE;i++)assert(ring.pop(e));assert(!ring.pop(e));
 // Many malformed IE lengths/short frames under sanitizers.
 for(int sub:{0,1,2,3,4,5,8})for(int n=0;n<256;n++){auto f=frame(sub,{'A'});for(size_t at=24;at<f.size();at++){auto g=f;g[at]=n;parse(g.data(),g.size(),-50,48,1,e);}}
 printf("Probe parsing, raw identity, equal timestamps, association correlation, AP evidence, conflicts, capacity and wrap PASS (%zu bytes PSRAM)\n",sizeof(Storage));
}
