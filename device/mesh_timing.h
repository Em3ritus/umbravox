#pragma once
#include "mesh.h"
#include <cstring>
namespace meshtiming {
enum Profile : uint8_t { Tracking=0, Discovery=1 };
constexpr uint16_t DEFAULT_DWELL=220, MIN_DWELL=100, MAX_DWELL=600;
inline uint16_t dwell(uint16_t v){return v<MIN_DWELL||v>MAX_DWELL?DEFAULT_DWELL:v;}
// Additive v4 messages: existing assignment/report layouts remain byte-identical.
constexpr uint8_t CONFIG=8, FORMAT=1;
struct __attribute__((packed)) Config {mesh::Hdr h;uint8_t format=FORMAT,profile=Tracking,active=0;uint16_t dwellMs=DEFAULT_DWELL;};
inline bool config(const uint8_t* data,int len,Config& out){
 if(len!=sizeof(Config) || !mesh::valid(data,len,CONFIG,sizeof(Config)))return false;
 memcpy(&out,data,sizeof out);
 return out.format==FORMAT && out.active<=1 && out.profile<=Discovery && out.dwellMs>=MIN_DWELL && out.dwellMs<=MAX_DWELL;
}
}
