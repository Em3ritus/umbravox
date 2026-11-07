// Host test for ui/project.h -- the ui::State -> per-screen Store projections.
// Builds one representative ui::State and asserts each projection lands the live
// fields in its screen's Store scene (the production replacement for the test
// fill() helpers). Pure; links the theme model + signature/auth name tables.
// Build:
//   g++ -std=c++17 -I. -Isim sim/test_project.cpp ui/palette.cpp ui/themepack.cpp \
//       ui/muffinseal.cpp ui/motif.cpp ui/canvas.cpp ui/font5x7.cpp ui/signatures.cpp \
//       ui/wifi_auth.cpp -o /tmp/tpj -lm
#include "../ui/project.h"
#include "../ui/signatures.h"
#include "../ui/wifi_auth.h"
#include <cstdio>
#include <cstring>
using namespace uiproj;

static int fails=0, checks=0;
static void expect(bool c,const char* m){ ++checks; if(!c){++fails;printf("  FAIL %s\n",m);} else printf("  ok   %s\n",m); }

static ui::State makeState() {
    ui::State s;
    s.nets24 = 1665; s.nets5 = 418; s.ble = 2903; s.chan = 6; s.on5 = false;
    for (int i=0;i<ui::CH24;i++) s.load24[i]=10+i; for (int i=0;i<ui::CH5;i++) s.load5[i]=20+i;
    s.gpsFix = true; s.sats = 12; s.nNodes = 3;
    // contacts (SCAN field)
    s.nContacts = 3;
    for (int i=0;i<3;i++){ s.contacts[i]={(uint16_t)(i*120),(uint8_t)(20+i*10),(uint8_t)i,(uint8_t)(i*5)}; }
    // sights (CODEX)
    s.nSights = 3; s.totalSights = 9; s.scroll = 0; s.filter = 2;
    const char* ss[3] = {"NEST-CAM","cafe-guest",""};
    const int8_t rs[3] = {-80,-48,-63};
    for (int i=0;i<3;i++){ snprintf(s.sights[i].ssid, ui::SSID_MAX, "%s", ss[i]);
        for(int k=0;k<6;k++) s.sights[i].mac[k]=(uint8_t)(i*16+k); s.sights[i].rssi=rs[i]; s.sights[i].band=(uint8_t)i; s.sights[i].chan=(uint8_t)(6+i); }
    // plots (DOMAIN)
    s.nPlots=4; s.plotTotal=9; s.plotBeyond=3; s.rangeM=250;
    for (int i=0;i<4;i++){ s.plots[i]={(int16_t)(i*5-10),(int16_t)(i*4-8),(uint8_t)(i%3),(uint8_t)(i==0?ui::TH_FLOCK:ui::TH_NONE)}; }
    s.nTrack=3; for(int i=0;i<3;i++) s.track[i]={(int16_t)(-i*4),(int16_t)(-i*3),0,0};
    // mesh
    s.nNodes=2; for(int i=0;i<2;i++){ s.nodes[i].live=true; s.nodes[i].id[0]=0x10; s.nodes[i].id[1]=(uint8_t)(0xA0+i); s.nodes[i].id[2]=(uint8_t)i; s.nodes[i].ageS=i*10; s.nodes[i].lossPct=i*7; }
    s.meshCoords=0; s.coreCamping=true; s.coreChan=6; s.meshLossPct=7; s.netId=2; s.enrollSecs=0;
    // omens
    s.nOmens=2; s.omenTotal=5;
    s.omens[0]={ui::TH_FLOCK, ui::CONF_HIGH, {0}, 0, 42, true}; snprintf(s.omens[0].name,ui::SSID_MAX,"cam-7");
    s.omens[1]={ui::TH_TRACKER, ui::CONF_MED, {0}, 0, 300, false}; snprintf(s.omens[1].name,ui::SSID_MAX,"tag");
    // echoes
    s.nEchoes=2; s.echoTotal=11; s.echoClients=4;
    snprintf(s.echoes[0].ssid,ui::SSID_MAX,"HOTEL-WIFI"); s.echoes[0].randomised=false; s.echoes[0].agoS=12; for(int k=0;k<6;k++) s.echoes[0].mac[k]=(uint8_t)(0xA0+k);
    snprintf(s.echoes[1].ssid,ui::SSID_MAX,"home"); s.echoes[1].randomised=true; s.echoes[1].agoS=240;
    // settings
    s.set.theme=pal::TH_COSMICMUFFIN; s.set.sweep=ui::SW_PULSE; s.set.alertStyle=ui::AL_COMET;
    s.set.alertMin=ui::CONF_MED; s.set.dwellMs=220; s.set.bleDuty=30; s.set.bands=0; s.set.bright=80; s.set.labels=1;
    s.packsSkipped=2;
    // detail
    s.detail.valid=true; snprintf(s.detail.ssid,ui::SSID_MAX,"FLOCK-CAM"); for(int k=0;k<6;k++) s.detail.mac[k]=(uint8_t)(0xAA+k);
    s.detail.rssi=-58; s.detail.band=ui::BAND_24; s.detail.chan=6; s.detail.threat=ui::TH_FLOCK; s.detail.conf=ui::CONF_HIGH;
    s.detail.auth=ui::AUTH_WPA2|ui::AUTH_PSK; snprintf(s.detail.why,20,"OUI+SSID"); s.detail.firstAgoS=10800; s.detail.lastAgoS=2;
    s.detail.histN=6; for(int i=0;i<6;i++) s.detail.hist[i]=(int8_t)(-80+i*4);
    // files
    s.fileCard=true; s.nFileRows=3; s.fileTotal=9; s.fileScroll=0; s.fileSel=1; s.fileActive=0;
    for(int i=0;i<3;i++){ snprintf(s.fileRows[i].name,ui::FILE_NAME_MAX,"WD-2026012%d",i); s.fileRows[i].size=(uint32_t)(1000000*(i+1)); }
    snprintf(s.fileSelName,ui::FILE_NAME_MAX,"WD-20260121"); s.fileSelSize=820000; s.fileConfirm=false; s.fileResult=0;
    return s;
}

