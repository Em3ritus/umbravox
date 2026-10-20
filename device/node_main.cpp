// Node firmware: a headless C5 that sniffs the channels the Core gives it
// and reports what it hears.
//
// No display, no touch, no GPS -- the Core owns all of that. A node is a
// radio and a buffer. It spends almost all its time on its assigned
// channels and only visits the report channel to deliver a batch.
#include <Arduino.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <esp_now.h>
#include <esp_mac.h>
#include "mesh.h"
#include "wifi_auth.h"

namespace {

constexpr uint32_t HELLO_MS   = 1000;   // while unassigned
constexpr uint32_t DWELL_MS   = 220;    // per channel, matching the Core
constexpr uint32_t REPORT_MS  = 2500;   // how often to deliver
// 64 overflowed on a busy sweep -- 8 records lost in a 100s run, which is
// the only loss left in the chain now the link measures clean. At 32 bytes
// a record this is 4KB on a node using 15% of its RAM.
constexpr int      QUEUE      = 192;

uint8_t  selfMac[6];
uint8_t  bcast[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};

uint8_t  coreMac[6];
volatile bool haveCore = false;
uint8_t  chans[mesh::MAX_ASSIGN];
uint8_t  nChan = 0;
bool     camp = false;
volatile bool assigned = false;
// Where to deliver. mesh::REPORT_CHAN against a lone Core -- which is all a
// Core ever assigns -- and a coordinator's own channel when one has taken us.
uint8_t  reportChan = mesh::REPORT_CHAN;
int      hop = 0;
uint32_t hopAt = 0, lastHello = 0, lastReport = 0;

mesh::Rec queue_[QUEUE];
uint32_t  qAt[QUEUE];          // millis() when each was heard
int       qN = 0;
uint32_t  seen = 0, sent = 0, dropped = 0, failed = 0;
// Frames, as distinct from records. esp_now_send() returning ESP_OK means
// the frame was *queued*, not delivered -- so queued and confirmed are
// counted separately, and the gap between them is the thing to look at.
uint32_t  txQueued = 0, txOk = 0, txFail = 0;
portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;

bool is5(uint8_t ch) { return ch > 14; }

void park(uint8_t ch) {
    esp_wifi_set_band(is5(ch) ? WIFI_BAND_5G : WIFI_BAND_2G);
    esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);
}

void onPacket(void* buf, wifi_promiscuous_pkt_type_t type) {
    if (type != WIFI_PKT_MGMT) return;
    const wifi_promiscuous_pkt_t* p = (const wifi_promiscuous_pkt_t*)buf;
    if (p->rx_ctrl.sig_len < 24) return;
    const uint8_t* f = p->payload;
    const uint8_t sub = (f[0] >> 4) & 0x0F;
    if (sub != 8 && sub != 5) return;          // beacon / probe response

    // Nothing is queued before an assignment arrives: there is nowhere to
    // send it, and the queue would simply fill and then discard everything
    // behind it. The Core is covering this channel anyway.
    if (!assigned) return;
    const uint8_t cur = chans[hop];

    portENTER_CRITICAL(&mux);
    seen++;
    if (qN < QUEUE) {
        qAt[qN] = millis();
        mesh::Rec& r = queue_[qN++];
        for (int i = 0; i < 6; i++) r.bssid[i] = f[16 + i];
        r.rssi = (int8_t)p->rx_ctrl.rssi;
        r.chan = cur;
        r.band = is5(cur) ? 1 : 0;
        r.ssidLen = 0;
        r.ssid[0] = 0;
        r.auth = ui::parseAuth(f + 24, (int)p->rx_ctrl.sig_len - 24);
        if (p->rx_ctrl.sig_len >= 38 && f[36] == 0) {
            uint8_t n = f[37];
            if (n > 20) n = 20;
            if (36 + 2 + f[37] <= p->rx_ctrl.sig_len) {
                for (uint8_t k = 0; k < n; k++) {
                    const char c = (char)f[38 + k];
                    r.ssid[k] = (c >= 32 && c < 127) ? c : '.';
                }
                r.ssidLen = n;
            }
        }
    } else {
        dropped++;
    }
    portEXIT_CRITICAL(&mux);
}

