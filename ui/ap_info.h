#pragma once
#include <stdint.h>
#include <stddef.h>
namespace ui {
// Advertised capabilities, not the PHY/width of an individual received frame.
struct ApInfo {
 uint16_t width=0,beaconTu=0,capabilities=0;
 uint8_t phy=0; // 1 HT, 2 VHT, 4 HE
 bool valid=false,wps=false; int8_t wpsLocked=-1;
 char country[3]{};
};
inline ApInfo parseApInfo(const uint8_t* body,size_t n){
 ApInfo a;
 if(!body || n<12)return a;
 a.beaconTu=body[8]|uint16_t(body[9])<<8;
 a.capabilities=body[10]|uint16_t(body[11])<<8;
 for(size_t p=12;p<n;){
  if(n-p<2)return ApInfo{};
  const unsigned id=body[p],len=body[p+1];p+=2;
  if(len>n-p)return ApInfo{};
  const uint8_t* v=body+p;
  if(id==7 && len>=3 && v[0]>='A' && v[0]<='Z' && v[1]>='A' && v[1]<='Z'){a.country[0]=v[0];a.country[1]=v[1];}
  if(id==45 && len>=26)a.phy|=1;
  if(id==191 && len>=12)a.phy|=2;
  if(id==255 && len>=22 && v[0]==35)a.phy|=4;
  if(id==61 && len>=22 && !a.width)a.width=((v[1]&4) && ((v[1]&3)==1 || (v[1]&3)==3))?40:20;
  if(id==192 && len>=5){
   if(v[0]==1){int delta=int(v[1])-v[2];if(delta<0)delta=-delta;a.width=v[2]?(delta==8?160:8080):80;}
   else if(v[0]==2)a.width=160;else if(v[0]==3)a.width=8080;
  }
  if(id==221 && len>=4 && v[0]==0 && v[1]==0x50 && v[2]==0xf2 && v[3]==4){
   a.wps=true;
   for(size_t q=4;q<len;){
    if(len-q<4)return ApInfo{};
    const unsigned type=unsigned(v[q])<<8|v[q+1],sz=unsigned(v[q+2])<<8|v[q+3];q+=4;
    if(sz>len-q)return ApInfo{};
    if(type==0x1057 && sz==1 && v[q]<=1)a.wpsLocked=v[q];
    q+=sz;
   }
  }
  p+=len;
 }
 a.valid=true;return a;
}
}
