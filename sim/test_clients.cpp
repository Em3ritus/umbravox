#include "../device/clients.h"
#include <cassert>
#include <cstdio>
#include <array>
using namespace clients;
uint8_t ap[6]={0x10,2,3,4,5,6},sta[6]={0x22,3,4,5,6,7},other[6]={0x30,4,5,6,7,8};
std::array<uint8_t,40> frame(uint8_t fc=8,uint8_t flags=1){
 std::array<uint8_t,40> f{};f[0]=fc;f[1]=flags;memcpy(f.data()+4,ap,6);memcpy(f.data()+10,sta,6);memcpy(f.data()+16,other,6);return f;
}
int main(){
 Event e;auto f=frame();assert(parse(f.data(),24,-51,6,100,e));assert(equal(e.ap,ap)&&equal(e.sta,sta)&&e.stationTx&&e.evidence==DATA&&e.rssi==-51);
 for(size_t n=0;n<24;n++)assert(!parse(f.data(),n,-51,6,0,e));assert(!parse(nullptr,24,0,0,0,e));
 f=frame(8,2);memcpy(f.data()+4,sta,6);memcpy(f.data()+10,ap,6);assert(parse(f.data(),24,-20,6,200,e)&&!e.stationTx&&equal(e.ap,ap)&&equal(e.sta,sta));
 for(int ds:{0,3}){f=frame(8,ds);assert(!parse(f.data(),40,0,0,0,e));}
 f=frame(0x88);assert(!parse(f.data(),25,0,0,0,e)&&parse(f.data(),26,0,0,0,e));f[1]|=0x80;assert(!parse(f.data(),29,0,0,0,e)&&parse(f.data(),30,0,0,0,e));
 f=frame(0x48,0x41);assert(parse(f.data(),24,-50,6,0,e)); // protected null-data relation
 f=frame();f[0]|=1;assert(!parse(f.data(),40,0,0,0,e));
 for(int pos:{4,10}){f=frame();f[pos]|=1;assert(!parse(f.data(),40,0,0,0,e));f=frame();memset(f.data()+pos,0,6);assert(!parse(f.data(),40,0,0,0,e));}
 f=frame();memcpy(f.data()+10,ap,6);assert(!parse(f.data(),40,0,0,0,e));
 for(int sub:{0,2,4,5,8,10,12,13}){f=frame(sub<<4,0);assert(!parse(f.data(),40,0,0,0,e));}
 for(int sub:{1,3}){
  f=frame(sub<<4,0);memcpy(f.data()+4,sta,6);memcpy(f.data()+10,ap,6);memcpy(f.data()+16,ap,6);
  assert(parse(f.data(),30,-20,6,300,e)&&e.evidence==ASSOC&&!e.stationTx);
  assert(!parse(f.data(),29,0,0,0,e));f[26]=1;assert(!parse(f.data(),30,0,0,0,e));f[26]=0;f[27]=1;assert(!parse(f.data(),30,0,0,0,e));f[27]=0;
  f[22]=1;assert(!parse(f.data(),30,0,0,0,e));f[22]=0;f[16]^=2;assert(!parse(f.data(),30,0,0,0,e));
 }
 // Exercise every byte length/FC combination with sanitizers (no short-read access).
 for(int fc=0;fc<256;fc++)for(int flags=0;flags<256;flags++)for(size_t n=0;n<=40;n++){f=frame(fc,flags);parse(f.data(),n,-50,6,1,e);}
 Ring q;for(int i=0;i<QUEUE;i++){e.at=i;assert(q.push(e));}assert(!q.push(e)&&q.drops==1&&q.peak==QUEUE);for(int i=0;i<QUEUE;i++){assert(q.pop(e)&&e.at==unsigned(i));}assert(!q.pop(e));
 Table t;f=frame();assert(parse(f.data(),24,-51,6,100,e));t.observe(e);e.at=200;e.stationTx=false;e.rssi=-20;e.evidence=ASSOC;t.observe(e);
 Record out[2];int offset=0;assert(t.page(ap,200,offset,2,out)==1);assert(out[0].stationRssi==-51&&out[0].rssiAt==100&&out[0].frames==2&&out[0].evidence==3&&out[0].first==100);
 memcpy(e.sta,other,6);e.at=201;t.observe(e);offset=0;assert(t.page(ap,202,offset,2,out)==2&&equal(out[0].last.sta,other)&&!out[0].hasRssi);
 offset=99;assert(t.page(ap,202,offset,1,out)==2&&offset==1&&equal(out[0].last.sta,sta));
 offset=0;assert(t.page(ap,300201,offset,2,out)==0); // strict retention boundary
 t={};for(int i=0;i<CAPACITY+1;i++){e={};memcpy(e.ap,ap,6);memcpy(e.sta,sta,6);e.sta[4]=i>>8;e.sta[5]=i;e.at=100+i;t.observe(e);}assert(t.evictions==1);offset=0;assert(t.page(ap,500,offset,2,out)==CAPACITY&&out[0].last.sta[4]==1);
 t={};e={};memcpy(e.ap,ap,6);memcpy(e.sta,sta,6);e.at=100;t.observe(e);memcpy(e.ap,other,6);e.at=101;t.observe(e);
 offset=0;assert(t.page(ap,102,offset,2,out)==1&&out[0].frames==1);offset=0;assert(t.page(other,102,offset,2,out)==1&&out[0].frames==1);
 // A retained pair heard again after the window begins a new observation period.
 e.at=300102;t.observe(e);offset=0;assert(t.page(other,300103,offset,2,out)==1&&out[0].frames==1&&out[0].first==300102);
 t={};e={};memcpy(e.ap,ap,6);memcpy(e.sta,sta,6);e.at=0xfffffff0;t.observe(e);e.at=20;t.observe(e);offset=0;assert(t.page(ap,30,offset,2,out)==1&&out[0].frames==2);
 t.rows[0].frames=UINT32_MAX;e.at=21;t.observe(e);assert(t.rows[0].frames==UINT32_MAX);
 printf("Clients parser, RX bounds, evidence, queue overflow, AP binding, RSSI provenance, retention, eviction and wrap PASS (%zu bytes storage)\n",sizeof(Storage));
}
