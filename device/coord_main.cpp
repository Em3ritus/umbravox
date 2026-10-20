// Coordinator firmware: a C5 that owns a cluster of nodes on one report
// channel and forwards their deduplicated sightings to the CYD.
//
// A coordinator is the middle tier of the three-tier mesh. It does NOT sniff
// the air itself -- it is the Core for its own handful of nodes, reusing
// meshcore to hear them, assign them channels and track their loss, exactly
// as the CYD does when it runs the mesh alone. The only differences from the
// CYD-as-Core are:
//
//   * it plans over the band SLICE the CYD gave it, not the whole band
//     (meshcore::setSlice), and reports on the slice's own report channel;
//   * it folds node sightings into its OWN deduped store rather than the
//     display table (meshcore::setSink), and periodically hops to the
//     collection channel to deliver them to the CYD as ClusterBatch frames,
//     mirroring node_main.cpp::report()'s batch/flush discipline.
//
// Boot sequence: park on the collection channel, broadcast CoordReg until the
// CYD answers with a CoordAssign (learned in the collector hook), then learn
// the cluster id / report channel / collection channel / slice, feed the
// slice to meshcore and start managing the cluster.
#include <Arduino.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <esp_now.h>
#include <esp_mac.h>
#include "mesh.h"
#include "meshcore.h"

namespace {

constexpr uint32_t CREG_MS   = 1000;   // CoordReg cadence while unassigned
constexpr uint32_t REPORT_MS = 2500;   // how often to deliver to the CYD
// The coordinator's deduped store. A cluster is a handful of nodes and the
// CYD holds the big table; this only has to buffer what has changed since the
// last delivery. 512 BSSIDs at ~36 bytes is ~18KB.
constexpr int      STORE     = 512;

uint8_t  selfMac[6];
uint8_t  bcast[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};

// The CYD, learned from the CoordAssign's sender so batches go unicast --
// acknowledged and retried by the radio, where broadcast is neither.
uint8_t  cydMac[6];
bool     haveCyd = false;

bool     assigned   = false;
uint8_t  clusterId  = 0;
uint8_t  reportChan = mesh::REPORT_CHAN;   // where this cluster's nodes report
uint8_t  collectChan = mesh::REPORT_CHAN;  // where we deliver to the CYD
uint8_t  slice[mesh::MAX_SLICE];
uint8_t  sliceN = 0;
bool     sliceApplied = false;

uint32_t lastCreg = 0, lastReport = 0;

// --- the coordinator's own deduped sightings -----------------------------
struct Sight {
    uint8_t  bssid[6];
    int8_t   rssi;
    uint8_t  chan, band, ssidLen;
    char     ssid[20];
    uint16_t auth;
    uint32_t lastMs;
    bool     dirty;      // changed since the last delivery
    bool     used;
};
Sight    store_[STORE];
uint32_t folded = 0, relayed = 0, storeDrop = 0;
portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;

// Frame accounting, as in node_main: ESP_OK from esp_now_send() means queued,
// not delivered, so queued and confirmed are counted apart.
uint32_t txQueued = 0, txOk = 0, txFail = 0;

bool is5(uint8_t ch) { return ch > 14; }

void park(uint8_t ch) {
    esp_wifi_set_band(is5(ch) ? WIFI_BAND_5G : WIFI_BAND_2G);
    esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);
}

// Fold a node sighting (meshcore calls this instead of cap::ingest). Dedup by
// BSSID, keep the latest reading, mark it to be relayed.
void fold(const uint8_t* bssid, int8_t rssi, uint8_t chan, uint8_t band,
          const char* ssid, uint32_t ageMs, uint8_t src, uint16_t auth) {
    (void)src;
    const uint32_t heardAt = millis() - (ageMs > 60000 ? 60000 : ageMs);
    portENTER_CRITICAL(&mux);
    folded++;
    int slot = -1, free = -1;
    for (int i = 0; i < STORE; i++) {
        if (!store_[i].used) { if (free < 0) free = i; continue; }
        bool eq = true;
        for (int k = 0; k < 6; k++) if (store_[i].bssid[k] != bssid[k]) { eq = false; break; }
        if (eq) { slot = i; break; }
    }
    if (slot < 0) slot = free;
    if (slot < 0) {
        // Full. The CYD holds the real table and dedups, so an entry that has
        // already been relayed is cheap to lose: evict the stalest clean one.
        // Only when every slot is still waiting to be delivered is it dropped.
        uint32_t oldest = 0;
        for (int i = 0; i < STORE; i++) {
            if (store_[i].dirty) continue;
            const uint32_t age = millis() - store_[i].lastMs;
            if (slot < 0 || age > oldest) { slot = i; oldest = age; }
        }
        if (slot < 0) { storeDrop++; portEXIT_CRITICAL(&mux); return; }
    }

    Sight& s = store_[slot];
    s.used = true;
    for (int k = 0; k < 6; k++) s.bssid[k] = bssid[k];
    s.rssi = rssi;
    s.chan = chan;
    s.band = band == ui::BAND_5 ? 1 : 0;
    s.auth = auth;
    s.lastMs = heardAt;
    int n = 0;
    if (ssid) for (; n < 20 && ssid[n]; n++) s.ssid[n] = ssid[n];
    s.ssidLen = (uint8_t)n;
    for (int k = n; k < 20; k++) s.ssid[k] = 0;
    s.dirty = true;
    portEXIT_CRITICAL(&mux);
}