// Consecutive failed UNICAST sends to our Core. A unicast frame is ACKed by
// the radio, so a streak of failures means the Core is gone (a dead
// coordinator, say) rather than that one frame was unlucky. A live Core ACKs
// every report, so against a lone CYD this never builds. Broadcast sends
// (Hello, and reports before a Core is known) always report success and are
// not counted either way. The recovery itself runs in loop(), not here: this
// callback is in the Wi-Fi task and must not retune the radio.
constexpr uint8_t LOSS_STREAK = 8;
volatile uint8_t failStreak = 0;

void onSent(const wifi_tx_info_t* info, esp_now_send_status_t st) {
    if (st == ESP_NOW_SEND_SUCCESS) txOk++; else txFail++;
    if (!haveCore || !info || !info->des_addr) return;
    if (memcmp(info->des_addr, coreMac, 6) != 0) return;     // not to the Core
    if (st == ESP_NOW_SEND_SUCCESS) failStreak = 0;
    else if (failStreak < 255) failStreak = failStreak + 1;
}

void onRecv(const esp_now_recv_info_t*, const uint8_t* data, int len) {
    if (!mesh::valid(data, len, mesh::MSG_ASSIGN, (int)sizeof(mesh::Assign))) return;
    const mesh::Assign* a = (const mesh::Assign*)data;
    for (int i = 0; i < 6; i++) if (a->h.mac[i] != selfMac[i]) return;

    // Learn the Core's address and peer with it, so reports can go unicast.
    if (!haveCore) {
        memcpy(coreMac, a->coreMac, 6);
        esp_now_peer_info_t p = {};
        memcpy(p.peer_addr, coreMac, 6);
        p.channel = 0;
        p.ifidx   = WIFI_IF_STA;
        p.encrypt = false;
        if (esp_now_add_peer(&p) == ESP_OK) {
            haveCore = true;
            Serial.printf("node: core is %02X:%02X:%02X:%02X:%02X:%02X, reports now unicast\n",
                          coreMac[0], coreMac[1], coreMac[2],
                          coreMac[3], coreMac[4], coreMac[5]);
        }
    }

    const bool first = !assigned;
    nChan = a->nChan > mesh::MAX_ASSIGN ? mesh::MAX_ASSIGN : a->nChan;
    for (int i = 0; i < nChan; i++) chans[i] = a->chans[i];
    camp = a->camp;
    if (a->reportChan) reportChan = a->reportChan;
    assigned = nChan > 0;
    if (first && assigned) {
        Serial.printf("node: assigned %u channels (%u..%u), camp=%u\n",
                      nChan, chans[0], chans[nChan - 1], (unsigned)camp);
        hop = 0;
        park(chans[0]);
        hopAt = millis();
    }
}

// Deliver the queue: hop to the report channel, send batches, go back.
void report() {
    mesh::Rec batch[mesh::MAX_RECS];
    uint32_t  batchAt[mesh::MAX_RECS];
    int n;
    park(reportChan);
    delay(5);                                  // let the radio settle

    for (;;) {
        portENTER_CRITICAL(&mux);
        n = qN < mesh::MAX_RECS ? qN : mesh::MAX_RECS;
        for (int i = 0; i < n; i++) { batch[i] = queue_[i]; batchAt[i] = qAt[i]; }
        for (int i = n; i < qN; i++) { queue_[i - n] = queue_[i]; qAt[i - n] = qAt[i]; }
        qN -= n;
        portEXIT_CRITICAL(&mux);
        if (!n) break;

        static uint8_t seq = 0;
        mesh::Report r = {};
        r.h.magic = mesh::MAGIC;
        r.h.ver   = mesh::VERSION;
        r.h.kind  = mesh::MSG_REPORT;
        memcpy(r.h.mac, selfMac, 6);
        r.seq = seq++;
        r.count = (uint8_t)n;
        const uint32_t sendAt = millis();
        for (int i = 0; i < n; i++) {
            r.recs[i] = batch[i];
            const uint32_t age = sendAt - batchAt[i];
            r.recs[i].ageMs = (uint16_t)(age > 60000 ? 60000 : age);
        }
        // Unicast when we know where the Core is: that path is acknowledged
        // and retried by the radio. Broadcast is a fallback only.
        const uint8_t* to = haveCore ? coreMac : bcast;
        if (esp_now_send(to, (const uint8_t*)&r, sizeof r) == ESP_OK) {
            sent += n;
            txQueued++;
        } else {
            failed += n;
        }
        // Wait for the send to complete rather than stuffing the queue.
        // ESP-NOW has a small transmit queue; filling it faster than the
        // radio drains it is how frames vanish with ESP_OK returned.
        const uint32_t waitFrom = txOk + txFail;
        const uint32_t until = millis() + 30;
        while ((txOk + txFail) == waitFrom && millis() < until) delay(1);
    }

    if (assigned) park(chans[hop]);
}

} // namespace