int main(){
    pal::setTheme(pal::TH_UMBRAVOX);
    ui::State s = makeState();
    Chrome c; c.gps = {true,3,12,500}; c.rec = scanr::Rec::On; c.hasCell=true; c.batt=78; c.now=5000;

    { scanpres::SceneStore st; scan(st, s, c); const auto& o=st.scene();
      expect(o.wifi==1665 && o.g5==418 && o.ble==2903, "scan: counts");
      expect(o.nBlips==3 && !strcmp(o.ch,"CH 06"), "scan: blips + channel label");
      expect(o.channels[0]==10 && o.channels[ui::CH24]==20, "scan: 2.4 then 5 channel loads"); }

    { dompres::DomainStore st; domain(st, s, c); const auto& o=st.scene();
      expect(o.nDots==4 && o.dots[0].threat && !o.dots[1].threat, "domain: dots + threat ring on plot 0");
      expect(o.plotShown==4 && o.plotTotal==9 && o.plotBeyond==3 && o.rangeM==250, "domain: counts/range");
      expect(o.nTrack==3 && o.nRows==3, "domain: track and three located rows supplied"); }

    { codpres::CodexStore st; codex(st, s, c); const auto& o=st.scene();
      expect(o.nRows==3, "codex: all window rows projected");
      expect(o.rows[0].rssi==-80, "codex: preserves collector order");
      expect(o.rows[2].rssi==-63, "codex: third source record stays third");
      expect(o.pageLo==1 && o.pageHi==3 && o.listTotal==9, "codex: page 1-3 / 9");
      expect(o.rows[0].key[0]=='w' || o.rows[0].key[0]=='b', "codex: key carries record type"); }

    for(int W:{480,320}) {
        const int H=W==480?320:240, pageSize=uieng::codex(W,H).nRows;
        ui::State many=s; many.nSights=8;many.totalSights=8;many.scroll=0;
        for(int i=0;i<8;++i){many.sights[i]={};many.sights[i].mac[5]=i;many.sights[i].rssi=-80+i;}
        codpres::CodexStore store;codex(store,many,c,W,H);
        expect(store.scene().nRows==pageSize && store.scene().pageHi==pageSize,"geometry limits visible rows and pager together");
        int seen=0;
        for(int offset=0;offset<8;offset+=pageSize){
            many.scroll=offset;many.nSights=std::min(8-offset,ui::MAX_SIGHTS);
            for(int i=0;i<many.nSights;++i)many.sights[i].mac[5]=offset+i;
            codex(store,many,c,W,H);
            expect(store.scene().pageLo==offset+1 && store.scene().pageHi==std::min(8,offset+pageSize),"last page reports exactly visible records");
            for(int i=0;i<store.scene().nRows;++i){bool ble;uint8_t mac[6];
                expect(decodeContactKey(store.scene().rows[i].key,ble,mac)&&!ble&&mac[5]==seen++,"every record remains reachable by exact identity");}
        }
        expect(seen==8 && pageOffset(8,999,pageSize)==((8-1)/pageSize)*pageSize && pageOffset(0,99,pageSize)==0,"page bounds cover all eight records without duplication");
        auto maps=s;maps.plotRadius=domr::radarGeom(uieng::domain(W,H).map).r;
        maps.nPlots=2;maps.plots[0].dx=maps.plotRadius;maps.plots[0].dy=0;
        maps.plots[1].dx=0;maps.plots[1].dy=-maps.plotRadius;
        dompres::DomainStore domainStore;domain(domainStore,maps,c,W,H);
        expect(domainStore.scene().dots[0].dx==maps.plotRadius && domainStore.scene().dots[1].dy==-maps.plotRadius,"native east/north rim positions preserve correct geographic scale");
        auto stale=c;stale.gps.ageMs=5000;domain(domainStore,maps,stale,W,H);
        expect(domainStore.scene().nDots==0 && domainStore.scene().nTrack==0,"stale GPS never presents old positions as current");
        expect(domainStore.scene().nRows==(W==480?3:2) && domainStore.scene().listTotal==9,"located rows remain available without live fix");
    }
    {bool ble;uint8_t mac[6];
        expect(decodeContactKey("b:001122aabbff",ble,mac)&&ble&&mac[5]==255,"BLE address decodes independently of current filter");
        expect(!decodeContactKey("alert",ble,mac)&&!decodeContactKey("w:001122aabbxz",ble,mac)&&!decodeContactKey("w:0011",ble,mac),"invalid and truncated identities cannot select another record");}


    {
        auto full=s;
        const char* name="0123456789ABCDEFGHIJKLMNOPQRSTUV";
        static_assert(sizeof(full.detail.ssid)==33,"all 32 SSID bytes fit");
        strcpy(full.detail.ssid,name);strcpy(full.sights[0].ssid,name);
        full.detail.valid=true;full.detail.located=true;full.detail.lat7=-1234567;full.detail.lon7=-876321234;full.detail.bestRssi=-37;
        full.detail.auth=ui::AUTH_WPA2|ui::AUTH_EAP|ui::AUTH_CCMP|ui::AUTH_ESS;
        detpres::DetailStore detailStore;detail(detailStore,full,c);
        const auto& ds=detailStore.scene();
        expect(!strcmp(ds.name,name),"detail preserves every byte of full 32-character SSID");
        expect(strstr(ds.auth,"ENT") && !strcmp(ds.cipher,"CCMP"),"detail exposes captured enterprise AKM and cipher");
        expect(ds.located && ds.bestRssi==-37 && !strcmp(ds.coordinates,"-0.1234567, -87.6321234"),"location retains sign and seven decimal places");
        for(int W:{480,320}) {
            dompres::DomainStore dom;domain(dom,full,c,W,W==480?320:240);
            codpres::CodexStore cod;codex(cod,full,c,W,W==480?320:240);
            expect(!strcmp(dom.scene().rows[0].label,name)&&!strcmp(cod.scene().rows[0].label,name),"native list snapshots preserve full SSID at both sizes");
            expect(dom.scene().rows[0].rssi==full.sights[0].rssi && dom.scene().rows[0].chan==full.sights[0].chan && strlen(dom.scene().rows[0].mac)==17,"DOMAIN rows carry full BSSID and actual radio data");
        }
    }
    { menpres::MenuStore st; menu(st, s, c); expect(st.scene().plain, "menu: plain label mode from Settings"); }

    { mshpres::MeshStore st; mesh(st, s, c); const auto& o=st.scene();
      expect(o.nRows==2 && o.rows[0].id[1]==0xA0 && o.netId==2 && o.coreChan==6, "mesh: nodes + header"); }

    { omnpres::OmenStore st; omens(st, s, c); const auto& o=st.scene();
      expect(o.nRows==2 && o.total==5 && !strcmp(o.rows[0].omen, ui::threatName(ui::TH_FLOCK)) && o.rows[0].located,
             "omens: threat name + located"); }

    { echpres::EchoStore st; echoes(st, s, c); const auto& o=st.scene();
      expect(o.nRows==2 && o.clients==4 && !strcmp(o.rows[0].seeking,"HOTEL-WIFI") && o.rows[1].randomised,
             "echoes: seeking + clients + randomised"); }

    { atnpres::AttuneStore st; attune(st, s, c, /*page*/0, /*rowsPerPage*/3); const auto& o=st.scene();
      expect(o.nRows==3 && !strcmp(o.rows[0].label,"ALERT") && !strcmp(o.rows[0].value,"MED"), "attune: ALERT=MED");
      expect(o.nPages==4, "attune: 10 radio/recording knobs / 3 per page -> 4 pages"); }
    { atnpres::AttuneStore st; attune(st, s, c, /*page*/2, /*rowsPerPage*/3); const auto& o=st.scene();
      expect(o.nRows==3 && !strcmp(o.rows[0].label,"NODE PROFILE") && !strcmp(o.rows[1].label,"NODE DWELL") && !strcmp(o.rows[2].label,"DISCOVERY TX"), "attune: page 2 maps original setting IDs after display controls move"); }

    { auto value=s;value.detailPage=1;value.detail.info.valid=true;value.detail.info.width=80;value.detail.info.phy=3;value.detail.observations=123;
      detpres::DetailStore one,two;detail(one,value,c);value.detail.observations=124;detail(two,value,c);
      expect(one.scene().page==1 && !strcmp(one.scene().facts[1],"OBSERVATIONS 123") && strstr(one.scene().facts[3],"80 MHz"),"AP info page projects observed metadata");
      bool status,body;detpres::sceneDirty(one.scene(),two.scene(),status,body);expect(body&&!status,"AP metadata changes invalidate detail body");
      for(int height:{240,320}){const int per=uieng::attuneRowsPerPage(height);atnpres::AttuneStore settings;snprintf(value.pcapStatus,sizeof value.pcapStatus,"WRITE ERROR");value.pcapDrops=12;attune(settings,value,c,9/per,per);
        expect(!strcmp(settings.scene().rows[9%per].label,"RAW PCAP") && strstr(settings.scene().rows[9%per].value,"D12"),"PCAP error/drop status on final settings page at both sizes");}
    }

    { asppres::AspectStore st; aspect(st, s, c); const auto& o=st.scene();
      expect(o.plain==bool(s.set.labels) && !strcmp(o.sweepName,"PULSE") && o.alertLive && o.packsSkipped==2, "aspect: sweep name + COSMIC alertLive + skipped"); }

    {
      auto value=s; value.detail.auth=0;
      detpres::DetailStore unknown;detail(unknown,value,c);
      expect(!unknown.scene().authOpen && !strcmp(unknown.scene().cipher,"NOT REPORTED"),"unknown security is not declared open");
      value.detail.auth=ui::AUTH_WPA2|ui::AUTH_PSK|ui::AUTH_CCMP;
      detpres::DetailStore first;detail(first,value,c);
      value.detail.auth=ui::AUTH_WPA2|ui::AUTH_PSK|ui::AUTH_TKIP;
      detpres::DetailStore second;detail(second,value,c);
      bool status=false,body=false;detpres::sceneDirty(first.scene(),second.scene(),status,body);
      expect(!status && body,"cipher-only change invalidates detail body");
    }
    { detpres::DetailStore st; detail(st, s, c); const auto& o=st.scene();
      expect(o.valid && !strcmp(o.name,"FLOCK-CAM") && o.rssi==-58, "detail: identity");
      expect(!strcmp(o.bandChan,"2.4GHZ CH6") && !strcmp(o.threat, ui::threatName(ui::TH_FLOCK)), "detail: band/chan + threat");
      expect(!strcmp(o.cipher,"NOT REPORTED"), "detail: absent cipher never inferred from WPA2");
      expect(o.histN==6 && o.hist[0]==-80, "detail: RSSI history carried");
      ui::State gone=s; gone.detail.valid=false; detpres::DetailStore st2; detail(st2, gone, c);
      expect(!st2.scene().valid, "detail: invalid selection -> GONE"); }

    { flspres::FilesStore st; files(st, s, c); const auto& o=st.scene();
      expect(o.card && o.nRows==3 && o.total==9, "files: rows + card");
      expect(o.rows[0].active && !o.rows[1].active, "files: active flag on the logging session (row 0)");
      expect(o.sel==1 && o.deletable, "files: selection maps to visible index; deletable (not active)"); }

    printf("\n%s  %d/%d checks passed\n", fails?"FAILED":"PASSED", checks-fails, checks);
    return fails?1:0;
}
