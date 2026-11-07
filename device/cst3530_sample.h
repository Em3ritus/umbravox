#pragma once
#include <stdint.h>
#include <stddef.h>
namespace cst3530 {
enum class Result:uint8_t { Touch, Lift, Fail };
// First-contact packet from Waveshare's CST3530 0xD0070000 register.
inline Result decode(const uint8_t* p,size_t n,uint16_t& x,uint16_t& y){
 if(!p || n!=9)return Result::Fail;
 const unsigned count=p[3]&15;
 if(count>5)return Result::Fail;
 if(!count || !(p[8]&0xF0))return Result::Lift;
 const uint16_t nx=((p[7]&15)<<8)|p[4],ny=((p[7]&0xF0)<<4)|p[5];
 if(nx>=240 || ny>=320)return Result::Fail;
 x=nx;y=ny;return Result::Touch;
}
inline void landscape(uint16_t x,uint16_t y,int& sx,int& sy){sx=y;sy=239-x;}
}