void setup() {
    Serial.begin(115200);
    delay(300);
    Serial.println("\nAETHERSCAN node");

    WiFi.mode(WIFI_MODE_STA);
    WiFi.disconnect();
    delay(100);
    esp_wifi_set_band_mode(WIFI_BAND_MODE_AUTO);
    esp_read_mac(selfMac, ESP_MAC_WIFI_STA);
    Serial.printf("mac   : %02X:%02X:%02X:%02X:%02X:%02X\n",
                  selfMac[0], selfMac[1], selfMac[2], selfMac[3], selfMac[4], selfMac[5]);

    park(mesh::REPORT_CHAN);

    if (esp_now_init() != ESP_OK) {
        Serial.println("node: esp_now_init FAILED");
        for (;;) delay(1000);
    }
    esp_now_register_recv_cb(onRecv);
    esp_now_register_send_cb(onSent);
    esp_now_peer_info_t peer = {};
    memcpy(peer.peer_addr, bcast, 6);
    peer.channel = 0;
    peer.ifidx   = WIFI_IF_STA;
    peer.encrypt = false;
    esp_now_add_peer(&peer);

    wifi_promiscuous_filter_t filt = {};
    filt.filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT;
    esp_wifi_set_promiscuous_filter(&filt);
    esp_wifi_set_promiscuous_rx_cb(&onPacket);
    esp_wifi_set_promiscuous(true);

    Serial.println("node: up, waiting for an assignment");
}

void loop() {
    const uint32_t now = millis();

    if (!assigned) {
        if (now - lastHello >= HELLO_MS) {
            lastHello = now;
            mesh::Hello h = {};
            h.h.magic = mesh::MAGIC;
            h.h.ver   = mesh::VERSION;
            h.h.kind  = mesh::MSG_HELLO;
            memcpy(h.h.mac, selfMac, 6);
            h.fw = 1; h.wants5 = 1;
            esp_now_send(bcast, (const uint8_t*)&h, sizeof h);
        }
        delay(5);
        return;
    }

    if (!camp && nChan > 1 && now - hopAt >= DWELL_MS) {
        hop = (hop + 1) % nChan;
        park(chans[hop]);
        hopAt = now;
    }

    if (now - lastReport >= REPORT_MS) {
        lastReport = now;
        report();
        if (failStreak >= LOSS_STREAK) {
            // The Core has stopped answering. Forget it entirely -- the
            // learn-once peer guard included, or a re-Hello'd node would keep
            // unicasting the dead address -- and go back to asking for one,
            // on the channel every Core and CYD listens on.
            Serial.printf("node: core %02X:%02X:%02X silent (%u failed sends) -- "
                          "re-registering\n", coreMac[3], coreMac[4], coreMac[5],
                          (unsigned)failStreak);
            esp_now_del_peer(coreMac);
            haveCore = false;
            assigned = false;
            failStreak = 0;
            reportChan = mesh::REPORT_CHAN;
            park(mesh::REPORT_CHAN);
            lastHello = 0;
            return;
        }
        Serial.printf("node: ch%-3u q=%-3d recs seen=%-5u sent=%-5u drop=%-4u | "
                      "frames queued=%-4u ok=%-4u fail=%-4u %s\n",
                      chans[hop], qN, (unsigned)seen, (unsigned)sent,
                      (unsigned)dropped,
                      (unsigned)txQueued, (unsigned)txOk, (unsigned)txFail,
                      haveCore ? "unicast" : "bcast");
    }
    delay(2);
}
