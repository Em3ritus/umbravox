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
      expect(o.nTrack==3 && o.nRows==0, "domain: track carried; located list empty (no State source yet)"); }

    { codpres::CodexStore st; codex(st, s, c); const auto& o=st.scene();
      expect(o.nRows==3, "codex: all window rows projected");
      expect(o.rows[0].rssi==-48, "codex: ordered strongest-first (-48 leads)");
      expect(o.rows[2].rssi==-80, "codex: weakest last");
      expect(o.pageLo==1 && o.pageHi==3 && o.listTotal==9, "codex: page 1-3 / 9");
      expect(o.rows[0].key[0]=='w' || o.rows[0].key[0]=='b', "codex: key carries record type"); }

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
      expect(o.nPages==3, "attune: 8 knobs / 3 per page -> 3 pages"); }
    { atnpres::AttuneStore st; attune(st, s, c, /*page*/2, /*rowsPerPage*/3); const auto& o=st.scene();
      expect(o.nRows==2 && !strcmp(o.rows[0].label,"AUTOLOG") && !strcmp(o.rows[1].label,"ORIENT"), "attune: page 2 continues knob index (AUTOLOG, ORIENT)"); }

    { asppres::AspectStore st; aspect(st, s, c); const auto& o=st.scene();
      expect(!strcmp(o.sweepName,"PULSE") && o.alertLive && o.packsSkipped==2, "aspect: sweep name + COSMIC alertLive + skipped"); }

    { detpres::DetailStore st; detail(st, s, c); const auto& o=st.scene();
      expect(o.valid && !strcmp(o.name,"FLOCK-CAM") && o.rssi==-58, "detail: identity");
      expect(!strcmp(o.bandChan,"2.4GHZ CH6") && !strcmp(o.threat, ui::threatName(ui::TH_FLOCK)), "detail: band/chan + threat");
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
