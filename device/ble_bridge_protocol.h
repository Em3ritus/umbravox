#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <algorithm>
#include "../ui/wifi_auth.h"

namespace bridgewire {
constexpr size_t MAX_LINE=384, MAX_CMD=192, HEADER=12;
// Compatibility sentinel from slickrick773's ESP32Marauder PR #1671.
// Not the Umbravox software version. Never replace with a normal release number.
constexpr const char* FW_VERSION="99.99.99";
#if defined(WS_C5_28)
constexpr const char* WD_CAPS="WD:CAPS fw=99.99.99 chip=esp32c5 bands=2g,5g ota=0 bt=0 gps=0 cap=0 usb=0 board_name=Umbravox_WS_C5_28 link=ble";
#elif defined(NM_CYD_C5)
constexpr const char* WD_CAPS="WD:CAPS fw=99.99.99 chip=esp32c5 bands=2g,5g ota=0 bt=0 gps=0 cap=0 usb=0 board_name=Umbravox_NM_CYD_C5 link=ble";
#else
constexpr const char* WD_CAPS="WD:CAPS fw=99.99.99 chip=esp32c5 bands=2g,5g ota=0 bt=0 gps=0 cap=0 usb=0 board_name=Umbravox_WS_C5_35 link=ble";
#endif
constexpr const char* WD_SERVICE="47686F73-7445-5350-4272-696467655376";
constexpr const char* WD_RX="0147686F-7374-4553-5042-726964675852";
constexpr const char* WD_TX="0247686F-7374-4553-5042-726964675854";
constexpr const char* WD_CTRL="0347686F-7374-4553-5042-72694C525443";
constexpr const char* BIS_SERVICE="4fafc201-1fb5-459e-8fcc-c5c9c331914b";
constexpr const char* BIS_CMD="beb5483e-36e1-4688-b7f5-ea07361b26a8";
constexpr const char* BIS_RSP="beb5483e-36e1-4688-b7f5-ea07361b26a9";
constexpr const char* BIS_STA="beb5483e-36e1-4688-b7f5-ea07361b26aa";
enum Mode:uint8_t { OFF, WARDRIVEGO, BISCUIT };
// esp_read_mac supplies canonical byte order, unlike NimBLE's native address.
inline void formatDeviceName(char* out,size_t cap,const uint8_t mac[6]){
    snprintf(out,cap,"Umbravox-%02X%02X%02X",mac[3],mac[4],mac[5]);
}
struct Identity {bool wdVisible,biscuitVisible;const char* model;};
inline Identity identity(Mode mode){
#if defined(WS_C5_28)
    constexpr const char* boardName="Umbravox WS C5 28";
#elif defined(NM_CYD_C5)
    constexpr const char* boardName="Umbravox NM CYD C5";
#else
    constexpr const char* boardName="Umbravox WS C5 35";
#endif
    // Keep Biscuit's compatibility model only in Biscuit mode.
    return {mode==WARDRIVEGO,mode==BISCUIT,mode==BISCUIT?"Biscuit DIY":boardName};
}
// Hide both services before exposing the selected protocol. Fail closed.
template<class SetVisibility> bool selectServices(Mode mode,SetVisibility set){
    if(!set(false,false))return false;
    if(!set(true,false))return false;
    const auto i=identity(mode);
    return (!i.wdVisible || set(false,true)) && (!i.biscuitVisible || set(true,true));
}
struct Ap { uint8_t mac[6]{}; char ssid[33]{}; int8_t rssi=0; uint8_t channel=0; uint16_t auth=0; uint32_t at=0; };
inline uint32_t u32(const uint8_t* p) { return uint32_t(p[0])|uint32_t(p[1])<<8|uint32_t(p[2])<<16|uint32_t(p[3])<<24; }
inline size_t frame(uint8_t* out,size_t cap,uint8_t type,uint32_t id,const char* text,size_t n,uint8_t status=0) {
    if(cap<HEADER || n>cap-HEADER || n>65535)return 0;
    out[0]='G';out[1]='B';out[2]=1;out[3]=type;out[4]=status;out[5]=0;
    for(int i=0;i<4;++i)out[6+i]=uint8_t(id>>(8*i));
    out[10]=uint8_t(n);out[11]=uint8_t(n>>8);
    if(n)memcpy(out+HEADER,text,n);
    return HEADER+n;
}
// Reassembles one bounded CMD across GATT writes. Raw writes are complete lines.
struct Command {
    char text[MAX_CMD+1]{}; size_t used=0; uint32_t id=1, at=0; bool partial=false, framed=false;
    void reset(){used=0;partial=false;text[0]=0;}
    int accept(const uint8_t* p,size_t n,uint32_t now) { // -1 rejected, 0 pending, 1 ready
        if(!p || !n)return -1;
        if(partial && uint32_t(now-at)>3000)reset();
        const bool gb=n>=2 && p[0]=='G' && p[1]=='B';
        if(gb){
            if(n<HEADER || p[2]!=1 || p[3]!=1 || p[4]!=0 || (p[5]&~3) || size_t(p[10]|uint16_t(p[11])<<8)!=n-HEADER){reset();return -1;}
            const uint32_t next=u32(p+6);const bool first=p[5]&1,more=p[5]&2;
            if(first){reset();id=next;}else if(partial){if(id!=next){reset();return -1;}}
            else if(more){reset();return -1;}else {reset();id=next;}
            framed=true;p+=HEADER;n-=HEADER;
            if(n>MAX_CMD-used){reset();return -1;}
            for(size_t i=0;i<n;++i)if(p[i]<32 && p[i]!='\r' && p[i]!='\n' && p[i]!='\t'){reset();return -1;}
            memcpy(text+used,p,n);used+=n;text[used]=0;partial=more;at=now;
            return more?0:1;
        }
        reset();framed=false;id=1;
        if(n>MAX_CMD)return -1;
        for(size_t i=0;i<n;++i)if(p[i]<32 && p[i]!='\r' && p[i]!='\n' && p[i]!='\t')return -1;
        memcpy(text,p,n);used=n;text[n]=0;return 1;
    }
};
enum Action { INVALID, START, STOP, TIME_SYNC, UNSUPPORTED };
struct ChannelFilter {
    bool enabled=false; uint8_t bits[32]{};
    bool allows(uint8_t ch) const {return ch && (!enabled || (bits[ch/8] & (1u<<(ch%8))));}
};
struct Parsed { Action action=INVALID; uint8_t band=0; char name[32]{}; ChannelFilter channels; };
inline Parsed parse(const char* input,Mode mode) {
    Parsed r;char s[MAX_CMD+1];size_t n=strlen(input);if(n>MAX_CMD)return r;
    while(n && (input[n-1]=='\n'||input[n-1]=='\r'||input[n-1]==' '||input[n-1]=='\t'))--n;
    while(n && (*input==' '||*input=='\t')){++input;--n;}
    memcpy(s,input,n);s[n]=0;
    // Never execute a prefix or accept multiple commands in one write.
    if(strchr(s,'\n')||strchr(s,'\r'))return r;
    if(mode==WARDRIVEGO){
        if(!strcmp(s,"wdstream start -wifi") || !strcmp(s,"wdstream start") || !strcmp(s,"start"))r.action=START;
        else if(!strncmp(s,"wdstream start -wifi -ch ",sizeof("wdstream start -wifi -ch ")-1)){
            const char* p=s+sizeof("wdstream start -wifi -ch ")-1;
            if(!*p)return r;
            while(*p){
                unsigned ch=0,digits=0;
                while(*p>='0' && *p<='9'){
                    if(++digits>3)return r;
                    ch=ch*10+unsigned(*p++-'0');
                }
                if(!digits || !ch || ch>196)return r;
                r.channels.bits[ch/8]|=uint8_t(1u<<(ch%8));
                if(!*p)break;
                if(*p++!=',' || !*p)return r;
            }
            r.channels.enabled=true;r.action=START;
        }
        else if(!strcmp(s,"stop")||!strcmp(s,"wdstream stop"))r.action=STOP;
        else r.action=UNSUPPORTED;
    }else if(mode==BISCUIT){
        if(!strncmp(s,"TIME:SYNC:",10)){r.action=TIME_SYNC;return r;}
        if(strncmp(s,"CMD:",4))return r;
        char* token=s+4;char* colon=strchr(token,':');if(colon)*colon=0;
        if(strlen(token)>=sizeof r.name)return r;
        for(const char* p=token;*p;++p)if(!((*p>='a'&&*p<='z')||(*p>='0'&&*p<='9')))return r;
        strcpy(r.name,token);
        if(!strcmp(token,"wardriveall")||!strcmp(token,"scanap"))r.action=START;
        else if(!strcmp(token,"wardrive")){r.action=START;r.band=1;}
        else if(!strcmp(token,"wardrive5g")){r.action=START;r.band=2;}
        else if(!strcmp(token,"stopscan"))r.action=STOP;
        else r.action=UNSUPPORTED;
    }
    return r;
}
inline const char* auth(uint16_t a){
    if(a&ui::AUTH_OWE)return "UNKNOWN";
    if(a&ui::AUTH_WPA3)return (a&ui::AUTH_PSK)?"WPA2_WPA3":"WPA3";
    if(a&ui::AUTH_WPA2)return "WPA2";
    if(a&ui::AUTH_WPA)return "WPA";
    if(a&ui::AUTH_WEP)return "WEP";
    return (a&ui::AUTH_PRIVACY)?"UNKNOWN":"OPEN";
}
inline const char* biscuitAuth(uint16_t a){
    if(a&ui::AUTH_OWE)return "[UNKNOWN]";
    if(a&ui::AUTH_WPA3)return (a&ui::AUTH_PSK)?"[WPA2_WPA3_PSK]":"[WPA3_PSK]";
    if(a&ui::AUTH_EAP)return "[WPA2_ENTERPRISE]";
    if(a&ui::AUTH_WPA2)return "[WPA2_PSK]";
    if(a&ui::AUTH_WPA)return "[WPA_PSK]";
    if(a&ui::AUTH_WEP)return "[WEP]";
    return (a&ui::AUTH_PRIVACY)?"[UNKNOWN]":"[OPEN]";
}
inline bool restartRound(bool done,uint32_t now,uint32_t at){return done && uint32_t(now-at)>=60000;}
inline bool allows(uint8_t band,uint8_t ch){return ch && (!band || (band==1?ch<=14:ch>14));}
inline size_t apLine(char* out,size_t cap,const Ap& a,Mode mode){
    char mac[18],hex[65],plain[33];
    snprintf(mac,sizeof mac,"%02X:%02X:%02X:%02X:%02X:%02X",a.mac[0],a.mac[1],a.mac[2],a.mac[3],a.mac[4],a.mac[5]);
    size_t n=0;const char* h="0123456789abcdef";
    while(n<32 && a.ssid[n]){const uint8_t c=a.ssid[n];hex[n*2]=h[c>>4];hex[n*2+1]=h[c&15];plain[n]=(c<32||c==','||c==127)?'_':char(c);++n;}
    hex[n*2]=0;plain[n]=0;
    const int k=mode==WARDRIVEGO?snprintf(out,cap,"WD:AP ts=%lu bssid=%s ssid_hex=%s rssi=%d ch=%u auth=%s hidden=%u\n",(unsigned long)a.at,mac,hex,int(a.rssi),unsigned(a.channel),auth(a.auth),n?0u:1u):
        snprintf(out,cap,"DATA:AP:%s,%s,%u,%d,%s\n",plain,mac,unsigned(a.channel),int(a.rssi),biscuitAuth(a.auth));
    if(k<0 || size_t(k)>=cap){if(cap)out[0]=0;return 0;}return size_t(k);
}
struct Line { char text[MAX_LINE]{}; uint16_t len=0, offset=0; uint8_t type=3,status=0;uint32_t id=1; };
struct Outbox {
    static constexpr unsigned CAP=12;
    Line lines[CAP];unsigned head=0,count=0;uint32_t dropped=0;
    void clear(){head=count=0;}
    bool put(const char* text,uint32_t id=1,uint8_t type=3,uint8_t status=0){
        const size_t n=strlen(text);if(count==CAP || n>=MAX_LINE){++dropped;return false;}
        Line& l=lines[(head+count)%CAP];memcpy(l.text,text,n+1);l.len=n;l.offset=0;l.id=id;l.type=type;l.status=status;++count;return true;
    }
    Line* front(){return count?&lines[head]:nullptr;}
    void pop(){if(count){head=(head+1)%CAP;--count;}}
    unsigned free()const{return CAP-count;}
};
}

namespace bridgewire {
// Notifications obey negotiated ATT MTU, including the GhostLink header.
inline size_t packet(const Line& l,Mode mode,unsigned mtu,uint8_t* out,size_t cap,size_t& consumed){
    consumed=0;const size_t att=mtu>3?std::min<size_t>(mtu-3,cap):0;
    const size_t hdr=mode==WARDRIVEGO?HEADER:0;
    if(att<=hdr || l.offset>l.len)return 0;
    consumed=std::min<size_t>(l.len-l.offset,att-hdr);
    if(mode==WARDRIVEGO)return frame(out,cap,l.type,l.id,l.text+l.offset,consumed,l.status);
    memcpy(out,l.text+l.offset,consumed);return consumed;
}
// On a control change, finish only the line already partly transmitted; never
// splice a new message into an incomplete DATA record at the receiver.
inline void discardUnsent(Outbox& q){
    if(q.count && q.front()->offset)q.count=1;
    else q.clear();
}
}
