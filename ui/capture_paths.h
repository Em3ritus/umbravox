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
// Evidence CSVs that reached the file limit continue in <name>-2.csv ... -N.csv (device/evidence_csv.h MAX_PARTS, which
// static_asserts it equals this). They are recognized so they can be listed, labelled and deleted like their first part.
constexpr unsigned MAX_EVIDENCE_PART=8;
inline bool partDigit(char c){return c>='2'&&c<char('0'+MAX_EVIDENCE_PART+1);}
inline Kind evidenceLeaf(const char* leaf){
 const char* names[]={"clients","probes"};const Kind kinds[]={Clients,Probes};
 for(int i=0;i<2;i++){
  const size_t z=strlen(names[i]);if(!starts(leaf,names[i]))continue;const char* r=leaf+z;
  if(eq(r,".csv")||(r[0]=='-'&&partDigit(r[1])&&eq(r+2,".csv")))return kinds[i];
 }
 return None;
}
inline Kind classify(const char* p){
 if(!p||p[0]!='/'||strlen(p)>=CAP)return None;
 if(starts(p,"/umbravox/sessions/")){
  const char* id=p+sizeof("/umbravox/sessions/")-1;const char* slash=strchr(id,'/');if(!slash||!stem(id,slash-id))return None;
  const char* leaf=slash+1;
  if(eq(leaf,"wifi.csv"))return Wifi;if(eq(leaf,"track.gpx"))return Track;return evidenceLeaf(leaf);
 }
 const char* base=p+1;
 if(starts(p,"/umbravox/captures/"))base=p+19;
 if(starts(base,"capture")){
  const char* d=base+7;int n=0;while(*d>='0'&&*d<='9'){++n;++d;}return n>=1&&n<=10&&eq(d,".pcap")?Pcap:None;
 }
 if(base!=p+1)return None;
 const char* suffixes[]={".csv","-clients.csv","-probes.csv",".gpx"};const Kind kinds[]={Wifi,Clients,Probes,Track};
 const size_t n=strlen(base);
 if(n>6&&eq(base+n-4,".csv")&&partDigit(base[n-5])&&base[n-6]=='-'){   // <stem>-clients-N.csv / <stem>-probes-N.csv
  const size_t m=n-6;const char* tails[]={"-clients","-probes"};const Kind tk[]={Clients,Probes};
  for(int i=0;i<2;i++){const size_t z=strlen(tails[i]);bool same=m>z;for(size_t j=0;same&&j<z;j++)same=lc(base[m-z+j])==tails[i][j];if(same&&stem(base,m-z))return tk[i];}
 }
 for(int i=0;i<4;i++){const size_t z=strlen(suffixes[i]);if(n>z&&eq(base+n-z,suffixes[i])&&stem(base,n-z))return kinds[i];}
 return None;
}
inline const char* label(Kind k){switch(k){case Wifi:return "WIFI CSV";case Clients:return "CLIENT CSV";case Probes:return "PROBE CSV";case Track:return "GPS GPX";case Pcap:return "RAW PCAP";default:return "FILE";}}
// Stable session identifier for grouping/sorting, independent of format/location.
inline void identity(const char* path,char* out,size_t cap){
 const char* p=path;if(*p=='/')++p;if(starts(p,"umbravox/sessions/"))p+=sizeof("umbravox/sessions/")-1;else if(starts(p,"umbravox/captures/"))p+=18;
 size_t n=strcspn(p,"/.");
 if(n>2&&p[n-2]=='-'&&partDigit(p[n-1])){   // a rolled-over part: drop "-clients-N" / "-probes-N" like the first part's tail
  const size_t q=n-2;auto tail=[&](const char* t){const size_t z=strlen(t);if(q<=z)return false;for(size_t j=0;j<z;j++)if(lc(p[q-z+j])!=t[j])return false;return true;};
  if(tail("-clients"))n=q-8;else if(tail("-probes"))n=q-7;
 }
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
