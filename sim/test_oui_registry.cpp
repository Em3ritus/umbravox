#include "../ui/oui.h"
#include <cassert>
#include <cstdio>
#include <cstring>
int main(int argc,char**){
 if(argc>1){unsigned long long n;while(scanf("%llx",&n)==1){uint8_t mac[6];for(int i=5;i>=0;i--){mac[i]=n&255;n>>=8;}auto m=oui::identify(mac);printf("%u\t%s\t%s\n",m.bits,oui::registryName(m.registry),m.vendor);}return 0;}
 struct Case{uint64_t mac;int bits;oui::Registry registry;const char* vendor;};
 Case cases[]={{0xb41e52000000,24,oui::Registry::MaL,"Flock Safety"},{0x003044123456,24,oui::Registry::MaL,"CradlePoint, Inc"},{0xc85ce2700000,28,oui::Registry::MaM,"SYNERGY SYSTEMS AND SOLUTIONS"},{0x8c1f64afa000,36,oui::Registry::MaS,"DATA ELECTRONIC DEVICES, INC"},{0x40d8550d7000,36,oui::Registry::Iab,"Avant Technologies"},{0x8c1f64574000,36,oui::Registry::MaS,"Flock Audio Inc."}};
 for(auto c:cases){uint8_t mac[6];for(int i=5;i>=0;i--){mac[i]=c.mac&255;c.mac>>=8;}auto m=oui::identify(mac);assert(m.bits==c.bits&&m.registry==c.registry&&!strcmp(m.vendor,c.vendor));char prefix[64];oui::formatPrefix(m,prefix,sizeof prefix);assert(strstr(prefix,oui::registryName(m.registry)));}
 uint8_t local[]={2,0,0,0,0,0},group[]={1,0,0,0,0,0};assert(!oui::identify(local).bits&&!oui::identify(group).bits&&!oui::identify(nullptr).bits);
 puts("OUI registries, longest-prefix and non-public identity PASS");
}
