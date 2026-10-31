#pragma once
#include <cstring>
#include <cstdio>
#include <cstdint>
namespace capturepaths {
constexpr size_t CAP=64;
enum Kind {None,Wifi,Clients,Probes,Track,Pcap};
inline char lc(char c){return c>='A'&&c<='Z'?c+32:c;}
inline bool eq(const char* a,const char* b){while(*a&&*b){if(lc(*a++)!=lc(*b++))return false;}return *a==*b;}
inline bool starts(const char* a,const char* b){while(*b){if(!*a||lc(*a++)!=lc(*b++))return false;}return true;}
inline bool stem(const char* s,size_t n){if(n<4||n>27||!starts(s,"WD-"))return false;bool digit=false;for(size_t i=3;i<n;i++){if(s[i]>='0'&&s[i]<='9')digit=true;else if(s[i]!='-')return false;}return digit;}
inline Kind classify(const char* p){
 if(!p||p[0]!='/'||strlen(p)>=CAP)return None;
 if(starts(p,"/umbravox/sessions/")){
  const char* id=p+sizeof("/umbravox/sessions/")-1;const char* slash=strchr(id,'/');if(!slash||!stem(id,slash-id))return None;
  const char* leaf=slash+1;
  if(eq(leaf,"wifi.csv"))return Wifi;if(eq(leaf,"clients.csv"))return Clients;if(eq(leaf,"probes.csv"))return Probes;if(eq(leaf,"track.gpx"))return Track;return None;
 }
 const char* base=p+1;
 if(starts(p,"/umbravox/captures/"))base=p+19;
 if(starts(base,"capture")){
  const char* d=base+7;int n=0;while(*d>='0'&&*d<='9'){++n;++d;}return n>=1&&n<=10&&eq(d,".pcap")?Pcap:None;
 }
 if(base!=p+1)return None;
 const char* suffixes[]={".csv","-clients.csv","-probes.csv",".gpx"};const Kind kinds[]={Wifi,Clients,Probes,Track};
 const size_t n=strlen(base);
 for(int i=0;i<4;i++){const size_t z=strlen(suffixes[i]);if(n>z&&eq(base+n-z,suffixes[i])&&stem(base,n-z))return kinds[i];}
 return None;
}
inline const char* label(Kind k){switch(k){case Wifi:return "WIFI CSV";case Clients:return "CLIENT CSV";case Probes:return "PROBE CSV";case Track:return "GPS GPX";case Pcap:return "RAW PCAP";default:return "FILE";}}
// Stable session identifier for grouping/sorting, independent of format/location.
inline void identity(const char* path,char* out,size_t cap){
 const char* p=path;if(*p=='/')++p;if(starts(p,"umbravox/sessions/"))p+=sizeof("umbravox/sessions/")-1;else if(starts(p,"umbravox/captures/"))p+=18;
 size_t n=strcspn(p,"/.");
 if(n>8&&eq(p+n-8,"-clients.csv"))n-=8;
 if(n>7&&eq(p+n-7,"-probes.csv"))n-=7;
 if(n>=cap)n=cap-1;memcpy(out,p,n);out[n]=0;
}
struct ActiveFiles {
 char paths[5][CAP]{};
 void set(unsigned i,const char* p){if(i<5)snprintf(paths[i],CAP,"%s",p?p:"");}
 bool contains(const char* p)const{if(!p||!*p)return false;for(const auto& a:paths)if(*a&&eq(a,p))return true;return false;}
 bool operator==(const ActiveFiles& b)const{for(int i=0;i<5;i++)if(!eq(paths[i],b.paths[i]))return false;return true;}
};
}
