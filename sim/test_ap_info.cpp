#include "../ui/ap_info.h"
#include "../ui/oui.h"
#include <vector>
#include <cassert>
#include <cstring>
#include <cstdio>
int main(){
 std::vector<uint8_t> b(12);b[8]=100;b[10]=0x31;b[11]=4;
 auto ie=[&](int id,std::vector<uint8_t> v){b.push_back(id);b.push_back(v.size());b.insert(b.end(),v.begin(),v.end());};
 ie(7,{'U','S',' '});ie(45,std::vector<uint8_t>(26));
 std::vector<uint8_t> ht(22);ht[1]=5;ie(61,ht);ie(191,std::vector<uint8_t>(12));ie(192,{1,42,0,0,0});
 std::vector<uint8_t> he(22);he[0]=35;ie(255,he);ie(221,{0,0x50,0xf2,4,0x10,0x57,0,1,1});
 auto a=ui::parseApInfo(b.data(),b.size());assert(a.valid&&a.width==80&&a.phy==7&&a.beaconTu==100&&a.capabilities==0x431&&a.wps&&a.wpsLocked==1&&!strcmp(a.country,"US"));
 assert(!ui::parseApInfo(nullptr,0).valid);
 for(size_t n=0;n<12;n++)assert(!ui::parseApInfo(b.data(),n).valid);
 b.push_back(7);assert(!ui::parseApInfo(b.data(),b.size()).valid);b.pop_back();
 b.back()=2;assert(ui::parseApInfo(b.data(),b.size()).wpsLocked==-1);
 b.pop_back();assert(!ui::parseApInfo(b.data(),b.size()).valid);
 uint8_t mac[6]={0x00,0x00,0x0c};assert(strstr(oui::lookup(mac),"Cisco"));
 mac[0]=2;assert(!strcmp(oui::lookup(mac),"LOCALLY ADMINISTERED"));mac[0]=1;assert(!strcmp(oui::lookup(mac),"GROUP ADDRESS"));
 mac[0]=8;mac[1]=0;mac[2]=0x30;assert(!strcmp(oui::lookup(mac),"MULTIPLE IEEE REGISTRANTS"));
 puts("AP metadata: bounded parsing, capabilities, malformed WPS/IEs and OUI identity PASS");
}