// Frames the coordinator tier owns, intercepted before meshcore's node
// handling. meshcore adopts the sender of ANY frame it does not recognise as a
// node, so every coordinator-tier kind -- and an Assign meant for some other
// cluster's node -- is claimed here even when it is not for us; otherwise a
// neighbouring coordinator's CoordAssign would register that coordinator as
// one of our nodes. Only Hello and Report (genuine node traffic) fall through.
bool onCoordFrame(const uint8_t* src, const uint8_t* data, int len) {
    const mesh::Hdr* h = (const mesh::Hdr*)data;
    if (h->ver != mesh::VERSION) return false;      // meshcore reports the skew
    if (h->kind == mesh::MSG_HELLO || h->kind == mesh::MSG_REPORT) return false;
    if (h->kind != mesh::MSG_CASSIGN) return true;  // CREG/CBATCH/ASSIGN: not ours to adopt

    if (!mesh::valid(data, len, mesh::MSG_CASSIGN, (int)sizeof(mesh::CoordAssign)))
        return true;
    const mesh::CoordAssign* a = (const mesh::CoordAssign*)data;
    for (int i = 0; i < 6; i++) if (a->h.mac[i] != selfMac[i]) return true;  // another coordinator's

    if (!haveCyd && src) {
        memcpy(cydMac, src, 6);
        esp_now_peer_info_t p = {};
        memcpy(p.peer_addr, cydMac, 6);
        p.channel = 0;
        p.ifidx   = WIFI_IF_STA;
        p.encrypt = false;
        if (esp_now_add_peer(&p) == ESP_OK) haveCyd = true;
    }

    // The CYD repeats our CoordAssign in answer to every batch (a broadcast
    // sent while we were parked elsewhere would otherwise be lost for good),
    // so only a real change re-feeds meshcore.
    const uint8_t n = a->nChan > mesh::MAX_SLICE ? mesh::MAX_SLICE : a->nChan;
    bool changed = !assigned || clusterId != a->clusterId ||
                   reportChan != a->reportChan || sliceN != n;
    for (int i = 0; !changed && i < n; i++) if (slice[i] != a->chans[i]) changed = true;

    const bool first = !assigned;
    clusterId   = a->clusterId;
    reportChan  = a->reportChan;
    collectChan = a->collectChan;
    sliceN = n;
    for (int i = 0; i < sliceN; i++) slice[i] = a->chans[i];
    assigned = true;
    if (changed) sliceApplied = false;       // re-feed meshcore on the next loop
    if (first)
        Serial.printf("coord: cluster %u, report ch%u, collect ch%u, %u slice ch\n",
                      clusterId, reportChan, collectChan, sliceN);
    return true;
}

void onSent(const wifi_tx_info_t*, esp_now_send_status_t st) {
    if (st == ESP_NOW_SEND_SUCCESS) txOk++; else txFail++;
}

