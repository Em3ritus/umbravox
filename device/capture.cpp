#include "capture.h"
#include <Arduino.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <esp_heap_caps.h>
#include "signatures.h"
#include "gpstrack.h"
#include "wifi_auth.h"
#include "gnss.h"
#include "layout.h"
#include <math.h>
#include <Print.h>
#include "csv.h"
#include "applog.h"
#include "wiglerow.h"
#include "plotproj.h"
#include "dedup.h"
#include "twinbudget.h"
#include <string.h>

namespace cap {
namespace {

// How long we sit on each channel. Too short and beacons (typically every
// 102ms) are missed; too long and the cycle takes forever. 220ms catches at
// least two beacon intervals per visit, and a full 21-channel cycle lands
// just under 5 seconds.
uint32_t dwellMs = 220;        // adjustable; see ATTUNE
uint8_t  bandMode = 0;         // 0 both, 1 = 2.4 only, 2 = 5 only
int      campIdx = -1;         // >=0: hold this channel index, do not hop

constexpr int N_CHAN = ui::CH24 + ui::CH5;
// The dedup table lives in PSRAM, which is what makes this number a real
// one rather than a budget. A Node is 92 bytes on the C5, so 32768 slots is
// ~3MB of the ~7MB free, and the table is touched once per packet and never
// in the draw path, so the extra latency of external RAM costs nothing that
// matters.
//
// The session rolls over to a new log file and clears this table at
// applog::ROLLOVER_APS uniques, long before it fills: linear probing degrades
// sharply near full. Behind that, dedup.h stops inserts at 15/16 full and
// bounds every probe, so a full table drops new APs instead of freezing.
// To change the size, edit TABLE (and ROLLOVER_APS in applog.h); the asserts
// keep the pair sane.
constexpr int TABLE  = 32768;         // dedup slots, open addressed
static_assert(applog::ROLLOVER_APS * 100u <= (uint32_t)TABLE * 65u,
              "rollover must leave the dedup table <= 65% full");
static_assert(TABLE <= 65536, "order_ and applog::Pending hold 16-bit slot indices");
static_assert(applog::ROLL_HARD_APS > applog::ROLLOVER_APS && applog::ROLL_HARD_APS * 100u <= (uint32_t)TABLE * 85u,
              "the forced-roll ceiling sits above the rollover, below a full table");
constexpr int RECENT = ui::MAX_CONTACTS;

struct Node {
    uint8_t  mac[6];
    char     ssid[ui::SSID_MAX];
    int8_t   rssi;
    uint8_t  band;        // ui::Band
    uint8_t  chan;
    uint8_t  threat, conf;
    uint8_t  finder;      // 0 = this radio, 1..N = node slot + 1
    uint16_t auth;        // ui::AuthBits
    const char* why;
    uint32_t firstMs, lastMs;
    uint32_t firstEpoch;   // UTC, 0 if no fix had a date yet
    int32_t  lat7, lon7;      // where it was heard loudest
    int8_t   bestRssi;
    bool     located;
    int8_t   hist[ui::HIST_N];   // RSSI, one sample per sighting
    uint8_t  histN;              // total written; the ring wraps
};

Node*     table_ = nullptr;
bool*     used_  = nullptr;
uint32_t  nUsed = 0;
// The backstop behind the rollover (dedup.h): inserts stop at INSERT_LIMIT,
// lookups stop after maxProbe_ + 1 probes once they do, and a frame that finds
// no slot is counted here instead of overwriting another AP's slot.
constexpr uint32_t INSERT_LIMIT = dedup::insertLimit(TABLE);
static_assert(applog::ROLL_HARD_APS < INSERT_LIMIT,
              "the forced roll must come before the table refuses inserts");
uint32_t  maxProbe_ = 0;
volatile uint32_t nTableDrops = 0;
volatile uint32_t nLongProbes = 0;   // inserts past PROBE_LIMIT: collision flood?
// The evil-twin pass budget (twinbudget.h) and the passes it skipped.
twinbudget::Bucket twinBudget;
volatile uint32_t nTwinSkips = 0;
static_assert(twinbudget::BURST >= (uint32_t)TABLE, "a pass over a full table must be affordable");
static_assert(applog::ROLLOVER_APS <= dedup::crowdLimit(TABLE),
              "below the rollover no insert may be dropped for a long probe");

// Slots in the order they were first seen, so the codex can show newest
// first without sorting 512 entries every frame.
uint16_t* order_ = nullptr;
uint32_t  nOrder = 0;

// The logger's done-bits, one per first-seen position (see applog.h).
uint8_t*  logBits_ = nullptr;

// The dial's blips. Slot is the MAC's hash, so an AP keeps the same place on
// the dial for the whole drive -- which is the point: the field should look
// like it is moving past you, not reshuffling.
struct Blip { uint16_t angle; uint8_t dist, band; uint32_t lastMs; bool live; };
Blip recent_[RECENT];

volatile uint32_t nFrames = 0, nLocal = 0, nUnique24 = 0, nUnique5 = 0;
// Bumped whenever the table is cleared, so caches built from it (the map)
// know to rebuild.
uint32_t tableGen = 0;

// Devices first discovered by each source, kept as a running count rather
// than derived on demand: scanning 8192 slots per node per frame to answer
// "how many did you find" would cost more than the drawing does.
constexpr int MAX_SRC = 9;              // core + 8 nodes
uint16_t foundBy_[MAX_SRC];
uint16_t loadCount[N_CHAN];     // frames seen during the current dwell
uint8_t  loadPct[N_CHAN];       // published, 0..100

int      hop = 0;               // index into the combined channel plan
uint32_t hopAt = 0;
bool     chanOk[N_CHAN];        // false if the radio refused the channel

// --- probe requests ------------------------------------------------------
//
// A client looking for a named network is telling you it has joined that
// network before. A phone that probes for a hotel SSID has stayed there.
// This is the one capture here that is about people rather than
// infrastructure, which is also why it is worth treating carefully.
//
// Broadcast probes -- the ones with no SSID -- are discarded: they say only
// "a device is here", which the rest of the radio already told us.
constexpr int PROBE_TABLE = 512;
struct Probe {
    uint8_t  mac[6];
    char     ssid[ui::SSID_MAX];
    int8_t   rssi;
    uint8_t  chan;
    uint32_t lastMs;
    bool     used;
};
Probe*   probes = nullptr;
uint16_t nProbes = 0;
uint32_t nProbeFrames = 0;

ui::AlertRec alert_;

// --- deauthentication floods --------------------------------------------
//
// Not a signature: a deauth frame is a normal part of 802.11 and seeing one
// means nothing. Seeing eighty in two seconds means someone is knocking
// clients off a network. So this is a rate, measured in a window, and that
// makes it HIGH confidence when it fires -- it is an observation rather
// than an inference.
constexpr uint32_t DEAUTH_WINDOW_MS = 2000;
constexpr uint32_t DEAUTH_TRIGGER   = 24;      // frames in that window
volatile uint32_t nDeauth = 0;
uint32_t deauthMark = 0, deauthAt = 0, deauthRate = 0;
uint8_t  deauthTarget[6];

portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;

int chanAt(int i);
void insert(const uint8_t* bssid, int8_t rssi, int chanIdx, uint8_t band,
            const char* name, const ui::Hit& sig, uint32_t ageMs, uint8_t src,
            uint16_t auth);

// Keyed per boot (dedup.h): a cluster of BSSIDs precomputed to share one
// probe chain does not collide here. Drawn in begin(), once the radio is up.
uint32_t hashSeed = 0;
uint32_t hashMac(const uint8_t* m) { return dedup::hashMac(m, hashSeed); }

bool macEq(const uint8_t* a, const uint8_t* b) {
    for (int i = 0; i < 6; i++) if (a[i] != b[i]) return false;
    return true;
}

// RSSI to dial radius. -30dBm or stronger sits on top of us, -90 or weaker
// rides the outer ring.
uint8_t rssiToDist(int8_t rssi) {
    int d = ((-(int)rssi) - 30) * 100 / 60;
    if (d < 0) d = 0;
    if (d > 100) d = 100;
    return (uint8_t)d;
}

// Runs on the WiFi task. Keep it short: no printf, no allocation, no blocking.
void onPacket(void* buf, wifi_promiscuous_pkt_type_t type) {
    if (type != WIFI_PKT_MGMT) return;
    const wifi_promiscuous_pkt_t* p = (const wifi_promiscuous_pkt_t*)buf;
    if (p->rx_ctrl.sig_len < 24) return;

    const uint8_t* f = p->payload;
    const uint8_t subtype = (f[0] >> 4) & 0x0F;

    // 12 = deauthentication, 10 = disassociation. Counted, not logged as
    // devices -- what matters is how many arrive, not who sent one.
    if (subtype == 12 || subtype == 10) {
        portENTER_CRITICAL(&mux);
        nDeauth++;
        for (int i = 0; i < 6; i++) deauthTarget[i] = f[16 + i];   // addr3
        portEXIT_CRITICAL(&mux);
        return;
    }

    // 4 = probe request: a client calling out for a network by name.
    if (subtype == 4) {
        if (p->rx_ctrl.sig_len < 26 || !probes) return;
        const uint8_t* src = f + 10;                  // addr2, the client
        if (f[24] != 0) return;                       // first element is SSID
        const uint8_t n = f[25];
        if (!n || n > 32) return;                     // broadcast probe: skip
        if (26 + n > p->rx_ctrl.sig_len) return;

        char name[ui::SSID_MAX];
        int k = 0;
        for (; k < (int)ui::SSID_MAX - 1 && k < n; k++) {
            const char ch = (char)f[26 + k];
            name[k] = (ch >= 32 && ch < 127) ? ch : '.';
        }
        name[k] = 0;

        portENTER_CRITICAL(&mux);
        nProbeFrames++;
        // Deduplicated on the pair, not the client: the same phone asking
        // after four networks is four findings, not one.
        uint32_t h = hashMac(src);
        for (int i = 0; name[i]; i++) { h ^= (uint8_t)name[i]; h *= 16777619u; }
        int slot = (int)(h % PROBE_TABLE);
        for (int probe = 0; probe < PROBE_TABLE; probe++) {
            const int i = (slot + probe) % PROBE_TABLE;
            if (!probes[i].used) {
                if (nProbes < PROBE_TABLE) {
                    probes[i].used = true;
                    for (int b = 0; b < 6; b++) probes[i].mac[b] = src[b];
                    for (int b = 0; b <= k; b++) probes[i].ssid[b] = name[b];
                    nProbes++;
                }
                slot = i;
                break;
            }
            bool same = macEq(probes[i].mac, src);
            for (int b = 0; same && b < (int)ui::SSID_MAX; b++) {
                if (probes[i].ssid[b] != name[b]) same = false;
                if (!name[b]) break;
            }
            if (same) { slot = i; break; }
        }
        probes[slot].rssi   = (int8_t)p->rx_ctrl.rssi;
        probes[slot].chan   = (uint8_t)chanAt(hop);
        probes[slot].lastMs = millis();
        portEXIT_CRITICAL(&mux);
        return;
    }

    // 8 = beacon, 5 = probe response. Both announce an AP.
    if (subtype != 8 && subtype != 5) return;

    const uint8_t* bssid = f + 16;            // addr3
    const int8_t rssi = (int8_t)p->rx_ctrl.rssi;

    // Tagged parameters start after the 24-byte header and the 12-byte fixed
    // body (timestamp, beacon interval, capability). Tag 0 is the SSID; a
    // zero length is a hidden network, which is a finding, not an absence.
    const uint8_t* ssid = nullptr;
    uint8_t ssidLen = 0;
    if (p->rx_ctrl.sig_len >= 38 && f[36] == 0) {
        ssidLen = f[37];
        if (ssidLen > 0 && (36 + 2 + ssidLen) <= p->rx_ctrl.sig_len) ssid = f + 38;
        else ssidLen = 0;
    }

    // The channel comes from our own hop state, not from rx_ctrl: that field
    // is 4 bits wide and cannot represent anything above channel 15, so it is
    // useless for 5GHz.
    const int h = hop;
    const uint8_t band = (h >= ui::CH24) ? ui::BAND_5 : ui::BAND_24;

    // Match before taking the lock -- the tables are const and the matcher
    // touches no shared state.
    char nameBuf[ui::ALERT_NAME];
    int nb = 0;
    for (; nb < ui::ALERT_NAME - 1 && nb < ssidLen && ssid; nb++) {
        const char c = (char)ssid[nb];
        nameBuf[nb] = (c >= 32 && c < 127) ? c : '.';
    }
    nameBuf[nb] = 0;
    const ui::Hit sig = ui::matchWifi(bssid, nameBuf);
    // The beacon body starts after the 24-byte MAC header.
    const uint16_t auth = ui::parseAuth(f + 24, (int)p->rx_ctrl.sig_len - 24);

    portENTER_CRITICAL(&mux);
    nLocal++;                       // heard on our own radio, not relayed
    portEXIT_CRITICAL(&mux);
    insert(bssid, rssi, h, band, nameBuf, sig, 0, 0, auth);
}

// The shared path: dedup, count, spectrum and dial, whether the sighting
// came off our own radio or arrived over the mesh from a node.
void insert(const uint8_t* bssid, int8_t rssi, int chanIdx, uint8_t band,
            const char* name, const ui::Hit& sig, uint32_t ageMs, uint8_t src,
            uint16_t auth) {
    const bool is5 = band == ui::BAND_5;
    const int h = chanIdx;

    portENTER_CRITICAL(&mux);
    nFrames++;
    if (sig.threat != ui::TH_NONE) {
        // Keep the strongest hit, and otherwise the newest -- a HIGH from a
        // moment ago matters more than a LOW right now.
        if (sig.conf >= alert_.conf || (millis() - alert_.atMs) > 6000) {
            alert_.threat = sig.threat;
            alert_.conf   = sig.conf;
            alert_.why    = sig.why;
            for (int k = 0; k < ui::ALERT_NAME - 1 && name[k]; k++) alert_.name[k] = name[k];
            alert_.atMs   = millis();
        }
    }
    if (h >= 0 && h < N_CHAN && loadCount[h] < 0xFFFF) loadCount[h]++;

    const uint32_t hsh = hashMac(bssid);
    // Bounded, and FULL rather than an overwrite once the table is past its
    // insert limit (dedup.h). A dropped frame is counted and changes nothing.
    const dedup::Hit hit = dedup::find(hsh, TABLE, nUsed, INSERT_LIMIT, maxProbe_,
        [](uint32_t i) { return used_[i]; },
        [bssid](uint32_t i) { return macEq(table_[i].mac, bssid); });
    if (hit.kind == dedup::FULL) {
        nTableDrops++;
        portEXIT_CRITICAL(&mux);
        return;
    }
    const int slot = hit.slot;
    if (hit.kind == dedup::INSERT) {
        const int i = slot;
        used_[i] = true;
        for (int k = 0; k < 6; k++) table_[i].mac[k] = bssid[k];
        table_[i].band = band;
        table_[i].ssid[0] = 0;
        table_[i].firstMs = millis();
        table_[i].firstEpoch = ui::Gnss::utcEpoch(millis());
        table_[i].threat = ui::TH_NONE;
        table_[i].conf = ui::CONF_LOW;
        table_[i].why = "";
        table_[i].histN = 0;
        table_[i].located = false;
        table_[i].bestRssi = -127;
        table_[i].finder = src;
        table_[i].auth = 0;
        if (src < MAX_SRC) foundBy_[src]++;
        order_[nOrder++] = (uint16_t)i;
        nUsed++;
        if (is5) nUnique5++; else nUnique24++;
        dedup::noteInsert(maxProbe_, hit);
        if (dedup::longProbe(hit)) nLongProbes++;
    }
    table_[slot].rssi   = rssi;
    table_[slot].chan   = (uint8_t)chanAt(h);
    table_[slot].lastMs = millis();
    table_[slot].hist[table_[slot].histN % ui::HIST_N] = rssi;
    if (table_[slot].histN < 255) table_[slot].histN++;

    // Record position at the loudest sighting. A weaker later one says less
    // about where the thing actually is.
    if (rssi > table_[slot].bestRssi) {
        int32_t la, lo;
        if (gpstrack::at(millis(), ageMs, la, lo)) {
            table_[slot].lat7 = la;
            table_[slot].lon7 = lo;
            table_[slot].located = true;
            table_[slot].bestRssi = rssi;
        }
    }
    // --- evil twin ------------------------------------------------------
    //
    // Same SSID from a second BSSID is not suspicious by itself: that is
    // what every mesh, extender and enterprise roaming setup looks like.
    // What a legitimate deployment does *not* do is advertise different
    // security on the same name. A disagreement there means one of them is
    // not what it claims.
    //
    // The pass walks the whole table, so it draws from a budget of entry
    // visits (twinbudget.h): under a beacon flood it is skipped and counted
    // rather than run for every fake SSID. And the lock is released every
    // CHUNK visits so interrupts are never off for the whole walk. That is
    // safe on this single-core part: every insert() runs on the Wi-Fi task
    // (promiscuous and ESP-NOW receive callbacks), and the only other writers,
    // reset()/rollover(), run on loopTask, which cannot preempt it; nothing that
    // can run in the gap (ISRs, the few higher-priority tasks) touches the
    // table. The pass only reads other entries. On a dual-core build the lock
    // is simply held for the whole pass, as before.
    bool twin = dedup::runTwinPass(hit, name, auth);
    if (twin && !twinBudget.take(millis(), nOrder)) { nTwinSkips++; twin = false; }
    if (twin) {
        for (uint32_t q = 0; q < nOrder; q++) {
#if CONFIG_FREERTOS_UNICORE
            if (twinbudget::chunkDue(q)) { portEXIT_CRITICAL(&mux); portENTER_CRITICAL(&mux); }
#endif
            Node& o = table_[order_[q]];
            if (&o == &table_[slot] || !o.ssid[0] || !o.auth) continue;
            if (o.auth == auth) continue;
            bool sameName = true;
            for (int k = 0; k < (int)ui::SSID_MAX; k++) {
                if (o.ssid[k] != name[k]) { sameName = false; break; }
                if (!o.ssid[k]) break;
            }
            if (!sameName) continue;

            // Also require different hardware. Plenty of consumer routers
            // put 2.4 and 5GHz on separate BSSIDs and manage to advertise
            // different security on each -- but those share an OUI, because
            // they are one device. A clone runs on someone else's radio.
            //
            // An attacker who spoofs the OUI too will slip past this. That
            // is the trade: a rule that fires on every dual-band router in
            // the street is one you stop reading.
            if (o.mac[0] == bssid[0] && o.mac[1] == bssid[1] && o.mac[2] == bssid[2])
                continue;

            // MED, not HIGH: a half-reconfigured AP looks exactly like this.
            table_[slot].threat = ui::TH_ROGUE;
            table_[slot].conf   = ui::CONF_MED;
            table_[slot].why    = "SSID AUTH CLASH";
            if (alert_.conf <= ui::CONF_MED || (millis() - alert_.atMs) > 6000) {
                alert_.threat = ui::TH_ROGUE;
                alert_.conf   = ui::CONF_MED;
                alert_.why    = "SSID AUTH CLASH";
                for (int k = 0; k < ui::ALERT_NAME - 1 && name[k]; k++)
                    alert_.name[k] = name[k];
                alert_.atMs = millis();
            }
            break;
        }
    }

    // Keep the strongest verdict this device has ever earned, so a probe
    // response with no SSID does not erase a beacon's match.
    if (sig.threat != ui::TH_NONE && sig.conf >= table_[slot].conf) {
        table_[slot].threat = sig.threat;
        table_[slot].conf   = sig.conf;
        table_[slot].why    = sig.why;
    }
    // Keep the first name we get. A later beacon from the same BSSID carries
    // the same SSID, and a probe response may carry none.
    // Keep the first non-zero reading: a probe response carries no RSN
    // element, and letting one overwrite a beacon's would blank it.
    if (auth) table_[slot].auth = auth;
    if (name && name[0] && table_[slot].ssid[0] == 0) {
        int k = 0;
        for (; k < (int)ui::SSID_MAX - 1 && name[k]; k++) table_[slot].ssid[k] = name[k];
        table_[slot].ssid[k] = 0;
    }

    Blip& b = recent_[hsh % RECENT];
    b.angle  = (uint16_t)(hsh % 360);
    b.dist   = rssiToDist(rssi);
    b.band   = band;
    b.lastMs = table_[slot].lastMs;
    b.live   = true;
    portEXIT_CRITICAL(&mux);
}

int chanAt(int i) { return i < ui::CH24 ? ui::CHAN24[i] : ui::CHAN5[i - ui::CH24]; }


// Park the radio. Band has to be selected before the channel on a dual-band
// part, and set_channel rejects a channel the current regulatory domain does
// not allow -- which is information worth keeping rather than ignoring.
bool park(int i) {
    const bool is5 = i >= ui::CH24;
    esp_wifi_set_band(is5 ? WIFI_BAND_5G : WIFI_BAND_2G);
    return esp_wifi_set_channel((uint8_t)chanAt(i), WIFI_SECOND_CHAN_NONE) == ESP_OK;
}

} // namespace

void begin() {
    // PSRAM. The internal-RAM fallback below cannot succeed: the table is
    // ~3 MB and internal RAM is 320 KB, so without PSRAM the unit halts at
    // boot with the FATAL serial message. Kept as written, not relied on.
    table_ = (Node*)heap_caps_malloc(sizeof(Node) * TABLE, MALLOC_CAP_SPIRAM);
    used_  = (bool*)heap_caps_malloc(sizeof(bool) * TABLE, MALLOC_CAP_SPIRAM);
    order_ = (uint16_t*)heap_caps_malloc(sizeof(uint16_t) * TABLE, MALLOC_CAP_SPIRAM);
    logBits_ = (uint8_t*)heap_caps_calloc((TABLE + 7) / 8, 1, MALLOC_CAP_SPIRAM);
    const bool inPsram = table_ && used_ && order_ && logBits_;
    if (!inPsram) {
        free(table_); free(used_); free(order_); free(logBits_);
        table_ = (Node*)calloc(TABLE, sizeof(Node));
        used_  = (bool*)calloc(TABLE, sizeof(bool));
        order_ = (uint16_t*)calloc(TABLE, sizeof(uint16_t));
        logBits_ = (uint8_t*)calloc((TABLE + 7) / 8, 1);
    }
    if (!table_ || !used_ || !order_ || !logBits_) {
        Serial.println("capture: FATAL -- no room for the dedup table");
        for (;;) delay(1000);
    }
    Serial.printf("capture: %d dedup slots in %s (%u bytes, node %u B), psram %u free\n",
                  TABLE, inPsram ? "PSRAM" : "internal RAM",
                  (unsigned)(sizeof(Node) * TABLE), (unsigned)sizeof(Node),
                  (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));

    for (int i = 0; i < TABLE; i++) used_[i] = false;
    nOrder = 0;
    for (int i = 0; i < RECENT; i++) recent_[i].live = false;
    for (int i = 0; i < N_CHAN; i++) { loadCount[i] = 0; loadPct[i] = 0; chanOk[i] = true; }
    for (int i = 0; i < MAX_SRC; i++) foundBy_[i] = 0;

    probes = (Probe*)heap_caps_malloc(sizeof(Probe) * PROBE_TABLE, MALLOC_CAP_SPIRAM);
    if (!probes) probes = (Probe*)calloc(PROBE_TABLE, sizeof(Probe));
    if (probes) for (int i = 0; i < PROBE_TABLE; i++) probes[i].used = false;
    Serial.printf("capture: %d probe slots\n", probes ? PROBE_TABLE : 0);

    WiFi.mode(WIFI_MODE_STA);
    // With the radio on, esp_random() is the hardware RNG. Before any frame
    // can reach insert() (the promiscuous callback is registered below).
    hashSeed = esp_random();
    WiFi.disconnect();
    delay(100);

    // AUTO is what makes esp_wifi_set_band() legal later; in a single-band
    // mode the band cannot be switched at runtime.
    const esp_err_t be = esp_wifi_set_band_mode(WIFI_BAND_MODE_AUTO);
    Serial.printf("capture: band mode AUTO -> %s\n", esp_err_to_name(be));

    wifi_promiscuous_filter_t filt = {};
    filt.filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT;
    esp_wifi_set_promiscuous_filter(&filt);
    esp_wifi_set_promiscuous_rx_cb(&onPacket);
    esp_wifi_set_promiscuous(true);

    // Probe every channel once so a refusal is reported at startup rather
    // than silently costing coverage for the whole drive.
    int ok24 = 0, ok5 = 0;
    for (int i = 0; i < N_CHAN; i++) {
        chanOk[i] = park(i);
        if (chanOk[i]) { if (i < ui::CH24) ok24++; else ok5++; }
    }
    Serial.printf("capture: channels usable -- 2.4GHz %d/%d, 5GHz %d/%d\n",
                  ok24, ui::CH24, ok5, ui::CH5);
    if (ok5 == 0)
        Serial.println("capture: NO 5GHz channels accepted (regulatory domain?)");

    hop = 0;
    while (hop < N_CHAN && !chanOk[hop]) hop++;
    if (hop >= N_CHAN) hop = 0;
    park(hop);
    hopAt = millis();
}

void tick(uint32_t nowMs) {
    // Deauth rate, evaluated on its own clock rather than per frame.
    if (nowMs - deauthAt >= DEAUTH_WINDOW_MS) {
        deauthAt = nowMs;
        const uint32_t n = nDeauth;
        deauthRate = n - deauthMark;
        deauthMark = n;
        if (deauthRate >= DEAUTH_TRIGGER) {
            portENTER_CRITICAL(&mux);
            alert_.threat = ui::TH_DEAUTH;
            alert_.conf   = ui::CONF_HIGH;
            alert_.why    = "FLOOD";
            snprintf(alert_.name, ui::ALERT_NAME, "%02X:%02X:%02X %lu/S",
                     deauthTarget[3], deauthTarget[4], deauthTarget[5],
                     (unsigned long)(deauthRate * 1000 / DEAUTH_WINDOW_MS));
            alert_.atMs = nowMs;
            portEXIT_CRITICAL(&mux);
        }
    }

    // Camping: the Core does this once it has nodes. It is not a sacrifice
    // -- channel 1 carries real traffic, so the Core becomes its full-time
    // listener while also being where the nodes can reach it.
    if (campIdx >= 0) {
        if (hop != campIdx) { hop = campIdx; park(hop); hopAt = nowMs; }
        return;
    }
    if (nowMs - hopAt < dwellMs) return;

    // Publish this dwell's frame count as a 0..100 load before moving on.
    portENTER_CRITICAL(&mux);
    const uint16_t seen = loadCount[hop];
    loadCount[hop] = 0;
    portEXIT_CRITICAL(&mux);

    // ~12 management frames in a 220ms dwell is a busy channel; scale to that
    // and clamp. This is an activity indicator, not a measurement.
    // Scale by dwell, or a longer dwell would read as a busier channel.
    int pct = (int)seen * 100 * 220 / (12 * (int)dwellMs);
    if (pct > 100) pct = 100;
    // Ease toward the new value so the bars do not strobe.
    loadPct[hop] = (uint8_t)((loadPct[hop] * 2 + pct) / 3);

    for (int n = 1; n <= N_CHAN; n++) {
        const int i = (hop + n) % N_CHAN;
        if (!chanOk[i]) continue;
        const bool is5 = i >= ui::CH24;
        if (bandMode == 1 && is5) continue;
        if (bandMode == 2 && !is5) continue;
        hop = i;
        break;
    }
    park(hop);
    hopAt = nowMs;
}

void fill(ui::State& s, uint32_t nowMs) {
    portENTER_CRITICAL(&mux);
    const uint32_t u24 = nUnique24, u5 = nUnique5;
    Blip snap[RECENT];
    for (int i = 0; i < RECENT; i++) snap[i] = recent_[i];
    portEXIT_CRITICAL(&mux);

    s.nets24 = (uint16_t)(u24 > 65535 ? 65535 : u24);
    s.nets5  = (uint16_t)(u5  > 65535 ? 65535 : u5);

    s.on5  = hop >= ui::CH24;
    s.chan = (uint8_t)chanAt(hop);
    for (int i = 0; i < ui::CH24; i++) s.load24[i] = loadPct[i];
    for (int i = 0; i < ui::CH5;  i++) s.load5[i]  = loadPct[ui::CH24 + i];

    // Two thirds of the dial for WiFi; blescan::fill() appends into the rest.
    constexpr int WIFI_SLOTS = ui::MAX_CONTACTS * 2 / 3;
    int n = 0;
    for (int i = 0; i < RECENT && n < WIFI_SLOTS; i++) {
        if (!snap[i].live) continue;
        const uint32_t age = nowMs - snap[i].lastMs;
        if (age > 60000) continue;       // a minute without a beacon: drop it
        ui::Contact& c = s.contacts[n++];
        c.angle = snap[i].angle;
        c.dist  = snap[i].dist;
        c.band  = snap[i].band;
        c.age   = (uint8_t)(age / 240 > 255 ? 255 : age / 240);
    }
    s.nContacts = (uint8_t)n;
}

void fillDetail(ui::State& s, int index, bool only5, uint32_t nowMs) {
    s.detail = ui::Detail{};
    int seen = 0;
    for (int i = (int)nOrder - 1; i >= 0; i--) {
        const Node& nd = table_[order_[i]];
        if (only5 && nd.band != ui::BAND_5) continue;
        if (seen++ != index) continue;

        ui::Detail& d = s.detail;
        d.valid = true;
        for (int k = 0; k < ui::SSID_MAX; k++) d.ssid[k] = nd.ssid[k];
        for (int k = 0; k < 6; k++) d.mac[k] = nd.mac[k];
        d.rssi = nd.rssi;  d.band = nd.band;  d.chan = nd.chan;
        d.threat = nd.threat; d.conf = nd.conf;
        d.auth = nd.auth;
        for (int k = 0; k < 19 && nd.why && nd.why[k]; k++) d.why[k] = nd.why[k];
        d.firstAgoS = (nowMs - nd.firstMs) / 1000;
        d.lastAgoS  = (nowMs - nd.lastMs) / 1000;

        // Unwrap the ring oldest-first, so the chart reads left to right.
        const int n = nd.histN < ui::HIST_N ? nd.histN : ui::HIST_N;
        const int start = nd.histN < ui::HIST_N ? 0 : nd.histN % ui::HIST_N;
        for (int k = 0; k < n; k++) d.hist[k] = nd.hist[(start + k) % ui::HIST_N];
        d.histN = (uint8_t)n;
        return;
    }
}

// Runs every frame (main sizes the scrollbar from it on every screen). The
// 5GHz count used to walk the whole table -- ~20k PSRAM misses per frame at
// rollover size -- but it is exactly nUnique5: a node's band is set once, at
// insert, alongside that counter, and both are cleared together.
uint16_t sightCount(bool only5) {
    const uint32_t c = only5 ? nUnique5 : nOrder;
    return (uint16_t)(c > 65535 ? 65535 : c);
}

void fillSights(ui::State& s, int offset, bool only5) {
    s.totalSights = sightCount(only5);
    int n = 0, skipped = 0;
    // Newest first: that is the order you care about while moving.
    for (int i = (int)nOrder - 1; i >= 0 && n < ui::MAX_SIGHTS; i--) {
        const Node& nd = table_[order_[i]];
        if (only5 && nd.band != ui::BAND_5) continue;
        if (skipped < offset) { skipped++; continue; }
        ui::Sighting& g = s.sights[n++];
        int k = 0;
        for (; k < ui::SSID_MAX - 1 && nd.ssid[k]; k++) g.ssid[k] = nd.ssid[k];
        g.ssid[k] = 0;
        for (int m = 0; m < 6; m++) g.mac[m] = nd.mac[m];
        g.rssi = nd.rssi;
        g.band = nd.band;
        g.chan = nd.chan;
    }
    s.nSights = (uint8_t)n;
}

// A sighting reported by a node. Band and channel come off the wire, so the
// spectrum shows mesh-wide activity rather than only what this radio heard.
void ingest(const uint8_t* bssid, int8_t rssi, uint8_t chan, uint8_t band,
            const char* ssid, uint32_t ageMs, uint8_t src, uint16_t auth) {
    int idx = -1;
    for (int i = 0; i < N_CHAN; i++) if (chanAt(i) == chan) { idx = i; break; }
    if (idx < 0) return;
    const ui::Hit sig = ui::matchWifi(bssid, ssid);
    insert(bssid, rssi, idx, band, ssid, sig, ageMs, src, auth);
}

uint16_t foundBy(uint8_t src) { return src < MAX_SRC ? foundBy_[src] : 0; }

// Project located APs onto the map, relative to where we are now.
//
// Metres per degree of latitude is near enough constant; per degree of
// longitude it shrinks with the cosine of the latitude, and ignoring that
// stretches the map east-west by a third at this latitude (plotproj.h).
//
// At 20,000 located APs the old per-frame pass (two divides and a dozen
// soft-float ops per node) was estimated at 60-140 ms a frame on the C5. Now
// an integer bounding box culls before any float work, and the pass runs at
// most every plotproj::REFRESH_MS; frames in between reuse its result.
namespace {
plotproj::Cache plotCache;
ui::Plot plotsC[ui::MAX_PLOT];
uint8_t  nPlotsC = 0;
uint16_t plotTotalC = 0, plotBeyondC = 0;
}

void fillPlots(ui::State& s, int rangeM, int32_t myLat7, int32_t myLon7, bool haveFix) {
    s.rangeM = (uint16_t)rangeM;
    const uint32_t now = millis();
    if (plotproj::stale(plotCache, now, rangeM, haveFix, tableGen)) {
        nPlotsC = 0; plotTotalC = 0; plotBeyondC = 0;
        if (haveFix && rangeM > 0) {
            const plotproj::Proj p = plotproj::make(rangeM, myLat7, myLon7, ui::MAP_R);
            for (uint32_t i = 0; i < nOrder; i++) {
                const Node& nd = table_[order_[i]];
                if (!nd.located) continue;
                plotTotalC++;
                int px, py;
                if (!plotproj::project(p, nd.lat7, nd.lon7, px, py)) { plotBeyondC++; continue; }
                if (nPlotsC >= ui::MAX_PLOT) continue;
                ui::Plot& o = plotsC[nPlotsC++];
                o.dx = (int16_t)px;
                o.dy = (int16_t)py;     // north is up
                o.band = nd.band;
                o.threat = nd.threat;
            }
        }
        plotproj::remember(plotCache, now, rangeM, haveFix, tableGen);
    }
    s.nPlots = nPlotsC; s.plotTotal = plotTotalC; s.plotBeyond = plotBeyondC;
    for (int i = 0; i < nPlotsC; i++) s.plots[i] = plotsC[i];
}

// Newest first. `randomised` flags a locally-administered address, which is
// what a modern phone uses when it is trying not to be tracked -- worth
// showing, because an un-randomised probe is a far stronger identifier.
void fillEchoes(ui::State& s, int offset, uint32_t nowMs) {
    s.nEchoes = 0;
    s.echoTotal = nProbes;
    if (!probes) return;

    // Count distinct clients for the summary.
    uint16_t clients = 0;
    for (int i = 0; i < PROBE_TABLE; i++) {
        if (!probes[i].used) continue;
        bool seen = false;
        for (int j = 0; j < i && !seen; j++)
            if (probes[j].used && macEq(probes[j].mac, probes[i].mac)) seen = true;
        if (!seen) clients++;
    }
    s.echoClients = clients;

    int n = 0, skipped = 0;
    uint32_t newest = 0;
    for (int pass = 0; pass < ui::MAX_ECHO_ROWS + offset; pass++) {
        int best = -1;
        uint32_t bestMs = 0;
        for (int i = 0; i < PROBE_TABLE; i++) {
            if (!probes[i].used) continue;
            if (pass && probes[i].lastMs >= newest) continue;
            if (probes[i].lastMs > bestMs) { bestMs = probes[i].lastMs; best = i; }
        }
        if (best < 0) break;
        newest = bestMs;
        if (skipped < offset) { skipped++; continue; }
        if (n >= ui::MAX_ECHO_ROWS) break;

        ui::Echo& e = s.echoes[n++];
        for (int k = 0; k < (int)ui::SSID_MAX; k++) e.ssid[k] = probes[best].ssid[k];
        for (int k = 0; k < 6; k++) e.mac[k] = probes[best].mac[k];
        e.rssi = probes[best].rssi;
        e.chan = probes[best].chan;
        e.agoS = (nowMs - probes[best].lastMs) / 1000;
        e.randomised = (probes[best].mac[0] & 0x02) != 0;
    }
    s.nEchoes = (uint8_t)n;
}

uint16_t echoCount()  { return nProbes; }
uint32_t probeFrames() { return nProbeFrames; }

uint32_t deauths()    { return nDeauth; }
uint32_t deauthPerS() { return deauthRate * 1000 / DEAUTH_WINDOW_MS; }

// --- WiGLE export --------------------------------------------------------
//
// Rows only: main writes the markers and the header, because the file is
// shared with the BLE exporter and only one of them can own it.
//
// Printed rather than buffered: thousands of rows will not fit in RAM.
namespace {

// A row without coordinates is not a wardrive observation, and one without a
// wall clock files every sighting under 1970. Both are dropped rather than
// faked. firstEpoch is set once, at first sight, so a row with none never
// gains one; a position can still arrive with a later, louder sighting.
applog::Verdict verdictOf(const Node& x, uint32_t nowMs, bool final) {
    return applog::holdVerdict(x.located, x.firstEpoch, x.firstMs, x.lastMs, nowMs, final);
}
// Exportable at all (the console dump's rule: no hold).
bool exportable(const Node& x) { return x.located && x.firstEpoch; }

// One WigleWifi-1.6 row, newline included (bytes pinned in wiglerow.h).
int formatRow(const Node& nd, char* buf, int cap) {
    char auth[64];
    ui::authString(nd.auth, auth, sizeof auth);
    return wiglerow::wifi(buf, cap, nd.mac, nd.ssid, auth, nd.firstEpoch,
                          nd.chan, nd.rssi, nd.lat7, nd.lon7);
}

// The table as the logger sees it: rows in first-seen order. Everything that
// touches table_/order_ takes the same lock the Wi-Fi task inserts under, and
// a row is copied out before it is formatted, so a sighting landing mid-row
// cannot tear it.
struct LogSrc : applog::Source {
    uint32_t count() override {
        portENTER_CRITICAL(&mux);
        const uint32_t n = nOrder;
        portEXIT_CRITICAL(&mux);
        return n;
    }
    uint32_t capacity() override { return TABLE; }
    uint8_t* doneBits() override { return logBits_; }
    applog::Verdict verdict(uint32_t i, uint32_t nowMs, bool final) override {
        applog::Verdict v = applog::WAIT;
        portENTER_CRITICAL(&mux);
        if (i < nOrder) v = verdictOf(table_[order_[i]], nowMs, final);
        portEXIT_CRITICAL(&mux);
        return v;
    }
    int format(uint32_t i, char* buf, int cap) override {
        Node nd;
        bool have = false;
        portENTER_CRITICAL(&mux);
        if (i < nOrder) { nd = table_[order_[i]]; have = true; }
        portEXIT_CRITICAL(&mux);
        if (!have || !exportable(nd)) return 0;
        return formatRow(nd, buf, cap);
    }
} logSrc;

} // namespace

applog::Source& logSource() { return logSrc; }

// The whole table at once, for the console WIGLE dump. Same rows, same bytes
// as the SD file (formatRow is shared).
uint32_t exportRows(Print& out, uint32_t& skipped) {
    char row[applog::ROW_MAX];
    uint32_t rows = 0;
    const uint32_t n = logSrc.count();
    for (uint32_t i = 0; i < n; i++) {
        const int len = logSrc.format(i, row, sizeof row);
        if (len <= 0) { skipped++; continue; }
        out.write((const uint8_t*)row, (size_t)len);
        rows++;
    }
    return rows;
}

uint16_t locatedCount() {
    uint32_t c = 0;
    for (uint32_t i = 0; i < nOrder; i++) if (table_[order_[i]].located) c++;
    return (uint16_t)(c > 65535 ? 65535 : c);
}

void setDwell(uint16_t ms)  { dwellMs = ms < 20 ? 20 : ms; }

void setCamp(int chan) {
    if (chan <= 0) { campIdx = -1; return; }
    for (int i = 0; i < N_CHAN; i++)
        if (chanAt(i) == chan) { campIdx = i; return; }
    campIdx = -1;
}

bool camping() { return campIdx >= 0; }

void setBands(uint8_t mode) {
    bandMode = mode;
    // Jump immediately if the current channel is now excluded, rather than
    // sitting out the rest of a dwell on a band that was just turned off.
    const bool is5 = hop >= ui::CH24;
    if ((mode == 1 && is5) || (mode == 2 && !is5)) hopAt = 0;
}

void reset() {
    portENTER_CRITICAL(&mux);
    for (int i = 0; i < TABLE; i++) used_[i] = false;
    for (int i = 0; i < RECENT; i++) recent_[i].live = false;
    for (int i = 0; i < N_CHAN; i++) { loadCount[i] = 0; loadPct[i] = 0; }
    nUsed = 0; nOrder = 0; nFrames = 0; nLocal = 0; nUnique24 = 0; nUnique5 = 0;
    maxProbe_ = 0; nTableDrops = 0; nLongProbes = 0;
    nProbes = 0; nProbeFrames = 0;
    if (probes) for (int i = 0; i < PROBE_TABLE; i++) probes[i].used = false;
    for (int i = 0; i < MAX_SRC; i++) foundBy_[i] = 0;
    alert_ = ui::AlertRec{};
    tableGen++;
    portEXIT_CRITICAL(&mux);
}

// The rollover clear: the dedup table and the counts that describe it. The
// dial's blips, the alert, the probe table and the frame counters stay, so
// the screen carries on as it was. Called from the main loop (logging::tick),
// the same task and point as RESET COUNTERS, so no draw is mid-way through
// the table; the lock keeps the Wi-Fi task's inserts out while it empties.
void rollover() {
    portENTER_CRITICAL(&mux);
    memset(used_, 0, sizeof(bool) * TABLE);
    nUsed = 0; nOrder = 0; nUnique24 = 0; nUnique5 = 0;
    maxProbe_ = 0;                  // an empty table has no probe chains
    for (int i = 0; i < MAX_SRC; i++) foundBy_[i] = 0;
    tableGen++;
    portEXIT_CRITICAL(&mux);
}

bool lastAlert(ui::AlertRec& out) {
    portENTER_CRITICAL(&mux);
    out = alert_;
    portEXIT_CRITICAL(&mux);
    return out.threat != ui::TH_NONE;
}

// frames() is mesh-wide: every sighting that reached the table, local or
// relayed. localFrames() is only what this radio heard, which is what the
// Core's own row on the MESH screen must show.
uint32_t frames()      { return nFrames; }
uint32_t localFrames() { return nLocal; }
uint32_t uniques()  { return nUnique24 + nUnique5; }
uint16_t tableUse() { return (uint16_t)(nUsed > 65535 ? 65535 : nUsed); }
uint16_t tableCap() { return (uint16_t)(TABLE > 65535 ? 65535 : TABLE); }
uint32_t tableDrops() { return nTableDrops; }
uint32_t longProbes() { return nLongProbes; }
uint32_t twinSkips()  { return nTwinSkips; }

} // namespace cap
