#include "../device/node_queue.h"
#include <cassert>
#include <cstdio>
int main(){
 mesh::Rec q[2]{},r{};uint32_t ages[2]{};int n=0;
 r.bssid[5]=1;r.ssidLen=4;memcpy(r.ssid,"name",4);r.chan=6;r.rssi=-70;
 assert(nodequeue::put(q,ages,n,r,10)&&n==1);
 r.rssi=-40;r.ssidLen=0;
 for(int i=0;i<1000;++i)assert(nodequeue::put(q,ages,n,r,20+i));
 assert(n==1&&q[0].rssi==-40&&q[0].ssidLen==4&&ages[0]==1019);
 r.bssid[5]=2;assert(nodequeue::put(q,ages,n,r,30)&&n==2);
 r.bssid[5]=3;assert(!nodequeue::put(q,ages,n,r,40)&&n==2);
 r.bssid[5]=1;r.chan=11;assert(nodequeue::put(q,ages,n,r,50)&&n==2&&q[0].chan==11);
 assert(q[1].bssid[5]==2);puts("node queue coalescing, metadata, full queue and refresh: PASS");
}
