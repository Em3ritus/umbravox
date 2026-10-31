#include "gpx.h"
#include "sdlog.h"
#include "csv.h"
#include <cstdio>
#include <cstring>
namespace gpx {
namespace {
constexpr char head[]="<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<gpx version=\"1.1\" creator=\"umbravox\" xmlns=\"http://www.topografix.com/GPX/1/1\"><trk><name>umbravox track</name><trkseg>\n";
constexpr char tail[]="</trkseg></trk></gpx>\n";
constexpr char split[]="</trkseg><trkseg>\n";
constexpr uint32_t LIMIT=16*1024*1024;
char session[32]{},path[32]{},batch[2048]{};
uint32_t body=0,count=0,pending=0,lastFix=0,flushedAt=0;
size_t used=0;
bool seen=false,gap=false,failed=false,error=false,recording=false;
void fail(){failed=true;error=true;used=0;pending=0;}
void flush(uint32_t now){
 if(!used || failed)return;
 // Between writes the file always has its XML footer. Only that footer is
 // replaced; committed points are never rewritten. Power loss during this
 // transaction may leave a truncated XML tail (not an atomic filesystem).
 const uint32_t expected=body?body+sizeof(tail)-1:0;
 if(!sdlog::present() || (body && sdlog::sizeOf(path)!=expected) || (!body && sdlog::exists(path))){fail();return;}
 if(body && !sdlog::truncateTo(path,body)){fail();return;}
 uint32_t size=0;
 bool opened=sdlog::openAppend(path,size);
 bool ok=opened && size==body;
 if(ok && !body)ok=sdlog::writeAppend(head,sizeof(head)-1);
 if(ok)ok=sdlog::writeAppend(batch,used);
 if(ok)ok=sdlog::writeAppend(tail,sizeof(tail)-1);
 if(opened){const bool closed=sdlog::closeAppend();ok=ok && closed;}
 if(!ok){
  // Best-effort restore the last complete document, then stop this session.
  // Do not retry appends to a potentially corrupt file.
  if(sdlog::truncateTo(path,body) && body){
   uint32_t n=0;
   if(sdlog::openAppend(path,n)){if(n==body)sdlog::writeAppend(tail,sizeof(tail)-1);sdlog::closeAppend();}
  }
  fail();return;
 }
 body+=(body?0:sizeof(head)-1)+used;count+=pending;used=0;pending=0;flushedAt=now;
}
void begin(const char* name,uint32_t now){
 snprintf(session,sizeof session,"%s",name);snprintf(path,sizeof path,"%s",name);
 const size_t n=strlen(name);
 body=count=pending=0;used=0;seen=gap=failed=false;recording=true;flushedAt=now;
 if(n>=sizeof session || n<4 || strcmp(name+n-4,".csv")){fail();return;}
 memcpy(path+n-4,".gpx",5);
}
}
void tick(const char* name,const ui::Gnss::Fix& f,uint32_t now,uint32_t epoch){
 if(!name)name="";
 if(recording && strcmp(name,session)){
  flush(now);recording=false;session[0]=0;return; // at most one transaction/pass
 }
 if(!*name)return;
 if(!recording)begin(name,now);
 if(failed)return;
 const bool fresh=f.valid && now-f.atMs<ui::Gnss::FRESH_MS && f.lat7>=-900000000 && f.lat7<=900000000 && f.lon7>=-1800000000 && f.lon7<=1800000000;
 if(!fresh){if(seen)gap=true;}
 else if(!seen || (f.atMs!=lastFix && uint32_t(f.atMs-lastFix)>=SAMPLE_MS)){
  char lat[20],lon[20],time[24],point[160];csv::deg7(lat,f.lat7);csv::deg7(lon,f.lon7);
  int n;
  if(epoch>=1577836800u){csv::stampUtc(time,epoch);time[10]='T';n=snprintf(point,sizeof point,"<trkpt lat=\"%s\" lon=\"%s\"><time>%sZ</time></trkpt>\n",lat,lon,time);}
  else n=snprintf(point,sizeof point,"<trkpt lat=\"%s\" lon=\"%s\"/>\n",lat,lon);
  const bool segment=seen && (gap || uint32_t(f.atMs-lastFix)>=ui::Gnss::FRESH_MS);
  const size_t add=size_t(n)+(segment?sizeof(split)-1:0);
  if(n<0 || n>=int(sizeof point) || body+used+add+sizeof(head)+sizeof(tail)>LIMIT){fail();return;}
  if(used+add>sizeof batch){flush(now);return;} // next pass samples the same fix
  if(segment){memcpy(batch+used,split,sizeof(split)-1);used+=sizeof(split)-1;}
  memcpy(batch+used,point,n);used+=n;pending++;lastFix=f.atMs;seen=true;gap=false;
 }
 if(now-flushedAt>=FLUSH_MS)flush(now);
}
const char* status(){return failed?"WRITE ERROR":!recording?(count?"SAVED":"OFF"):!seen?"WAIT GPS":"RECORDING";}
const char* fileName(){return path;}
uint32_t points(){return count;}
bool takeError(){const bool e=error;error=false;return e;}
}
