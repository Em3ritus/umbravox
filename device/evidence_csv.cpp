#include "evidence_csv.h"
#include "csv.h"
#include "sdlog.h"
#include <cstdio>
#include <cstring>
namespace evidencecsv {
namespace {
const char* headers[]={
 "snapshot_ms,snapshot_utc,ap_bssid,station_mac,local_mac,channel,station_rssi_dbm,rssi_age_ms,evidence,first_seen_ms,last_seen_ms,observed_frames,queue_drops,table_evictions\r\n",
 "snapshot_ms,snapshot_utc,station_mac,local_mac,channel,rssi_dbm,request_type,ssid_display,ssid_hex,first_seen_ms,last_seen_ms,observed_requests,queue_drops,table_evictions\r\n"};
struct File {char path[64]{},batch[1024]{};size_t used=0;uint32_t bytes=0,pending=0;};
File files[2];char session[48]{},row[512]{};
unsigned cursor=0;uint32_t nextAt=0,committed=0;bool running=false,failed=false,error=false,sweeping=false;
// Shared scratch: main-loop only, avoids consuming scarce loop stack.
char timeText[24],apText[20],staText[20],rssiText[12],ageText[16],nameText[34],quoted[72],hex[65];
void stamp(uint32_t epoch){timeText[0]=0;if(epoch>=1577836800u)csv::stampUtc(timeText,epoch);}
size_t length(int n,size_t cap){return n>=0&&size_t(n)<cap?size_t(n):0;}
void fail(){failed=true;error=true;}
bool flush(File& f){
 if(!f.used||failed)return !failed;
 if(!sdlog::present() || (!f.bytes&&sdlog::exists(f.path)) || (f.bytes&&sdlog::sizeOf(f.path)!=f.bytes)){fail();return false;}
 uint32_t before=0;const bool opened=sdlog::openAppend(f.path,before);
 bool ok=opened&&before==f.bytes;
 if(ok)ok=sdlog::writeAppend(f.batch,f.used);
 if(opened){const bool closed=sdlog::closeAppend();ok=ok&&closed;}
 if(!ok){if(opened&&before==f.bytes)sdlog::truncateTo(f.path,f.bytes);fail();return false;}
 f.bytes+=f.used;committed+=f.pending;f.used=0;f.pending=0;return true;
}
}
size_t client(char* out,size_t cap,const clients::Record& r,uint32_t now,uint32_t epoch,uint32_t drops,uint32_t evictions){
 if(!r.used||uint32_t(now-r.last.at)>=clients::RETAIN_MS)return 0;
 stamp(epoch);csv::mac(apText,r.last.ap);csv::mac(staText,r.last.sta);rssiText[0]=ageText[0]=0;
 if(r.hasRssi){snprintf(rssiText,sizeof rssiText,"%d",r.stationRssi);snprintf(ageText,sizeof ageText,"%lu",(unsigned long)(now-r.rssiAt));}
 return length(snprintf(out,cap,"%lu,%s,%s,%s,%u,%u,%s,%s,%s,%lu,%lu,%lu,%lu,%lu\r\n",
 (unsigned long)now,timeText,apText,staText,!!(r.last.sta[0]&2),r.last.channel,rssiText,ageText,
 r.evidence==3?"DATA+ASSOC_OK":(r.evidence&clients::ASSOC)?"ASSOC_OK":"DATA",
 (unsigned long)r.first,(unsigned long)r.last.at,(unsigned long)r.frames,(unsigned long)drops,(unsigned long)evictions),cap);
}
size_t probe(char* out,size_t cap,const evidence::ProbeRow& r,uint32_t now,uint32_t epoch,uint32_t drops,uint32_t evictions){
 if(!r.used||r.last.len>32)return 0;
 stamp(epoch);csv::mac(staText,r.last.sta);
 // Prefix display text with an apostrophe for spreadsheet safety. Hex is the
 // authoritative raw SSID, including NUL/non-ASCII bytes and original prefix.
 nameText[0]='\'';evidence::display(nameText+1,r.last.name,r.last.len);csv::quote(nameText,quoted,sizeof quoted);
 constexpr char digits[]="0123456789ABCDEF";
 for(unsigned i=0;i<r.last.len;i++){hex[2*i]=digits[r.last.name[i]>>4];hex[2*i+1]=digits[r.last.name[i]&15];}hex[2*r.last.len]=0;
 return length(snprintf(out,cap,"%lu,%s,%s,%u,%u,%d,%s,%s,%s,%lu,%lu,%lu,%lu,%lu\r\n",
 (unsigned long)now,timeText,staText,!!(r.last.sta[0]&2),r.last.channel,r.last.rssi,r.last.len?"DIRECTED":"WILDCARD",quoted,hex,
 (unsigned long)r.first,(unsigned long)r.last.at,(unsigned long)r.count,(unsigned long)drops,(unsigned long)evictions),cap);
}
void stop(){if(running){flush(files[0]);flush(files[1]);}running=false;session[0]=0;}
void tick(const char* name,uint32_t now,uint32_t epoch,Source source){
 if(!name)name="";
 if(running&&strcmp(name,session)){
  // Stop/rollover drains at most one batch per pass; no new rows admitted.
  for(auto& f:files)if(f.used&&!failed){flush(f);return;}
  running=false;session[0]=0;return;
 }
 if(!*name)return;
 if(!running){
  failed=false;committed=0;cursor=0;sweeping=true;running=true;
  const size_t n=strlen(name);
  if(n>=sizeof session||n<4||strcmp(name+n-4,".csv")){fail();running=false;return;}
  memcpy(session,name,n+1);
  for(unsigned i=0;i<2;i++){auto& f=files[i];f.used=0;f.bytes=0;f.pending=0;snprintf(f.path,sizeof f.path,"%.*s-%s.csv",int(n-4),name,i?"probes":"clients");f.used=strlen(headers[i]);memcpy(f.batch,headers[i],f.used);}
 }
 if(failed)return;
 if(!sweeping){if(int32_t(now-nextAt)<0)return;sweeping=true;cursor=0;}
 if(cursor>=clients::CAPACITY+evidence::PROBES){
  for(auto& f:files)if(f.used){flush(f);return;}
  sweeping=false;nextAt=now+INTERVAL_MS;return;
 }
 auto& f=files[cursor>=clients::CAPACITY];
 const size_t n=source?source(cursor,row,sizeof row,now,epoch):0;
 if(n>=sizeof row){fail();return;}
 if(n&&f.used+n>sizeof f.batch){flush(f);return;} // retry this slot next pass
 if(f.bytes+f.used+n>16u*1024*1024){fail();return;}
 if(n){memcpy(f.batch+f.used,row,n);f.used+=n;f.pending++;}
 cursor++;
}
bool takeError(){bool v=error;error=false;return v;}
const char* status(){return failed?"WRITE ERROR":running?"RECORDING":"OFF";}
uint32_t rows(){return committed;}
}
