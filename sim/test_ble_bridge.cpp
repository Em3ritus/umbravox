#include "../device/ble_bridge_protocol.h"
#include "../ui/nav.h"
#include "../ui/project.h"
#include <cassert>
#include <string>
#include <cstdio>
using namespace bridgewire;
int main(){
    assert(!strcmp(FW_VERSION,"99.99.99"));
    assert(strstr(WD_CAPS,"fw=99.99.99 ") && strstr(WD_CAPS,"ota=0 "));
    assert(strstr(WD_CAPS,"board_name=Umbravox_WS_C5_35"));
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
    ui::applyKnobStep(settings,8,1);assert(settings.bleBridge==BISCUIT);
    ui::applyKnobStep(settings,8,1);assert(settings.bleBridge==OFF);
    settings.bleBridge=255;ui::clampSettings(settings);assert(!settings.bleBridge);
    char label[16],value[32];settings.bleBridge=1;
    uiproj::attuneKnob(settings,8,label,sizeof label,value,sizeof value);
    assert(!strcmp(label,"BRIDGE") && !strcmp(value,"WARDRIVEGO"));
    for(int h:{240,320}){
        const int per=uieng::attuneRowsPerPage(h),page=8/per;
        const auto hits=uieng::attuneHits(h==240?320:480,h,page);
        bool found=false;for(int i=0;i<hits.count;++i)if(hits.items[i].action==uieng::Action::SettingStep && hits.items[i].payload==9)found=true;
        assert(found);
    }
    puts("bridge protocol, bounds, fragmentation, packet retry, queues and UI: PASS");
}
