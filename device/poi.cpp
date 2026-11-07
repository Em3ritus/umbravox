#include "poi.h"
#include "sdlog.h"
#include <cstdio>
#include <cstring>
namespace poi {
namespace {
ui::PoiStatus state=ui::PoiStatus::Ready;
uint32_t changedAt=0,lastRequest=0;
bool requested=false,blocked=false;
char row[256]{};
constexpr const char* path="/poi.csv";
constexpr const char* header="gps_utc_epoch,uptime_ms,latitude,longitude,fix_age_ms,recording_file\n";
void result(ui::PoiStatus s,uint32_t now){state=s;changedAt=now;}
void coordinate(char* out,size_t n,int32_t v){
    const int64_t a=v<0?-(int64_t)v:v;
    snprintf(out,n,"%s%ld.%07ld",v<0?"-":"",(long)(a/10000000),(long)(a%10000000));
}
}
bool request(const ui::Gnss::Fix& fix,uint32_t now,uint32_t epoch,const char* session){
    if(state==ui::PoiStatus::Saving || (requested && now-lastRequest<1000))return false;
    requested=true;lastRequest=now;
    if(blocked){result(ui::PoiStatus::WriteError,now);return false;}
    if(!fix.valid || now-fix.atMs>=ui::Gnss::FRESH_MS || fix.lat7 < -900000000 || fix.lat7 > 900000000 || fix.lon7 < -1800000000 || fix.lon7 > 1800000000){
        result(ui::PoiStatus::NoFix,now);return false;
    }
    if(!sdlog::present()){result(ui::PoiStatus::NoCard,now);return false;}
    char lat[20],lon[20],utc[16]={},escaped[128]={};
    coordinate(lat,sizeof lat,fix.lat7);coordinate(lon,sizeof lon,fix.lon7);
    if(epoch)snprintf(utc,sizeof utc,"%lu",(unsigned long)epoch);
    size_t k=0;
    for(size_t i=0;session && session[i];++i){
        const char c=session[i];
        if(c=='\r'||c=='\n'||k+2>=sizeof escaped){result(ui::PoiStatus::WriteError,now);return false;}
        if(c=='"')escaped[k++]='"';
        escaped[k++]=c;
    }
    const int n=snprintf(row,sizeof row,"%s,%lu,%s,%s,%lu,\"%s\"\n",utc,(unsigned long)now,lat,lon,(unsigned long)(now-fix.atMs),escaped);
    if(n<0 || (size_t)n>=sizeof row){result(ui::PoiStatus::WriteError,now);return false;}
    result(ui::PoiStatus::Saving,now);return true;
}
void service(uint32_t now){
    if(state!=ui::PoiStatus::Saving)return;
    if(!sdlog::present()){result(ui::PoiStatus::NoCard,now);return;}
    uint32_t before=0;
    if(!sdlog::openAppend(path,before)){result(ui::PoiStatus::WriteError,now);return;}
    bool ok=before!=0 || sdlog::writeAppend(header,strlen(header));
    if(ok)ok=sdlog::writeAppend(row,strlen(row));
    const bool closed=sdlog::closeAppend();
    ok=ok && closed;
    if(!ok && !sdlog::truncateTo(path,before))blocked=true;
    result(ok?ui::PoiStatus::Saved:ui::PoiStatus::WriteError,now);
}
ui::PoiStatus status(uint32_t now){
    if(state!=ui::PoiStatus::Saving && now-changedAt>=4000)return ui::PoiStatus::Ready;
    return state;
}
}
