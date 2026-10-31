#pragma once
#include <cstdio>
#include <cstring>
#include <cstddef>
#include "../ui/capture_paths.h"
namespace storagepaths {
constexpr size_t CAP=64;
constexpr const char* ROOT="/umbravox";
constexpr const char* SESSIONS="/umbravox/sessions";
constexpr const char* CAPTURES="/umbravox/captures";
inline bool session(char* out,size_t cap,const char* legacy){
 if(!legacy||strncmp(legacy,"/WD-",4))return false;
 const size_t n=strlen(legacy);if(n<9||strcmp(legacy+n-4,".csv"))return false;
 for(size_t i=4;i<n-4;i++)if((legacy[i]<'0'||legacy[i]>'9')&&legacy[i]!='-')return false;
 int z=snprintf(out,cap,"%s/%.*s/wifi.csv",SESSIONS,int(n-5),legacy+1);return z>0&&size_t(z)<cap;
}
inline bool companion(char* out,size_t cap,const char* session,const char* kind){
 if(!session||!kind||capturepaths::classify(session)!=capturepaths::Wifi)return false;
 if(strcmp(kind,"track.gpx")&&strcmp(kind,"clients.csv")&&strcmp(kind,"probes.csv"))return false;
 const size_t n=strlen(session);
 const char* leaf=strrchr(session,'/');
 int z=-1;
 if(leaf&&!strcmp(leaf,"/wifi.csv")&&!strncmp(session,"/umbravox/sessions/WD-",sizeof("/umbravox/sessions/WD-")-1))
  z=snprintf(out,cap,"%.*s/%s",int(leaf-session),session,kind);
 else if(n>=4&&!strcmp(session+n-4,".csv")){
  const char* suffix=!strcmp(kind,"track.gpx")?".gpx":!strcmp(kind,"clients.csv")?"-clients.csv":"-probes.csv";
  z=snprintf(out,cap,"%.*s%s",int(n-4),session,suffix);
 }
 return z>=0&&size_t(z)<cap;
}
}
