#include "meshcore.h"
#include <Arduino.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <esp_now.h>
#include <esp_mac.h>
#include "mesh.h"
#include "model.h"
#include "capture.h"
#include "chanplan.h"
#include "phase.h"

namespace meshcore {
namespace {

// A node that has said nothing for this long is gone -- out of range, flat
// battery, or unplugged. Its channels go back in the pool.
constexpr uint32_t NODE_TIMEOUT_MS = 20000;

NodeRec nodes_[MAX_NODES];
volatile uint32_t nHello = 0, nReport = 0;
// Where received frames go. The radio ACKs far more than the application
// handles, so the loss is on this side of the aerial and these say which
// branch is eating it.
volatile uint32_t rxTotal = 0, rxMagic = 0, rxVer = 0, rxLen = 0, rxNoSlot = 0;
uint8_t selfMac[6];
uint8_t bcast[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;

// Slice planning (a coordinator's band share). sliceN_ == 0 => whole band on
// mesh::REPORT_CHAN, the CYD-as-Core default.
uint8_t sliceChans_[mesh::MAX_SLICE];
int     sliceN_ = 0;
uint8_t sliceReportChan_ = mesh::REPORT_CHAN;

// Fold sink and collector hook. Both null by default, which is the CYD/node
// behaviour; a coordinator installs both.
Sink      sink_ = nullptr;
FrameHook collector_ = nullptr;

int findOrAdd(const uint8_t* mac) {
    for (int i = 0; i < MAX_NODES; i++)
        if (nodes_[i].live && !memcmp(nodes_[i].mac, mac, 6)) return i;
    for (int i = 0; i < MAX_NODES; i++)
        if (!nodes_[i].live) {
            nodes_[i] = NodeRec{};
            memcpy(nodes_[i].mac, mac, 6);
            nodes_[i].live = true;
            return i;
        }
    return -1;
}

void onRecv(const esp_now_recv_info_t* info, const uint8_t* data, int len) {
    rxTotal++;
    if (!data || len < (int)sizeof(mesh::Hdr)) { rxLen++; return; }
    const mesh::Report* pending = nullptr;
    uint8_t pendingSrc = 0;
    const uint8_t* newPeer = nullptr;
    const mesh::Hdr* h = (const mesh::Hdr*)data;
    if (h->magic != mesh::MAGIC) { rxMagic++; return; }

    // Offer the frame to the coordinator tier first. On a node build or a CYD
    // with no coordinators this is null / returns false, so everything below
    // -- the node path -- runs exactly as it always has.
    if (collector_ && info && collector_(info->src_addr, data, len)) return;

    // Ours, but not this build's. Say so rather than dropping it quietly --
    // a silent reject is indistinguishable from a node that is out of range.
    if (h->ver != mesh::VERSION) {
        static uint32_t lastWarn = 0;
        if (millis() - lastWarn > 5000) {
            lastWarn = millis();
            Serial.printf("mesh: node %02X:%02X:%02X speaks protocol v%u, we speak v%u"
                          " -- reflash it\n",
                          h->mac[3], h->mac[4], h->mac[5], h->ver, mesh::VERSION);
        }
        rxVer++;
        return;
    }

    // Right protocol, wrong size: the structs drifted without the version
    // moving. Worth shouting about, because it is a bug rather than a
    // deployment mistake.
    if (h->kind == mesh::MSG_REPORT && len != (int)sizeof(mesh::Report)) {
        static uint32_t lastLen = 0;
        if (millis() - lastLen > 5000) {
            lastLen = millis();
            Serial.printf("mesh: report is %d bytes, expected %u -- struct drift\n",
                          len, (unsigned)sizeof(mesh::Report));
        }
        rxLen++;
        return;
    }

    portENTER_CRITICAL(&mux);
    const int i = findOrAdd(h->mac);
    if (i < 0) rxNoSlot++;
    if (i >= 0) {
        if (!nodes_[i].peered) {
            nodes_[i].peered = true;
            newPeer = nodes_[i].mac;
        }
        nodes_[i].lastMs = millis();
        if (h->kind == mesh::MSG_HELLO)  { nodes_[i].hellos++;  nHello++; }
        if (h->kind == mesh::MSG_REPORT && len >= (int)sizeof(mesh::Report)) {
            const mesh::Report* r = (const mesh::Report*)data;
            nodes_[i].reports++;
            nodes_[i].recs += r->count;
            nReport++;

            // Sequence gap = reports that never arrived. The counter is a
            // byte, so a gap of more than a quarter of its range is read as
            // a node restart rather than 200 lost reports -- which is the
            // difference between a useful number and a frightening one.
            if (nodes_[i].seqValid) {
                const uint8_t gap = (uint8_t)(r->seq - nodes_[i].lastSeq - 1);
                if (gap > 64) nodes_[i].lost = 0;      // restarted
                else          nodes_[i].lost += gap;
            }
            nodes_[i].lastSeq = r->seq;
            nodes_[i].seqValid = true;
            pending = r;          // folded in below, outside this lock
            pendingSrc = (uint8_t)(i + 1);
        }
    }
    portEXIT_CRITICAL(&mux);

    if (newPeer) {
        esp_now_peer_info_t p = {};
        memcpy(p.peer_addr, newPeer, 6);
        p.channel = 0;
        p.ifidx   = WIFI_IF_STA;
        p.encrypt = false;
        esp_now_add_peer(&p);
    }

    // Folded in outside the lock: cap::ingest takes its own, and nesting the
    // two across modules is how a deadlock gets written.
    if (pending) {
        char ssid[21];
        for (int k = 0; k < pending->count && k < mesh::MAX_RECS; k++) {
            const mesh::Rec& rc = pending->recs[k];
            const int n = rc.ssidLen > 20 ? 20 : rc.ssidLen;
            for (int j = 0; j < n; j++) ssid[j] = rc.ssid[j];
            ssid[n] = 0;
            if (sink_)
                sink_(rc.bssid, rc.rssi, rc.chan,
                      rc.band ? ui::BAND_5 : ui::BAND_24, ssid, rc.ageMs,
                      pendingSrc, rc.auth);
            else
                cap::ingest(rc.bssid, rc.rssi, rc.chan,
                            rc.band ? ui::BAND_5 : ui::BAND_24, ssid, rc.ageMs,
                            pendingSrc, rc.auth);
        }
    }
}

} // namespace

void begin() {
    for (int i = 0; i < MAX_NODES; i++) nodes_[i] = NodeRec{};
    esp_read_mac(selfMac, ESP_MAC_WIFI_STA);

    if (esp_now_init() != ESP_OK) {
        Serial.println("mesh: esp_now_init FAILED");
        return;
    }
    esp_now_register_recv_cb(onRecv);

    esp_now_peer_info_t peer = {};
    memcpy(peer.peer_addr, bcast, 6);
    peer.channel = 0;              // 0 = whatever channel we are on
    peer.ifidx   = WIFI_IF_STA;
    peer.encrypt = false;
    esp_now_add_peer(&peer);

    Serial.printf("mesh: core up, listening for nodes (report channel %u)\n",
                  mesh::REPORT_CHAN);
}

void setSlice(const uint8_t* chans, int n, uint8_t reportChan) {
    if (n < 0) n = 0;
    if (n > mesh::MAX_SLICE) n = mesh::MAX_SLICE;
    for (int i = 0; i < n; i++) sliceChans_[i] = chans[i];
    sliceN_ = n;
    sliceReportChan_ = n > 0 ? reportChan : mesh::REPORT_CHAN;
}

void setSink(Sink sink) { sink_ = sink; }

void setCollector(FrameHook hook) { collector_ = hook; }

void assignAll(uint32_t nowMs) {
    int live = 0;
    for (int i = 0; i < MAX_NODES; i++) if (nodes_[i].live) live++;
    if (!live) return;

    chanplan::Plan plans[MAX_NODES];
    uint8_t reportChan;
    if (sliceN_ > 0) {
        // A coordinator: plan over its slice and report on its own channel.
        chanplan::planCluster(sliceChans_, sliceN_, live, plans);
        reportChan = sliceReportChan_;
    } else {
        // The CYD-as-Core: the whole band on the fixed report channel.
        chanplan::planChannels(live, plans);
        reportChan = mesh::REPORT_CHAN;
    }

    // Only announce when the shape actually changed, rather than every pass.
    static int lastLive = -1;
    const bool shapeChanged = live != lastLive;
    lastLive = live;

    int slot = 0;
    for (int i = 0; i < MAX_NODES; i++) {
        if (!nodes_[i].live) continue;
        const chanplan::Plan& p = plans[slot++];

        mesh::Assign a = {};
        a.h.magic = mesh::MAGIC;
        a.h.ver   = mesh::VERSION;
        a.h.kind  = mesh::MSG_ASSIGN;
        memcpy(a.h.mac, nodes_[i].mac, 6);
        memcpy(a.coreMac, selfMac, 6);
        a.reportChan = reportChan;
        a.nChan = p.n;
        for (int k = 0; k < p.n; k++) a.chans[k] = p.ch[k];
        // planCluster hands out n==0 when the pool has fewer channels than
        // there are nodes. That is an IDLE node, not one sweeping nothing: the
        // node reads nChan==0 as unassigned and stops queueing until it is given
        // work, so make sure it is never marked camping on a channel it lacks.
        a.camp = p.n > 0 && p.camp;
        a.phaseMs = (uint16_t)mesh_phase::offset(slot - 1, live, mesh::REPORT_MS);

        esp_now_send(bcast, (const uint8_t*)&a, sizeof a);
        nodes_[i].nChan = p.n;
        nodes_[i].camp  = a.camp;
        for (int k = 0; k < p.n && k < 10; k++) nodes_[i].chans[k] = p.ch[k];
        nodes_[i].assigned = true;

        if (shapeChanged)
            Serial.printf("mesh: node %02X:%02X:%02X -> %s %u ch (first %u)\n",
                          nodes_[i].mac[3], nodes_[i].mac[4], nodes_[i].mac[5],
                          p.n == 0 ? "IDLE" : p.camp ? "CAMP" : "sweep",
                          p.n, p.n ? p.ch[0] : 0);
    }
    if (shapeChanged)
        Serial.printf("mesh: %d node(s), reporting on ch%u\n", live, reportChan);
    (void)nowMs;
}

void tick(uint32_t nowMs) {
    portENTER_CRITICAL(&mux);
    for (int i = 0; i < MAX_NODES; i++)
        if (nodes_[i].live && (nowMs - nodes_[i].lastMs) > NODE_TIMEOUT_MS)
            nodes_[i].live = false;
    portEXIT_CRITICAL(&mux);

    // Re-send assignments periodically rather than once: a node that was
    // out of range, or rebooted, needs telling again, and the topology
    // changes whenever one joins or drops.
    static uint32_t lastAssign = 0;
    if (nowMs - lastAssign >= 3000) { lastAssign = nowMs; assignAll(nowMs); }
}

int count() {
    int n = 0;
    for (int i = 0; i < MAX_NODES; i++) if (nodes_[i].live) n++;
    return n;
}

const NodeRec* node(int i) {
    if (i < 0 || i >= MAX_NODES) return nullptr;
    return &nodes_[i];
}

void fill(ui::State& s, uint32_t nowMs) {
    int n = 0;
    for (int i = 0; i < MAX_NODES && n < ui::MAX_NODE_ROWS; i++) {
        if (!nodes_[i].live) continue;
        ui::NodeRow& r = s.nodes[n++];
        r.live  = true;
        for (int k = 0; k < 3; k++) r.id[k] = nodes_[i].mac[3 + k];
        r.nChan = nodes_[i].nChan;
        r.chan0 = nodes_[i].chans[0];
        r.camp  = nodes_[i].camp;
        r.recs  = nodes_[i].recs;
        r.found = cap::foundBy((uint8_t)(i + 1));
        const uint32_t tot = nodes_[i].reports + nodes_[i].lost;
        r.lossPct = (uint8_t)(tot ? (nodes_[i].lost * 100 + tot / 2) / tot : 0);
        r.ageS  = (nowMs - nodes_[i].lastMs) / 1000;
    }
    s.nNodes = (uint8_t)n;
}

// Mesh-wide loss, across every live node.
void rxStats(uint32_t& tot, uint32_t& magic, uint32_t& ver,
             uint32_t& len, uint32_t& noslot) {
    tot = rxTotal; magic = rxMagic; ver = rxVer; len = rxLen; noslot = rxNoSlot;
}

uint8_t lossPct() {
    uint32_t got = 0, lost = 0;
    for (int i = 0; i < MAX_NODES; i++) {
        if (!nodes_[i].live) continue;
        got  += nodes_[i].reports;
        lost += nodes_[i].lost;
    }
    const uint32_t tot = got + lost;
    return (uint8_t)(tot ? (lost * 100 + tot / 2) / tot : 0);
}

uint32_t hellosRx()  { return nHello; }
uint32_t reportsRx() { return nReport; }

} // namespace meshcore
