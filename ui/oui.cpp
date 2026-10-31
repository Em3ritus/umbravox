#include "oui.h"
#include <cstdio>
namespace oui {
namespace {
#include "oui_data.inc"
template<size_t N> const Entry* find(const Entry (&entries)[N],uint64_t key,bool wide=false){
 size_t lo=0,hi=N;
 auto prefix=[wide](const Entry& e){return wide?(uint64_t(e.prefix)<<4)|(e.offset&15):uint64_t(e.prefix);};
 while(lo<hi){size_t mid=lo+(hi-lo)/2;if(prefix(entries[mid])<key)lo=mid+1;else hi=mid;}
 return lo<N&&prefix(entries[lo])==key?entries+lo:nullptr;
}
}
Match identify(const uint8_t mac[6]){
 if(!mac)return {};
 if(mac[0]&1)return {"GROUP ADDRESS"};
 if(mac[0]&2)return {"LOCALLY ADMINISTERED"};
 uint64_t full=0;for(int i=0;i<6;i++)full=(full<<8)|mac[i];
 if(const auto* e=find(entries36,full>>12,true))return {names+(e->offset>>5),full>>12,36,(e->offset&16)?Registry::Iab:Registry::MaS};
 if(const auto* e=find(entries28,full>>20))return {names+e->offset,full>>20,28,Registry::MaM};
 if(const auto* e=find(entries24,full>>24))return {names+e->offset,full>>24,24,Registry::MaL};
 return {};
}
const char* lookup(const uint8_t mac[6]){return identify(mac).vendor;}
const char* registryName(Registry r){switch(r){case Registry::MaL:return "MA-L";case Registry::MaM:return "MA-M";case Registry::MaS:return "MA-S";case Registry::Iab:return "IAB";default:return "NONE";}}
void formatPrefix(const Match& m,char* out,size_t cap){
 if(!m.bits){snprintf(out,cap,"NO REGISTERED PREFIX");return;}
 snprintf(out,cap,"%0*llX/%u (%s)",m.bits/4,(unsigned long long)m.prefix,unsigned(m.bits),registryName(m.registry));
}
const char* snapshotDate(){return datasetDate;}
}
