#include "../device/ble_bridge_protocol.h"
#include "../device/ble_bridge_init.h"
#include "../ui/nav.h"
#include "../ui/project.h"
#include <cassert>
#include <string>
#include <cstdio>
using namespace bridgewire;
struct FakeScanner {
    bool active=true,stopOk=true,startOk=true;int stops=0,starts=0;
    bool isScanning(){return active;}
    bool stop(){++stops;if(stopOk)active=false;return stopOk;}
    bool start(int,decltype(nullptr),bool){++starts;active=startOk;return startOk;}
};
int main(){
#if defined(NM_CYD_C5)
    const char* expectedModel="Umbravox NM CYD C5";
    assert(strstr(WD_CAPS,"board_name=Umbravox_NM_CYD_C5"));
#elif defined(WS_C5_28)
    const char* expectedModel="Umbravox WS C5 28";
#else
    const char* expectedModel="Umbravox WS C5 35";
#endif
    const uint8_t primary[6]={0x10,0xbd,0xa3,0x11,0x22,0x33};
    const uint8_t secondary[6]={0x10,0xbd,0xa3,0x44,0x55,0x66};
    char first[24],second[24];
    formatDeviceName(first,sizeof first,primary);formatDeviceName(second,sizeof second,secondary);
    assert(!strcmp(first,"Umbravox-112233") && !strcmp(second,"Umbravox-445566"));
    assert(strcmp(first,second));

    for(auto mode:{OFF,WARDRIVEGO,BISCUIT}){
        bool wd=true,bs=true;int calls=0;
        assert(selectServices(mode,[&](bool biscuit,bool visible){++calls;(biscuit?bs:wd)=visible;return true;}));
        assert(wd==(mode==WARDRIVEGO) && bs==(mode==BISCUIT) && !(wd&&bs));
        assert(calls==(mode==OFF?2:3));
        assert(!strcmp(identity(mode).model,mode==BISCUIT?"Biscuit DIY":expectedModel));
        for(int fail=1;fail<=calls;++fail){int n=0;assert(!selectServices(mode,[&](bool,bool){return ++n!=fail;}));assert(n==fail);}
    }

    {
        FakeScanner s;bool called=false;
        auto r=bridgeinit::configure(s,[&]{called=true;assert(!s.active);return true;});
        assert(called && r.configured && r.paused && r.resumed && s.active && s.stops==1 && s.starts==1);
        s={};s.active=false;
        r=bridgeinit::configure(s,[&]{assert(!s.active);return true;});
        assert(r.configured && !r.paused && !s.stops && !s.starts && !s.active);
        s={};s.stopOk=false;called=false;
        r=bridgeinit::configure(s,[&]{called=true;return true;});
        assert(!r.configured && !called && !s.starts);
        s={};r=bridgeinit::configure(s,[&]{assert(!s.active);return false;});
        assert(!r.configured && r.resumed && s.active && s.starts==1);
        s={};s.startOk=false;r=bridgeinit::configure(s,[&]{return true;});
        assert(r.configured && !r.resumed && !s.active);
    }

    assert(!strcmp(FW_VERSION,"99.99.99"));
    assert(strstr(WD_CAPS,"fw=99.99.99 ") && strstr(WD_CAPS,"ota=0 "));
#if defined(NM_CYD_C5)
    assert(strstr(WD_CAPS,"board_name=Umbravox_NM_CYD_C5"));
#elif defined(WS_C5_28)
    assert(strstr(WD_CAPS,"board_name=Umbravox_WS_C5_28"));
#else
    assert(strstr(WD_CAPS,"board_name=Umbravox_WS_C5_35"));
#endif
    assert(!restartRound(false,120000,0)); // a large table cannot starve its tail
    assert(!restartRound(true,59999,0) && restartRound(true,60000,0));
    Command c;uint8_t b[256];
    size_t n=frame(b,sizeof b,1,0x12345678,"wdstream start -wifi",20);
    assert(n==32 && u32(b+6)==0x12345678);
    assert(c.accept(b,n,0)==1 && c.framed && c.id==0x12345678);
    assert(parse(c.text,WARDRIVEGO).action==START);
    for(size_t i=2;i<n;++i)assert(c.accept(b,i,0)==-1);
    b[2]=2;assert(c.accept(b,n,0)==-1);b[2]=1;
    b[3]=3;assert(c.accept(b,n,0)==-1);b[3]=1;
    b[5]=4;assert(c.accept(b,n,0)==-1);
    n=frame(b,sizeof b,1,42,"wdstream ",9);b[5]=3;
    assert(c.accept(b,n,0)==0);
    n=frame(b,sizeof b,1,42,"start -wifi",11);b[5]=0;
    assert(c.accept(b,n,5)==1 && parse(c.text,WARDRIVEGO).action==START);
    n=frame(b,sizeof b,1,42,"wdstream ",9);b[5]=3;assert(c.accept(b,n,0)==0);
    n=frame(b,sizeof b,1,43,"start -wifi",11);b[5]=2;assert(c.accept(b,n,5)==-1);
    // Assembly cannot grow beyond the command bound, and timed-out fragments
    // cannot be joined to a later continuation.
    n=frame(b,sizeof b,1,7,"stop",4);b[5]=3;assert(c.accept(b,n,0)==0);
    b[5]=2;assert(c.accept(b,n,4000)==-1);
    std::string part(150,'x');n=frame(b,sizeof b,1,7,part.data(),part.size());b[5]=3;
    assert(c.accept(b,n,5000)==0);b[5]=2;assert(c.accept(b,n,5001)==-1);
    // Truncated, oversized, embedded NUL and command-injection inputs rejected.
    std::string huge(193,'x');assert(c.accept((const uint8_t*)huge.data(),huge.size(),0)==-1);
    const uint8_t bad[]={'s','t','o','p',0,'x'};assert(c.accept(bad,sizeof bad,0)==-1);
    assert(parse("stop\nwdstream start -wifi",WARDRIVEGO).action==INVALID);
    assert(parse("wdstream start -wifi EXTRA",WARDRIVEGO).action==UNSUPPORTED);
    assert(parse("iam a motorolaedge2024",WARDRIVEGO).action==IDENTITY);
    assert(parse("iam a ",WARDRIVEGO).action!=IDENTITY);
    assert(parse("iam a phone\nstop",WARDRIVEGO).action==INVALID);
    assert(parse("unknown command",WARDRIVEGO).action==UNSUPPORTED);
    auto selected=parse("wdstream start -wifi -ch 1,6,11,36,161",WARDRIVEGO);
    assert(selected.action==START && selected.channels.enabled);
    assert(selected.channels.allows(1) && selected.channels.allows(161));
    assert(!selected.channels.allows(2) && !selected.channels.allows(0));
    assert(parse("wdstream start -wifi",WARDRIVEGO).channels.allows(2));
    for(auto bad:{"", "0", "197", "256", "999999999", "1,", ",1", "1,,6", "-1", "1x", "1 EXTRA"}){
        std::string cmd=std::string("wdstream start -wifi -ch ")+bad;
        assert(parse(cmd.c_str(),WARDRIVEGO).action!=START);
    }
    assert(parse("CMD:wardrive5g:",BISCUIT).band==2);
    assert(parse("CMD:wardrive:",BISCUIT).band==1);
    assert(parse("CMD:wardriveall:",BISCUIT).band==0);
    assert(parse("CMD:stopscan:",BISCUIT).action==STOP);
    assert(parse("CMD:reboot:",BISCUIT).action==UNSUPPORTED);
    assert(parse("TIME:SYNC:2026-10-30",BISCUIT).action==TIME_SYNC);
    assert(allows(1,6) && !allows(1,36) && allows(2,36) && !allows(0,0));
    Ap ap;ap.mac[5]=0xab;ap.rssi=-40;ap.channel=36;ap.auth=ui::AUTH_WPA2|ui::AUTH_PSK;
    strcpy(ap.ssid,"a,b\n");char line[MAX_LINE];
    assert(apLine(line,sizeof line,ap,WARDRIVEGO));
    assert(strstr(line,"ssid_hex=612c620a") && strstr(line,"bssid=00:00:00:00:00:AB"));
    assert(apLine(line,sizeof line,ap,BISCUIT));assert(strstr(line,"DATA:AP:a_b_"));
    ap.ssid[0]=0;apLine(line,sizeof line,ap,WARDRIVEGO);assert(strstr(line,"hidden=1"));
    assert(!apLine(line,8,ap,WARDRIVEGO));
    memset(ap.ssid,'a',32);ap.ssid[32]=0;assert(apLine(line,sizeof line,ap,WARDRIVEGO));
    // Actual transmit packetizer: reassemble at default and negotiated MTUs.
    for(Mode mode:{WARDRIVEGO,BISCUIT})for(unsigned mtu:{23u,64u,247u,517u}){
        Outbox q;assert(q.put(line,123));std::string received;
        while(auto* l=q.front()){
            size_t consumed;size_t bytes=packet(*l,mode,mtu,b,244,consumed);
            assert(bytes && bytes<=mtu-3 && bytes<=244);
            const size_t hdr=mode==WARDRIVEGO?HEADER:0;
            if(hdr)assert(u32(b+6)==123 && b[3]==3 && size_t(b[10]|uint16_t(b[11])<<8)==consumed);
            // A failed notify leaves the exact same packet available for retry.
            uint8_t retry[244];size_t again;assert(packet(*l,mode,mtu,retry,sizeof retry,again)==bytes);
            assert(!memcmp(b,retry,bytes));received.append((char*)b+hdr,consumed);
            l->offset+=consumed;if(l->offset==l->len)q.pop();
        }
        assert(received==line);
    }
    Outbox q;for(unsigned i=0;i<q.CAP;++i)assert(q.put("line\n"));
    assert(!q.put("overflow") && q.dropped==1);q.front()->offset=2;
    discardUnsent(q);assert(q.count==1 && q.front()->offset==2);
    q.clear();assert(q.put("",99,2));size_t consumed;
    assert(packet(*q.front(),WARDRIVEGO,23,b,sizeof b,consumed)==HEADER && consumed==0);
    q.clear();assert(!q.count);
    ui::Settings settings;assert(!settings.bleBridge);
    ui::applyKnobStep(settings,8,1);assert(settings.bleBridge==WARDRIVEGO);
    ui::applyKnobStep(settings,8,1);assert(settings.bleBridge==OFF);
    ui::applyKnobStep(settings,9,1);assert(settings.bridgeApp==BISCUIT && !settings.bleBridge);
    ui::applyKnobStep(settings,8,1);assert(settings.bleBridge==BISCUIT);
    settings.bleBridge=255;ui::clampSettings(settings);assert(!settings.bleBridge);
    char label[16],value[32];settings.bleBridge=1;settings.bridgeApp=1;
    uiproj::attuneKnob(settings,8,label,sizeof label,value,sizeof value);
    assert(!strcmp(label,"BRIDGE APP") && !strcmp(value,"WARDRIVEGO"));
    for(int h:{240,320}){
        const int w=h==240?320:480;
        const auto layout=uieng::scan(w,h);assert(layout.bridge.w>=44 && layout.bridge.h>=44);
        const auto scanHits=uieng::scanHits(w,h,{});bool toggle=false;
        for(int i=0;i<scanHits.count;++i)if(scanHits.items[i].slot && !strcmp(scanHits.items[i].slot,"bridge")){
            assert(scanHits.items[i].rect==layout.bridge && scanHits.items[i].payload==9);toggle=true;
        }
        assert(toggle);
        scanpres::SceneStore store;ui::State uiState;uiproj::Chrome chrome;
        uiState.set.bleBridge=1;uiproj::scan(store,uiState,chrome);assert(store.scene().bridgeOn);
        const auto on=store.scene();uiState.set.bleBridge=0;uiproj::scan(store,uiState,chrome);
        bool status=false,body=false,field=false;scanpres::sceneDirty(w,h,on,store.scene(),status,body,&field);
        assert(status && !body);

        const int per=uieng::attuneRowsPerPage(h),page=5/per;
        const auto hits=uieng::attuneHits(h==240?320:480,h,page);
        bool found=false;for(int i=0;i<hits.count;++i)if(hits.items[i].action==uieng::Action::SettingStep && hits.items[i].payload==10)found=true;
        assert(found);
    }
    // Dense stationary burst: old 100ms producer loses fresh rows even with
    // a healthy notification sink. Exercise the production outbox admission
    // and packet fragmentation with both a normal and a small negotiated MTU.
    for(unsigned attMtu:{247u,80u}){
        auto run=[&](bool legacy){
            Outbox q; unsigned next=0,delivered=0,expired=0;
            uint32_t lastAp=0,lastTx=0;
            for(uint32_t now=0;now<120000;++now){
                if(next<1000 && apReady(now,0,q.free()) && (!legacy || now-lastAp>=100)){
                    if(now>AP_FRESH_MS){expired+=1000-next;next=1000;}
                    else { assert(q.put("WD:AP ts=0 bssid=02:00:00:00:00:01 ssid_hex=74657374 rssi=-70 ch=6 auth=WPA2 hidden=0\n"));++next;lastAp=now; }
                }
                if(q.front() && now-lastTx>=15){
                    uint8_t bytes[244];size_t consumed=0;
                    assert(packet(*q.front(),WARDRIVEGO,attMtu,bytes,sizeof bytes,consumed));
                    q.front()->offset+=consumed;lastTx=now;
                    if(q.front()->offset==q.front()->len){q.pop();++delivered;}
                }
            }
            assert(q.dropped==0);
            return std::pair<unsigned,unsigned>{delivered,expired};
        };
        const auto old=run(true),fixed=run(false);
        assert(old.first<600 && old.second>400);
        assert(fixed.first==1000 && fixed.second==0);
    }
    // Congestion must stop the producer, while startup and millis wrap remain bounded.
    assert(!apReady(4000,0,5));assert(apReady(4000,0,6));
    assert(!apReady(2999,0,12));assert(apReady(2000,UINT32_MAX-2000,12));
    puts("bridge protocol, bounds, fragmentation, packet retry, queues and UI: PASS");
}
