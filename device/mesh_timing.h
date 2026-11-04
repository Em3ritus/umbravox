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

namespace meshtiming {
constexpr uint8_t TELEMETRY=9;
struct __attribute__((packed)) Telemetry {
 mesh::Hdr h;
 uint8_t format=FORMAT,profile=Tracking,active=0,camp=0,nChan=0;
 uint16_t dwellMs=DEFAULT_DWELL,depth=0,capacity=0,peak=0;
 uint32_t boot=0,uptime=0,drops=0,reportQueued=0,reportRejected=0,sendFailed=0,scanFailed=0;
};
static_assert(sizeof(Telemetry)<=250,"ESP-NOW v1 payload budget");
inline bool telemetry(const uint8_t* data,int len,const uint8_t* sender,Telemetry& out){
 if(!data || !sender || len!=sizeof(Telemetry) || !mesh::valid(data,len,TELEMETRY,sizeof(Telemetry)))return false;
 memcpy(&out,data,sizeof out);
 return !memcmp(out.h.mac,sender,6) && out.format==FORMAT && out.active<=1 && out.camp<=1
     && out.profile<=Discovery && out.nChan<=mesh::MAX_ASSIGN && out.nChan>0
     && out.dwellMs>=MIN_DWELL && out.dwellMs<=MAX_DWELL
     && out.capacity>0 && out.capacity<=4096 && out.depth<=out.capacity
     && out.peak<=out.capacity && out.peak>=out.depth;
}
struct TelemetryState {
 bool known=false;uint32_t receivedAt=0;Telemetry data{};
 bool accept(const Telemetry& v,uint32_t now){
  if(known && data.boot==v.boot && int32_t(v.uptime-data.uptime)<=0)return false;
  data=v;receivedAt=now;known=true;return true;
 }
};
}
