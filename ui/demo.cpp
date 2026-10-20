#include "demo.h"
#include "nav.h"
#include "signatures.h"
#include <string.h>
#include <stdio.h>

namespace ui {
namespace {

// Deterministic, so a screenshot is comparable across runs and a palette
// change is the only thing that moves between two renders.
struct Rng {
    uint32_t s;
    explicit Rng(uint32_t seed) : s(seed ? seed : 1) {}
    uint32_t next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
    int range(int lo, int hi) { return lo + (int)(next() % (uint32_t)(hi - lo + 1)); }
};

// Invented but plausible: 2.4GHz piles onto 1/6/11, 5GHz spreads out, and the
// contact field is dense enough to show what a real drive looks like.
}  // namespace

State demoState(uint32_t frame) {
    State s;
    Rng r(0xA57E);

    s.nets24 = 1247;
    s.nets5  = 418;
    s.ble    = 2903;

    const int busy24[CH24] = {92, 18, 24, 31, 44, 88, 22, 19, 27, 36, 79, 15, 11};
    for (int i = 0; i < CH24; i++) s.load24[i] = (uint8_t)busy24[i];
    const int busy5[CH5] = {54, 38, 61, 27, 45, 19, 33, 22};
    for (int i = 0; i < CH5; i++) s.load5[i] = (uint8_t)busy5[i];

    // Park somewhere that moves with the frame, so the spectrum marker and the
    // header badge are exercised rather than always showing channel 1.
    const int hop = (int)((frame / 8) % (CH24 + CH5));
    s.on5  = hop >= CH24;
    s.chan = (uint8_t)(s.on5 ? 36 + (hop - CH24) * 4 : hop + 1);

    s.gpsFix = true;
    s.sats   = 11;
    s.sdOk   = true;
    snprintf(s.logName, sizeof(s.logName), "W0042");

    s.uptimeS = 5047 + frame;
    s.frame   = frame;

    // A codex page: a mix of named, long and hidden networks across both
    // bands, so the row layout gets exercised by the awkward cases too.
    static const struct { const char* ssid; int8_t rssi; uint8_t band, chan; } DEMO[] = {
        {"HOLLOWBROOK-5G",   -42, BAND_5,  149},
        {"linksys",          -58, BAND_24,   6},
        {"",                 -61, BAND_24,  11},
        {"ATT-WIFI-9920",    -67, BAND_24,   1},
        {"Verkada-AC-0417",  -71, BAND_5,   36},
        {"xfinitywifi",      -79, BAND_24,  11},
        {"SpectrumSetup-2F", -84, BAND_24,   3},
        {"NETGEAR58",        -91, BAND_24,   9},
    };
    s.totalSights = 23;
    s.nSights = (uint8_t)(sizeof(DEMO) / sizeof(DEMO[0]));
    if (s.nSights > MAX_SIGHTS) s.nSights = MAX_SIGHTS;
    for (int i = 0; i < s.nSights; i++) {
        Sighting& g = s.sights[i];
        int k = 0;
        for (; k < SSID_MAX - 1 && DEMO[i].ssid[k]; k++) g.ssid[k] = DEMO[i].ssid[k];
        g.ssid[k] = 0;
        for (int m = 0; m < 6; m++) g.mac[m] = (uint8_t)(0x10 * i + m);
        g.rssi = DEMO[i].rssi;
        g.band = DEMO[i].band;
        g.chan = DEMO[i].chan;
    }
    s.scroll = 4;

    // Probes: a mix of randomised and not, because the distinction is the
    // point of the column.
    static const struct { const char* ssid; uint8_t m4, m5; bool rnd; uint32_t age; } EC[] = {
        {"Hilton_Honors",  0x4B, 0x2A, false,   8},
        {"CHARTER-A41F",   0x9C, 0x07, true,   31},
        {"Starbucks WiFi", 0x4B, 0x2A, false,  74},
        {"eduroam",        0x22, 0xD1, true,  160},
        {"Pixel_8832",     0xE0, 0x5F, true,  402},
        {"BOINGO",         0x4B, 0x2A, false, 915},
    };
    s.nEchoes = MAX_ECHO_ROWS; s.echoTotal = 31; s.echoClients = 9;
    for (int i = 0; i < s.nEchoes; i++) {
        Echo& e = s.echoes[i];
        int k = 0; for (; k < SSID_MAX-1 && EC[i].ssid[k]; k++) e.ssid[k] = EC[i].ssid[k];
        e.ssid[k] = 0;
        e.mac[4] = EC[i].m4; e.mac[5] = EC[i].m5;
        e.randomised = EC[i].rnd; e.agoS = EC[i].age;
    }

    static const struct { uint8_t th, cf; const char* nm; uint32_t age; bool loc; } OM[] = {
        {TH_FLOCK,   CONF_HIGH, "B4:1E:52",   14,  true},
        {TH_DEAUTH,  CONF_HIGH, "3C:A7 92/S", 96,  true},
        {TH_GLASSES, CONF_MED,  "4F:2A:08",  210,  true},
        {TH_ROGUE,   CONF_MED,  "HOLLOWBRO", 455, false},
        {TH_TRACKER, CONF_LOW,  "7E:31:AA",  881,  true},
        {TH_CAMERA,  CONF_LOW,  "A4:C1:38", 1604,  true},
    };
    s.nOmens = MAX_OMEN_ROWS;
    s.omenTotal = 23;
    for (int i = 0; i < s.nOmens; i++) {
        Omen& o = s.omens[i];
        o.threat = OM[i].th; o.conf = OM[i].cf;
        int k = 0; for (; k < SSID_MAX-1 && OM[i].nm[k]; k++) o.name[k] = OM[i].nm[k];
        o.name[k] = 0;
        o.agoS = OM[i].age; o.located = OM[i].loc;
    }

    s.rangeM = 250; s.plotTotal = 46; s.plotBeyond = 7;
    s.nPlots = 39;
    for (int i = 0; i < s.nPlots; i++) {
        // Clustered along a notional road rather than scattered evenly --
        // which is what a drive actually produces.
        const int along = (int)(r.next() % 80) - 40;
        const int off   = (int)(r.next() % 26) - 13;
        s.plots[i].dx = (int16_t)(along * 4 / 5);
        s.plots[i].dy = (int16_t)(off + along / 3);
        const int roll = (int)(r.next() % 100);
        s.plots[i].band = roll < 55 ? BAND_24 : roll < 80 ? BAND_5 : BAND_BLE;
        s.plots[i].threat = (r.next() % 11) ? TH_NONE : TH_FLOCK;
    }
    s.nTrack = MAX_TRACK;
    for (int i = 0; i < s.nTrack; i++) {
        const int a = -34 + i * 3;
        s.track[i].dx = (int16_t)(a * 4 / 5);
        s.track[i].dy = (int16_t)(a / 3);
    }

    s.coreCamping = true; s.coreChan = 1; s.coreFrames = 4182;
    s.meshHits = 18061; s.coreFound = 41;
    // The second node is the case worth showing: camped on a busy channel,
    // relaying the most of anyone, and discovering almost nothing new.
    static const struct { uint8_t id[3]; uint8_t n; bool camp; uint32_t recs; uint16_t found; uint8_t loss; uint32_t age; } ND[] = {
        {{0xA7,0xB4,0x54}, 12, false, 3184, 112,  2,  2},
        {{0x1C,0x9F,0x30},  1, true,  9022,   6,  9,  5},
        {{0x44,0x0B,0xE2},  8, false, 1455,  78, 31, 14},
        {{0x7E,0x31,0xAA},  0, false,  318,   3, 68, 47},
    };
    s.nNodes = 4;
    s.meshLossPct = 14;
    for (int i = 0; i < 4; i++) {
        NodeRow& r = s.nodes[i];
        r.live = true;
        for (int k = 0; k < 3; k++) r.id[k] = ND[i].id[k];
        r.nChan = ND[i].n; r.camp = ND[i].camp;
        r.recs = ND[i].recs; r.found = ND[i].found;
        r.lossPct = ND[i].loss; r.ageS = ND[i].age;
    }

    // A device for the detail screen: a signal that has been climbing, which
    // is the case the chart exists to make visible.
    {
        Detail& v = s.detail;
        v.valid = true;
        strcpy(v.ssid, "HOLLOWBROOK-5G");
        const uint8_t m[6] = {0xB4, 0x1E, 0x52, 0x3C, 0xA7, 0x19};
        for (int i = 0; i < 6; i++) v.mac[i] = m[i];
        v.band = BAND_5; v.chan = 149;
        v.threat = TH_FLOCK; v.conf = CONF_HIGH;
        strcpy(v.why, "FLOCK-MA-L");
        v.firstAgoS = 842; v.lastAgoS = 3;
        v.histN = HIST_N;
        for (int i = 0; i < HIST_N; i++) {
            const int base = -88 + i * 40 / HIST_N;
            v.hist[i] = (int8_t)(base + (int)(r.next() % 7) - 3);
        }
        v.rssi = v.hist[HIST_N - 1];
    }

    // Every twelfth frame-block, raise a demo alert so the card's layout can
    // be judged without waiting for a real sighting.
    if ((frame / 60) % 2 == 1) {
        s.alertActive = true;
        s.alertThreat = TH_FLOCK;
        s.alertConf   = CONF_HIGH;
        strcpy(s.alertWhy,  "FLOCK-MA-L");
        strcpy(s.alertName, "B4:1E:52");
        // ~16ms a frame, so the card's whole arrival fits in the first
        // second of the window.
        s.alertAge = (uint16_t)((frame % 60) * 16);
    }
    s.set.bright = 80;
    s.set.dwellMs = 600;
    s.set.bleDuty = 30;

    const float beam = (float)((frame * 5) % 360);
    s.nContacts = 34;
    for (int i = 0; i < s.nContacts; i++) {
        Contact& k = s.contacts[i];
        k.angle = (uint16_t)r.range(0, 359);
        k.dist  = (uint8_t)r.range(12, 100);
        const int roll = r.range(0, 99);
        k.band  = (uint8_t)(roll < 45 ? BAND_24 : roll < 65 ? BAND_5 : BAND_BLE);

        // Age is time since the beam last crossed it, which is exactly what the
        // device will report from millis(). Gives the dial its trailing comet
        // for free instead of faking a decay.
        const int behind = ((int)beam - (int)k.angle + 360) % 360;
        k.age = (uint8_t)(behind * 255 / 360);
    }
    return s;
}


}  // namespace ui
