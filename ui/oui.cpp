#include "oui.h"
#include <stddef.h>
namespace oui {
namespace {
#include "oui_data.inc"
}
const char* lookup(const uint8_t mac[6]){
 if(!mac)return "UNKNOWN";
 if(mac[0]&1)return "GROUP ADDRESS";
 if(mac[0]&2)return "LOCALLY ADMINISTERED";
 const uint32_t key=uint32_t(mac[0])<<16|uint32_t(mac[1])<<8|mac[2];
 size_t lo=0,hi=sizeof(entries)/sizeof(entries[0]);
 while(lo<hi){size_t mid=lo+(hi-lo)/2;if(entries[mid].prefix<key)lo=mid+1;else hi=mid;}
 return lo<sizeof(entries)/sizeof(entries[0]) && entries[lo].prefix==key?names+entries[lo].offset:"UNKNOWN (MA-L)";
}
}
