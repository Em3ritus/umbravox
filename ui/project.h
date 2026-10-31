// project.h -- ui::State -> per-screen engine Store projections.
//
// The engine Stores hold a STABLE, pre-projected snapshot; this is the pure
// mapping from the live ui::State (ui/model.h, the one struct the UI is allowed
// to know) into each screen's Store. It is the production replacement for the
// test fill() helpers: the device adapter calls these every frame so the engine
// shows live data. Pure and host-testable -- no Arduino, no drawing, no radios.
//
// Chrome inputs (gps/rec/hasCell/batt/now) are computed by the device loop and
// passed in, exactly as the SCAN adapter already does.
#pragma once
#include "oui.h"
#include "model.h"
#include "scan_present.h"
#include "domain_present.h"
#include "codex_present.h"
#include "menu_present.h"
#include "mesh_present.h"
#include "omens_present.h"
#include "echoes_present.h"
#include "attune_present.h"
#include "aspect_present.h"
#include "detail_present.h"
#include "files_present.h"
#include "signatures.h"
#include "wifi_auth.h"
#include "palette.h"
#include <cstdio>
#include <cstdint>
#include <cstring>

namespace uiproj {

using scanr::GnssFix;
using scanr::Rec;

struct Chrome { GnssFix gps; Rec rec = Rec::Off; bool hasCell = false; int batt = 0; uint32_t now = 0; bool pmuOk = false, charging = false; ui::PoiStatus poi = ui::PoiStatus::Ready; };

// --- small, self-contained formatters (no legacy screen deps) ----------------
inline void agoStr(char* o, int n, uint32_t s) {
    if (s < 60) snprintf(o,n,"%uS",(unsigned)s);
    else if (s < 3600) snprintf(o,n,"%uM",(unsigned)(s/60));
    else snprintf(o,n,"%uH",(unsigned)(s/3600));
}
inline void macStr(char* o, const uint8_t* m) {
    snprintf(o, 18, "%02X:%02X:%02X:%02X:%02X:%02X", m[0],m[1],m[2],m[3],m[4],m[5]);
}
inline void macTail(char* o, const uint8_t* m) { snprintf(o, 8, "%02X:%02X", m[4], m[5]); }
// A stable per-record key distinguishing Wi-Fi from BLE (contract section 3):
// record type + address, so a rotating BLE address is never confused for a
// permanent identity.
inline void contactKey(char* o, int n, uint8_t band, const uint8_t* m) {
    snprintf(o, n, "%c:%02x%02x%02x%02x%02x%02x", band == ui::BAND_BLE ? 'b' : 'w',
             m[0],m[1],m[2],m[3],m[4],m[5]);
}

// --- SCAN --------------------------------------------------------------------
inline void scan(scanpres::SceneStore& st, const ui::State& s, const Chrome& c) {
    st.begin(s.set.theme, c.now);
    st.gps(c.gps);
    st.status(s.nNodes, c.hasCell, c.batt, c.rec); st.header(s.set.bleBridge!=0,c.pmuOk,c.charging); st.poi(c.poi);st.storage(ui::storageState(s.sdOk,s.sdWriteError));
    st.bridge(s.set.bleBridge!=0);
    st.counts(s.nets24, s.nets5, s.ble);
    char ch[16]; snprintf(ch, sizeof ch, "CH %02u", (unsigned)s.chan);
    st.channel(ch, s.on5 ? "5 GHz" : "2.4 GHz");
    for (int i = 0; i < ui::CH24; i++) st.setChannel(i, s.load24[i]);
    for (int i = 0; i < ui::CH5;  i++) st.setChannel(ui::CH24 + i, s.load5[i]);
    for (int i = 0; i < s.nContacts; i++) {
        const ui::Contact& k = s.contacts[i];
        st.addBlip(k.angle, k.dist, k.band, c.now - (uint32_t)k.age * 240u);
    }
}

// --- DOMAIN / CODEX ----------------------------------------------------------
// Page sizes come from native geometry. Preserve the collector's source-ranked
// order: sorting only a fetched window creates a misleading global order.
inline int pageOffset(int total, int offset, int perPage) {
    if(total<=0 || perPage<=0)return 0;
    return std::max(0,std::min(offset,((total-1)/perPage)*perPage));
}
inline bool decodeContactKey(const char* key, bool& ble, uint8_t* mac) {
    if(!key || std::strlen(key)!=14 || key[1]!=':' || (key[0]!='w' && key[0]!='b'))return false;
    auto hex=[](char c){return c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:c>='A'&&c<='F'?c-'A'+10:-1;};
    for(int i=0;i<6;++i){int a=hex(key[2+2*i]),b=hex(key[3+2*i]);if(a<0||b<0)return false;mac[i]=(a<<4)|b;}
    ble=key[0]=='b';return true;
}
inline void domain(dompres::DomainStore& st, const ui::State& s, const Chrome& c, int W=480, int H=320) {
    st.begin(s.set.theme,c.now);st.gps(c.gps);st.status(s.nNodes,c.hasCell,c.batt,c.rec); st.header(s.set.bleBridge!=0,c.pmuOk,c.charging); st.poi(c.poi);st.storage(ui::storageState(s.sdOk,s.sdWriteError));
    const auto layout=uieng::domain(W,H);
    const int radius=domr::radarGeom(layout.map).r;
    auto scale=[&](int x){if(!s.plotRadius || s.plotRadius==radius)return x;
        const int v=x*radius;return (v+(v<0?-(int)s.plotRadius/2:(int)s.plotRadius/2))/s.plotRadius;};
    const bool fresh=scanr::gpsState(c.gps)==scanr::GpsState::Fresh;
    st.counts(fresh?s.nPlots:0,s.plotTotal,fresh?s.plotBeyond:0,s.rangeM);
    if(fresh) {
        for(int i=0;i<s.nPlots;++i)st.addDot(scale(s.plots[i].dx),scale(s.plots[i].dy),s.plots[i].band,s.plots[i].threat!=ui::TH_NONE);
        for(int i=0;i<s.nTrack;++i)st.addTrack(scale(s.track[i].dx),scale(s.track[i].dy));
    }
    const int limit=std::max(0,uieng::domainRowBands(layout)-1);
    const int n=std::min((int)s.nSights,limit);
    for(int i=0;i<n;++i){const auto& row=s.sights[i];char key[20];contactKey(key,sizeof key,row.band,row.mac);
        char mac[18];macStr(mac,row.mac);
        st.addRow(key,row.ssid[0]?row.ssid:"<HIDDEN>",row.band,row.rssi,row.chan,mac);}
    st.page(n?1:0,n,s.totalSights);
}
inline void codex(codpres::CodexStore& st, const ui::State& s, const Chrome& c, int W=480, int H=320) {
    st.begin(s.set.theme,c.now);st.gps(c.gps);st.status(s.nNodes,c.hasCell,c.batt,c.rec); st.header(s.set.bleBridge!=0,c.pmuOk,c.charging); st.poi(c.poi);st.storage(ui::storageState(s.sdOk,s.sdWriteError));st.filter(s.filter);
    const int n=std::min((int)s.nSights,uieng::codex(W,H).nRows);
    for(int i=0;i<n;++i){const auto& row=s.sights[i];char key[20];contactKey(key,sizeof key,row.band,row.mac);
        st.addRow(key,row.ssid,row.band,row.rssi,row.chan,false);}
    st.page(n?s.scroll+1:0,n?s.scroll+n:0,s.totalSights);
}

// --- MENU --------------------------------------------------------------------
inline void menu(menpres::MenuStore& st, const ui::State& s, const Chrome& c) {
    st.begin(s.set.theme, c.now);
    st.gps(c.gps);
    st.status(s.nNodes, c.hasCell, c.batt, c.rec); st.header(s.set.bleBridge!=0,c.pmuOk,c.charging); st.poi(c.poi);st.storage(ui::storageState(s.sdOk,s.sdWriteError));
    st.labels(s.set.labels != 0);
}

// --- MESH --------------------------------------------------------------------
inline void mesh(mshpres::MeshStore& st, const ui::State& s, const Chrome& c) {
    st.begin(s.set.theme, c.now);
    st.gps(c.gps);
    st.status(s.nNodes, c.hasCell, c.batt, c.rec); st.header(s.set.bleBridge!=0,c.pmuOk,c.charging); st.poi(c.poi);st.storage(ui::storageState(s.sdOk,s.sdWriteError));
    st.header(s.meshCoords, s.coreCamping, s.coreChan, s.meshLossPct, s.netId, s.enrollSecs);
    for (int i = 0; i < s.nNodes && i < ui::MAX_NODE_ROWS; i++)
        st.addNode(s.nodes[i].id, s.nodes[i].ageS, s.nodes[i].lossPct, s.nodes[i].live,s.nodes[i].diag);
    st.page(s.meshPage);st.local(s.localDiag);
}

// --- OMENS -------------------------------------------------------------------
inline void omens(omnpres::OmenStore& st, const ui::State& s, const Chrome& c) {
    st.begin(s.set.theme, c.now);
    st.gps(c.gps);
    st.status(s.nNodes, c.hasCell, c.batt, c.rec); st.header(s.set.bleBridge!=0,c.pmuOk,c.charging); st.poi(c.poi);st.storage(ui::storageState(s.sdOk,s.sdWriteError));
    st.total(s.omenTotal);
    for (int i = 0; i < s.nOmens; i++)
        st.addRow(ui::threatName(s.omens[i].threat), s.omens[i].name, s.omens[i].conf, s.omens[i].agoS, s.omens[i].located);
}

// --- ECHOES ------------------------------------------------------------------
inline void echoes(echpres::EchoStore& st, const ui::State& s, const Chrome& c) {
    st.begin(s.set.theme, c.now);
    st.gps(c.gps);
    st.status(s.nNodes, c.hasCell, c.batt, c.rec); st.header(s.set.bleBridge!=0,c.pmuOk,c.charging); st.poi(c.poi);st.storage(ui::storageState(s.sdOk,s.sdWriteError));
    st.totals(s.echoTotal, s.echoClients);st.page(s.scroll,s.echoDrops,s.echoEvicted,s.echoAvailable);
    for (int i = 0; i < s.nEchoes; i++) {
        const auto& e=s.echoes[i];char who[18],link[40]="";macStr(who,e.mac);
        if(e.linked){char ap[18];macStr(ap,e.ap);snprintf(link,sizeof link,"SEEN WITH AP %s",ap);}
        st.addRow(e.ssid,who,e.randomised,e.agoS,e.rssi,e.chan,e.observations,e.wildcard,link);
    }
}

// --- ATTUNE (compact knob formatters; 9 knobs in engine order) ---------------
inline int attuneActiveChannels(const ui::Settings& st) { return st.bands == 1 ? ui::CH24 : st.bands == 2 ? ui::CH5 : (ui::CH24 + ui::CH5); }
inline void attuneKnob(const ui::Settings& st, int knob, char* lbl, int ln, char* val, int vn) {
    switch (knob) {
        case 0: snprintf(lbl,ln,"ALERT");  snprintf(val,vn,"%s", ui::confName(st.alertMin)); break;
        case 1: { const int cyc = ((int)st.dwellMs * attuneActiveChannels(st) + 500) / 1000;
                  snprintf(lbl,ln,"DWELL"); snprintf(val,vn,"%uMS (%dS)",(unsigned)st.dwellMs, cyc < 1 ? 1 : cyc); break; }
        case 2: snprintf(lbl,ln,"BLE");    snprintf(val,vn,"%u%%",(unsigned)st.bleDuty); break;
        case 3: snprintf(lbl,ln,"BANDS");  snprintf(val,vn,"%s", st.bands==1?"2.4":st.bands==2?"5 GHZ":"BOTH"); break;
        case 4: snprintf(lbl,ln,"BRIGHT"); snprintf(val,vn,"%u%%",(unsigned)st.bright); break;
        case 5: { const uint16_t v = ui::IDLE_S[st.idleIdx % ui::IDLE_N];
                  snprintf(lbl,ln,"IDLE");
                  if (!v) snprintf(val,vn,"NEVER");
                  else { char u[16]; if (v < 60) snprintf(u,sizeof u,"%uS",(unsigned)v); else snprintf(u,sizeof u,"%uM",(unsigned)(v/60));
                         snprintf(val,vn,"%s %s", u, st.idleMode==ui::IDLE_OFF?"OFF":st.idleMode==ui::IDLE_ANIM?"ANIM":"DIM"); }
                  break; }
        case 12: snprintf(lbl,ln,"RAW PCAP");snprintf(val,vn,"%s",st.pcapEnabled?"ON":"OFF");break;
        case 11: snprintf(lbl,ln,"DISCOVERY TX"); snprintf(val,vn,"%s",st.activeDiscovery?"ACTIVE":"PASSIVE"); break;
        case 9: snprintf(lbl,ln,"NODE PROFILE"); snprintf(val,vn,"%s",st.meshProfile?"DISCOVERY":"TRACKING"); break;
        case 10: snprintf(lbl,ln,"NODE DWELL"); snprintf(val,vn,"%u MS",(unsigned)st.nodeDwellMs); break;
        case 8: snprintf(lbl,ln,"BRIDGE APP"); snprintf(val,vn,"%s",st.bridgeApp==2?"BISCUIT":"WARDRIVEGO"); break;
        case 6: snprintf(lbl,ln,"AUTOLOG"); snprintf(val,vn,"%s", st.autoLog?"ON":"OFF"); break;
        default: snprintf(lbl,ln,"ORIENT"); snprintf(val,vn,"%s", st.orient==ui::ORIENT_PORTRAIT?"PORTRAIT":"LANDSCAPE"); break;
    }
}
// The adapter knows the panel H, so it passes the engine's per-page row count:
// fill the knobs for `page` at `rowsPerPage` and set the page count.
inline void attune(atnpres::AttuneStore& st, const ui::State& s, const Chrome& c, int page, int rowsPerPage) {
    st.begin(s.set.theme, c.now);
    st.gps(c.gps);
    st.status(s.nNodes, c.hasCell, c.batt, c.rec); st.header(s.set.bleBridge!=0,c.pmuOk,c.charging); st.poi(c.poi);st.storage(ui::storageState(s.sdOk,s.sdWriteError));
    if (rowsPerPage < 1) rowsPerPage = 1;
    const int pages = (uieng::kAttuneKnobs + rowsPerPage - 1) / rowsPerPage;
    if (page < 0) page = 0; else if (page >= pages) page = pages - 1;
    for (int i = 0; i < rowsPerPage; i++) {
        const int knob = page * rowsPerPage + i;
        if (knob >= uieng::kAttuneKnobs) break;
        char lbl[16], val[32]; attuneKnob(s.set, knob, lbl, sizeof lbl, val, sizeof val);
        if(knob==12)snprintf(val,sizeof val,"%s D%lu",s.pcapStatus,(unsigned long)s.pcapDrops);
        st.addRow(lbl, val);
    }
    st.paging(page, pages);
}

// --- ASPECT (compact name formatters) ----------------------------------------
inline const char* sweepName(uint8_t id) {
    switch (id) {
        case ui::SW_GAZE: return "GAZE"; case ui::SW_PULSE: return "PULSE"; case ui::SW_STAR: return "STAR";
        case ui::SW_CANDLE: return "CANDLE"; case ui::SW_GALAXY: return "GALAXY"; case ui::SW_ORBIT: return "ORBIT";
        case ui::SW_STARFALL: return "STARFALL"; case ui::SW_PULSAR: return "PULSAR"; case ui::SW_COMET: return "COMET";
        case ui::SW_AURORA: return "AURORA"; default: return "RADAR";
    }
}
inline const char* alertStyleName(uint8_t theme, uint8_t style) {
    if (theme == pal::TH_BAPH0MAU5) return "SUMMONED";
    if (theme != pal::TH_COSMICMUFFIN) return "WARD";
    switch (style) { case ui::AL_VORTEX: return "VORTEX"; case ui::AL_COMET: return "COMET"; case ui::AL_LOCKON: return "LOCK-ON"; default: return "SUPERNOVA"; }
}
inline void aspect(asppres::AspectStore& st, const ui::State& s, const Chrome& c) {
    st.begin(s.set.theme, c.now);
    st.labels(s.set.labels!=0);
    st.gps(c.gps);
    st.status(s.nNodes, c.hasCell, c.batt, c.rec); st.header(s.set.bleBridge!=0,c.pmuOk,c.charging); st.poi(c.poi);st.storage(ui::storageState(s.sdOk,s.sdWriteError));
    const bool live = (s.set.theme == pal::TH_COSMICMUFFIN);
    st.appearance(pal::themeName(s.set.theme), sweepName(s.set.sweep),
                  alertStyleName(s.set.theme, s.set.alertStyle), live, s.packsSkipped);
}

// --- DETAIL ------------------------------------------------------------------
inline void detail(detpres::DetailStore& st, const ui::State& s, const Chrome& c) {
    st.begin(s.set.theme, c.now);
    st.gps(c.gps);
    st.status(s.nNodes, c.hasCell, c.batt, c.rec); st.header(s.set.bleBridge!=0,c.pmuOk,c.charging); st.poi(c.poi);st.storage(ui::storageState(s.sdOk,s.sdWriteError));
    const ui::Detail& v = s.detail;
    if (!v.valid) { st.device(false, "", "", "", 0, 0, "", false, "", 0, "", "", ""); return; }
    char mac[18]; macStr(mac, v.mac);
    char bc[24];
    if (v.band == ui::BAND_BLE) snprintf(bc, sizeof bc, "BLE");
    else snprintf(bc, sizeof bc, "%s CH%u", v.band == ui::BAND_5 ? "5GHZ" : "2.4GHZ", (unsigned)v.chan);
    const bool open = (v.band != ui::BAND_BLE) && (v.auth & (ui::AUTH_ESS|ui::AUTH_IBSS)) && !(v.auth & (ui::AUTH_PRIVACY|ui::AUTH_WPA|ui::AUTH_WPA2|ui::AUTH_WPA3|ui::AUTH_WEP|ui::AUTH_OWE));
    char auth[112] = {};
    if(v.band != ui::BAND_BLE && v.auth) ui::authSummary(v.auth,auth,sizeof auth);
    char first[8], last[8]; agoStr(first, sizeof first, v.firstAgoS); agoStr(last, sizeof last, v.lastAgoS);
    const char* threat = v.threat != ui::TH_NONE ? ui::threatName(v.threat) : "";
    st.device(true, v.ssid, mac, bc, v.band, v.rssi, auth, open, threat, v.conf, v.why, first, last);
    st.cipher(v.band==ui::BAND_BLE?"":ui::cipherSummary(v.auth));
    st.location(v.located,v.lat7,v.lon7,v.bestRssi);
    st.page(s.detailPage);
    st.clients(s.clients);
    for(int i=0;i<s.clients.count && i<ui::CLIENT_ROWS;i++){
        const auto& row=s.clients.rows[i];char line[80],address[18],signal[12];macStr(address,row.mac);
        if(row.hasRssi)snprintf(signal,sizeof signal,"%d dBm",row.rssi);else snprintf(signal,sizeof signal,"RSSI ?");
        snprintf(line,sizeof line,"%s  %s",address,signal);st.clientLine(i,0,line);
        snprintf(line,sizeof line,"%s / %luS AGO / %lu FR",row.evidence==3?"DATA+ASSOC":row.evidence==2?"ASSOC OK":"DATA",(unsigned long)row.ageS,(unsigned long)row.frames);st.clientLine(i,1,line);
        // Locally administered is not proof of randomization or vendor identity.
        snprintf(line,sizeof line,"CH%u %s",row.channel,(row.mac[0]&2)?"LOCAL MAC / VENDOR UNKNOWN":oui::lookup(row.mac));st.clientLine(i,2,line);
    }
    char info[160];
    snprintf(info,sizeof info,"OUI %s",v.band==ui::BAND_BLE?"NOT INFERRED FOR BLE":oui::lookup(v.mac));st.fact(0,info);
    if(v.band==ui::BAND_BLE)snprintf(info,sizeof info,"OBSERVATIONS NOT COUNTED");
    else snprintf(info,sizeof info,"OBSERVATIONS %lu",(unsigned long)v.observations);
    st.fact(1,info);
    const auto& a=v.info;
    snprintf(info,sizeof info,"CAPS %s%s%s%s",!a.valid?"NOT REPORTED":!a.phy?"NO HT/VHT/HE":"",a.phy&1?"HT ":"",a.phy&2?"VHT ":"",a.phy&4?"HE":"");st.fact(2,info);
    char width[16];if(a.width==8080)snprintf(width,sizeof width,"80+80");else if(a.width)snprintf(width,sizeof width,"%u",a.width);else snprintf(width,sizeof width,"?");
    snprintf(info,sizeof info,"WIDTH %s MHz / COUNTRY %s",width,a.country[0]?a.country:"?");st.fact(3,info);
    snprintf(info,sizeof info,"WPS %s",!a.valid?"NOT REPORTED":!a.wps?"NOT ADVERTISED":a.wpsLocked==1?"ADVERTISED / LOCKED":a.wpsLocked==0?"ADVERTISED / UNLOCKED":"ADVERTISED / LOCK ?");st.fact(4,info);
    if(a.valid)snprintf(info,sizeof info,"BEACON %u TU / CAP 0x%04X",a.beaconTu,a.capabilities);else snprintf(info,sizeof info,"BEACON / CAP NOT REPORTED");st.fact(5,info);
    if(v.nameEvidence==3)snprintf(info,sizeof info,"SSID EVIDENCE CONFLICT");
    else if(v.nameEvidence)snprintf(info,sizeof info,"SSID RESOLVED: %s",v.nameEvidence==1?"AP FRAME":"ASSOC EXCHANGE");
    else snprintf(info,sizeof info,"FIRST %s AGO / LAST %s AGO",first,last);st.fact(6,info);
    if (v.histN) st.history(v.hist, v.histN);
}

// --- FILES -------------------------------------------------------------------
inline void fmtBytes(char* o, int n, uint32_t b) {
    if (b >= 1024u*1024u) snprintf(o,n,"%.1fM",(double)b/(1024.0*1024.0));
    else if (b >= 1024u) snprintf(o,n,"%uK",(unsigned)(b/1024u));
    else snprintf(o,n,"%uB",(unsigned)b);
}
inline void files(flspres::FilesStore& st, const ui::State& s, const Chrome& c) {
    st.begin(s.set.theme, c.now);
    st.gps(c.gps);
    st.status(s.nNodes, c.hasCell, c.batt, c.rec); st.header(s.set.bleBridge!=0,c.pmuOk,c.charging); st.poi(c.poi);st.storage(ui::storageState(s.sdOk,s.sdWriteError));
    const int visSel = (s.fileSel >= s.fileScroll && s.fileSel < s.fileScroll + s.nFileRows) ? (s.fileSel - s.fileScroll) : -1;
    const bool deletable = (s.fileSel >= 0 && s.fileSel != s.fileActive);
    st.list(s.fileCard, s.fileTotal, s.fileScroll, visSel, deletable);
    // bar status text
    char bar[24]; uint16_t ink = pal::DIM;
    if (s.fileResult == 1) { snprintf(bar,sizeof bar,"DELETED"); ink = pal::GREEN; }
    else if (s.fileResult == 2) { snprintf(bar,sizeof bar,"FAILED"); ink = pal::RED; }
    else if (s.fileSel >= 0 && s.fileSel == s.fileActive) { snprintf(bar,sizeof bar,"LOGGING"); ink = pal::AMBER; }
    else if (s.fileSel >= 0) { fmtBytes(bar,sizeof bar,s.fileSelSize); ink = pal::BRIGHT; }
    else if (s.fileCard && s.fileTotal) { snprintf(bar,sizeof bar,"TAP A LOG"); ink = pal::DIM; }
    else bar[0] = 0;
    st.bar(bar, ink);
    char csize[12]; fmtBytes(csize, sizeof csize, s.fileSelSize);
    st.modal(s.fileConfirm, s.fileSelName, csize);
    for (int i = 0; i < s.nFileRows && i < (int)ui::FILE_VIS; i++) {
        char sz[12]; fmtBytes(sz, sizeof sz, s.fileRows[i].size);
        st.addRow(s.fileRows[i].name, sz, (s.fileScroll + i) == s.fileActive);
    }
}

}  // namespace uiproj