// Deliver the deduped store: hop to the collection channel, send ClusterBatch
// frames until nothing dirty is left, then return to the report channel. Same
// batch/flush discipline as node_main.cpp::report().
void report() {
    mesh::Rec batch[mesh::MAX_BATCH];
    uint32_t  batchAt[mesh::MAX_BATCH];
    const uint8_t loss = meshcore::lossPct();
    static uint8_t seq = 0;

    park(collectChan);
    delay(5);                                  // let the radio settle

    bool first = true;
    for (;;) {
        int n = 0;
        const uint32_t sendAt = millis();
        portENTER_CRITICAL(&mux);
        for (int i = 0; i < STORE && n < mesh::MAX_BATCH; i++) {
            if (!store_[i].used || !store_[i].dirty) continue;
            Sight& s = store_[i];
            mesh::Rec& r = batch[n];
            for (int k = 0; k < 6; k++) r.bssid[k] = s.bssid[k];
            r.rssi = s.rssi;
            r.chan = s.chan;
            r.band = s.band;
            r.ssidLen = s.ssidLen;
            for (int k = 0; k < 20; k++) r.ssid[k] = s.ssid[k];
            r.auth = s.auth;
            batchAt[n] = s.lastMs;
            s.dirty = false;
            n++;
        }
        portEXIT_CRITICAL(&mux);
        // Always send one batch per delivery, even an empty one: it is the
        // coordinator's heartbeat. A quiet cluster would otherwise be timed
        // out by the CYD and silently dropped from the mesh.
        if (!n && !first) break;
        first = false;

        mesh::ClusterBatch b = {};
        b.h.magic = mesh::MAGIC;
        b.h.ver   = mesh::VERSION;
        b.h.kind  = mesh::MSG_CBATCH;
        memcpy(b.h.mac, selfMac, 6);
        b.clusterId = clusterId;
        b.seq     = seq++;
        b.lossPct = loss;
        b.count   = (uint8_t)n;
        for (int i = 0; i < n; i++) {
            b.recs[i] = batch[i];
            const uint32_t age = sendAt - batchAt[i];
            b.recs[i].ageMs = (uint16_t)(age > 60000 ? 60000 : age);
        }

        const uint8_t* to = haveCyd ? cydMac : bcast;
        if (esp_now_send(to, (const uint8_t*)&b, sizeof b) == ESP_OK) {
            relayed += n;
            txQueued++;
        }
        // Wait for the send to complete rather than overrunning ESP-NOW's
        // small transmit queue -- the same reason node_main does.
        const uint32_t waitFrom = txOk + txFail;
        const uint32_t until = millis() + 30;
        while ((txOk + txFail) == waitFrom && millis() < until) delay(1);
    }

    // Linger on the collection channel: the CYD answers a batch with our
    // CoordAssign, and this is the only moment we are on a channel it can reach.
    delay(30);
    park(reportChan);
}

} // namespace

void setup() {
    Serial.begin(115200);
    delay(300);
    Serial.println("\nAETHERSCAN coordinator");

    WiFi.mode(WIFI_MODE_STA);
    WiFi.disconnect();
    delay(100);
    esp_wifi_set_band_mode(WIFI_BAND_MODE_AUTO);
    esp_read_mac(selfMac, ESP_MAC_WIFI_STA);
    Serial.printf("mac   : %02X:%02X:%02X:%02X:%02X:%02X\n",
                  selfMac[0], selfMac[1], selfMac[2], selfMac[3], selfMac[4], selfMac[5]);

    for (int i = 0; i < STORE; i++) store_[i].used = false;

    // meshcore owns ESP-NOW init and the receive callback. We take over the
    // frames it does not handle (the CoordAssign) through the collector hook,
    // and redirect its sighting fold into our own store through the sink -- so
    // the node handling is reused verbatim, not forked.
    meshcore::begin();
    meshcore::setSink(fold);
    meshcore::setCollector(onCoordFrame);
    esp_now_register_send_cb(onSent);

    // Park on the collection channel to announce; the CYD collects there.
    park(collectChan);
    Serial.println("coord: up, registering with the CYD");
}

void loop() {
    const uint32_t now = millis();

    if (!assigned) {
        if (now - lastCreg >= CREG_MS) {
            lastCreg = now;
            mesh::CoordReg c = {};
            c.h.magic = mesh::MAGIC;
            c.h.ver   = mesh::VERSION;
            c.h.kind  = mesh::MSG_CREG;
            memcpy(c.h.mac, selfMac, 6);
            c.wants5 = 1;
            esp_now_send(bcast, (const uint8_t*)&c, sizeof c);
        }
        delay(5);
        return;
    }

    // Feed the slice to meshcore once per assignment, then hold the report
    // channel so our nodes can reach us (meshcore sends their assignments from
    // whatever channel we are parked on).
    if (!sliceApplied) {
        sliceApplied = true;
        meshcore::setSlice(slice, sliceN, reportChan);
        park(reportChan);
    }

    meshcore::tick(now);

    if (now - lastReport >= REPORT_MS) {
        lastReport = now;
        report();
        Serial.printf("coord: cl%u ch%u nodes=%d loss=%u%% | folded=%-5u relayed=%-5u "
                      "drop=%-4u frames q=%-4u ok=%-4u fail=%-4u %s\n",
                      clusterId, reportChan, meshcore::count(),
                      (unsigned)meshcore::lossPct(),
                      (unsigned)folded, (unsigned)relayed, (unsigned)storeDrop,
                      (unsigned)txQueued, (unsigned)txOk, (unsigned)txFail,
                      haveCyd ? "unicast" : "bcast");
    }
    delay(2);
}
