#include "blescan.h"
#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#include "signatures.h"
#include "ble_ad.h"
#include <esp_heap_caps.h>
#include <Print.h>
#include "csv.h"
#include "applog.h"
#include "wiglerow.h"
#include "dedup.h"
#include "blerecycle.h"
#include "elog.h"
#include <string.h>
#include "gpstrack.h"
#include "gnss.h"
#include <math.h>

namespace blescan {
namespace {

constexpr uint16_t BLE_INTERVAL_MS = 100;
uint8_t duty = 30;                                // % of the radio BLE takes

// Also PSRAM-backed. BLE sees far more distinct addresses than WiFi does,
// because most of them are randomised and rotate every few minutes.
// Past its insert limit a new address is simply not tracked (counted in
// tableDrops()), after a bounded probe (dedup.h) -- and the log rolls over at
// applog::ROLLOVER_BLE, well before that.
constexpr int TABLE  = 4096;                      // distinct addresses tracked
static_assert(applog::ROLLOVER_BLE * 100u <= (uint32_t)TABLE * 65u,
              "BLE rollover must leave the address table <= 65% full");
static_assert(TABLE <= 65536, "order_ and applog::Pending hold 16-bit slot indices");
static_assert(applog::ROLL_HARD_BLE > applog::ROLLOVER_BLE && applog::ROLL_HARD_BLE * 100u <= (uint32_t)TABLE * 85u,
              "the forced-roll ceiling sits above the rollover, below a full table");
constexpr int RECENT = 16;                        // blips BLE may put on the dial

// Per-device detail, mirroring what capture keeps for access points. BLE
// addresses rotate, so none of this is long-lived -- but within a drive it
// is the difference between "38:4B:C2" and "Tile".
struct Extra {
    char     name[ui::SSID_MAX];
    int32_t  lat7, lon7;              // at the strongest sighting
    int32_t  firstLat7, firstLon7;    // where it was first heard
    uint32_t firstEpoch;
    uint32_t firstMs, lastMs;
    int8_t   bestRssi;
    uint8_t  threat, conf;
    bool     located, hasFirst, following;
};

// --- following -----------------------------------------------------------
//
// "A tracker is nearby" is not a finding: in a car park half the phones in
// range are near a tracker. "A tracker has been within earshot for twenty
// minutes and two kilometres" is a different statement entirely, and it is
// the one worth interrupting someone for.
//
// This works better than it might sound. An AirTag rotates its address every
// 15 minutes while it is with its owner -- but a tag that has been separated
// from its owner holds one for 24 hours, specifically so that it can be
// detected. The dangerous case is the detectable one.
constexpr uint32_t FOLLOW_MIN_MS = 8UL * 60UL * 1000UL;   // minutes together
constexpr int32_t  FOLLOW_MIN_M  = 500;                   // and distance moved
constexpr uint32_t FOLLOW_SCAN_MS = 5000;
uint16_t nFollowing = 0;

// Equirectangular approximation. Exact enough below a few kilometres, and
// it avoids a trig call per device per sweep.
int32_t metresBetween(int32_t aLat, int32_t aLon, int32_t bLat, int32_t bLon) {
    const float latDeg = (float)aLat / 1e7f;
    const float dN = ((float)(bLat - aLat) / 1e7f) * 111320.0f;
    const float dE = ((float)(bLon - aLon) / 1e7f) * 111320.0f *
                     cosf(latDeg * 3.14159265f / 180.0f);
    return (int32_t)sqrtf(dN * dN + dE * dE);
}

uint8_t  (*addrs)[6] = nullptr;
int8_t*  rssis = nullptr;
Extra*   extra = nullptr;
bool*    used_ = nullptr;
uint32_t nUsed = 0;
// The table-full backstop (dedup.h), as in capture.
constexpr uint32_t INSERT_LIMIT = dedup::insertLimit(TABLE);
static_assert(applog::ROLL_HARD_BLE < INSERT_LIMIT,
              "the forced roll must come before the table refuses inserts");
uint32_t maxProbe_ = 0;
volatile uint32_t nTableDrops = 0;
volatile uint32_t nLongProbes = 0;
static_assert(applog::ROLLOVER_BLE <= dedup::crowdLimit(TABLE),
              "below the rollover no insert may be dropped for a long probe");

// Insertion order, so the codex can list newest first without sorting.
uint16_t* order_ = nullptr;
uint32_t  nOrder = 0;
uint8_t*  logBits_ = nullptr;         // the logger's done-bits (applog.h)

struct Blip { uint16_t angle; uint8_t dist; uint32_t lastMs; bool live; };
Blip recent_[RECENT];

ui::AlertRec alert_;
volatile uint32_t nAdverts = 0, nUnique = 0;
portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;

// Keyed per boot, like capture's (dedup.h). Drawn in begin().
uint32_t hashSeed = 0;
uint32_t hashAddr(const uint8_t* a) { return dedup::hashMac(a, hashSeed); }

// BLE advertisers are weaker than APs, so the dial scale differs: -40dBm is
// on top of us, -100 rides the outer ring. Using the WiFi mapping here would
// push every tag to the edge.
uint8_t rssiToDist(int rssi) {
    int d = ((-rssi) - 40) * 100 / 60;
    if (d < 0) d = 0;
    if (d > 100) d = 100;
    return (uint8_t)d;
}

class Cb : public BLEAdvertisedDeviceCallbacks {
    void onResult(BLEAdvertisedDevice dev) override {
        const uint8_t* a = dev.getAddress().getNative();
        const int rssi = dev.getRSSI();

        // Walk the raw advertisement rather than the parsed accessors:
        // getManufacturerData() returns a String and getServiceUUID() builds a
        // BLEUUID; with shouldParse on that allocated ~17 bytes per advert that
        // never came back, and the device ran out of heap and rebooted about
        // every 45 seconds. The walk is pure and bounds every attacker-
        // controlled length (see ble_ad.h), fuzzed in sim/test_ble_ad.cpp.
        const ble_ad::Fields ad = ble_ad::scan(dev.getPayload(),
                                               dev.getPayloadLength());
        const uint16_t company = ad.company, svc = ad.svc;
        const uint8_t* nameP = ad.name;
        const uint8_t  nameLen = ad.nameLen;

        // A null-terminated, printable copy of the advertised name for the
        // matcher. Some surveillance accessories -- Flock's Penguin battery and
        // camera modules -- are identifiable only by name: they ride locally
        // administered MACs that match no OUI and carry no vendor UUID.
        char nm[ui::SSID_MAX];
        {
            int k = 0;
            for (; nameP && k < (int)ui::SSID_MAX - 1 && k < nameLen; k++) {
                const char ch = (char)nameP[k];
                nm[k] = (ch >= 32 && ch < 127) ? ch : '.';
            }
            nm[k] = 0;
        }
        const ui::Hit sig = ui::matchBle(svc, company, nm);

        portENTER_CRITICAL(&mux);
        nAdverts++;
        if (sig.threat != ui::TH_NONE &&
            (sig.conf >= alert_.conf || (millis() - alert_.atMs) > 6000)) {
            alert_.threat = sig.threat;
            alert_.conf   = sig.conf;
            alert_.why    = sig.why;
            // Named by the tail of the address, as the codex rows are.
            static const char* HD = "0123456789ABCDEF";
            int k = 0;
            for (int b = 3; b < 6; b++) {
                alert_.name[k++] = HD[(a[b] >> 4) & 0xF];
                alert_.name[k++] = HD[a[b] & 0xF];
                if (b < 5) alert_.name[k++] = ':';
            }
            alert_.name[k] = 0;
            alert_.atMs = millis();
        }
        const uint32_t h = hashAddr(a);
        // Bounded, and FULL (not tracked, counted) past the insert limit:
        // the same backstop as capture's table (dedup.h).
        const dedup::Hit hit = dedup::find(h, TABLE, nUsed, INSERT_LIMIT, maxProbe_,
            [](uint32_t i) { return used_[i]; },
            [a](uint32_t i) { return memcmp(addrs[i], a, 6) == 0; });
        int slot = hit.slot;
        if (hit.kind == dedup::FULL) nTableDrops++;
        if (hit.kind == dedup::FOUND) rssis[slot] = (int8_t)rssi;
        if (hit.kind == dedup::INSERT) {
            const int i = slot;
            used_[i] = true;
            for (int k = 0; k < 6; k++) addrs[i][k] = a[k];
            rssis[i] = (int8_t)rssi;
            extra[i] = Extra{};
            extra[i].bestRssi = -127;
            extra[i].firstEpoch = ui::Gnss::utcEpoch(millis());
            extra[i].firstMs = millis();
            {
                int32_t la, lo;
                if (gpstrack::at(millis(), 0, la, lo)) {
                    extra[i].firstLat7 = la;
                    extra[i].firstLon7 = lo;
                    extra[i].hasFirst = true;
                }
            }
            order_[nOrder++] = (uint16_t)i;
            nUsed++;
            nUnique++;
            dedup::noteInsert(maxProbe_, hit);
            if (dedup::longProbe(hit)) nLongProbes++;
        }

        // Name and position, recorded at the strongest sighting.
        if (slot >= 0) {
            extra[slot].lastMs = millis();
            if (sig.threat != ui::TH_NONE && sig.conf >= extra[slot].conf) {
                extra[slot].threat = sig.threat;
                extra[slot].conf   = sig.conf;
            }
            if (nameP && !extra[slot].name[0]) {
                int k = 0;
                for (; k < (int)ui::SSID_MAX - 1 && k < nameLen; k++) {
                    const char ch = (char)nameP[k];
                    extra[slot].name[k] = (ch >= 32 && ch < 127) ? ch : '.';
                }
                extra[slot].name[k] = 0;
            }
            if ((int8_t)rssi > extra[slot].bestRssi) {
                int32_t la, lo;
                if (gpstrack::at(millis(), 0, la, lo)) {
                    extra[slot].lat7 = la;
                    extra[slot].lon7 = lo;
                    extra[slot].located = true;
                    extra[slot].bestRssi = (int8_t)rssi;
                }
            }
        }

        Blip& b = recent_[h % RECENT];
        b.angle  = (uint16_t)(h % 360);
        b.dist   = rssiToDist(rssi);
        b.lastMs = millis();
        b.live   = true;
        portEXIT_CRITICAL(&mux);
    }
};

Cb cb;
BLEScan* scan = nullptr;

} // namespace

void begin() {
    // Allocate before touching any of it, in PSRAM. The internal-RAM fallback
    // needs ~300 KB, more than the heap has free once Wi-Fi and BLE are up, so
    // in practice a board without PSRAM halts with the FATAL serial message
    // (and capture's table would already have halted it). Not relied on.
    addrs  = (uint8_t(*)[6])heap_caps_malloc(6 * TABLE, MALLOC_CAP_SPIRAM);
    rssis  = (int8_t*)heap_caps_malloc(TABLE, MALLOC_CAP_SPIRAM);
    used_  = (bool*)heap_caps_malloc(TABLE, MALLOC_CAP_SPIRAM);
    extra  = (Extra*)heap_caps_malloc(sizeof(Extra) * TABLE, MALLOC_CAP_SPIRAM);
    order_ = (uint16_t*)heap_caps_malloc(sizeof(uint16_t) * TABLE, MALLOC_CAP_SPIRAM);
    logBits_ = (uint8_t*)heap_caps_calloc((TABLE + 7) / 8, 1, MALLOC_CAP_SPIRAM);
    const bool inPsram = addrs && rssis && used_ && order_ && extra && logBits_;
    if (!inPsram) {
        free(addrs); free(rssis); free(used_); free(order_); free(extra); free(logBits_);
        logBits_ = (uint8_t*)calloc((TABLE + 7) / 8, 1);
        addrs  = (uint8_t(*)[6])calloc(TABLE, 6);
        rssis  = (int8_t*)calloc(TABLE, 1);
        used_  = (bool*)calloc(TABLE, 1);
        order_ = (uint16_t*)calloc(TABLE, sizeof(uint16_t));
        extra  = (Extra*)calloc(TABLE, sizeof(Extra));
    }
    if (!addrs || !rssis || !used_ || !order_ || !extra || !logBits_) {
        Serial.println("blescan: FATAL -- no room for the address table");
        for (;;) delay(1000);
    }
    Serial.printf("blescan: %d address slots in %s\n",
                  TABLE, inPsram ? "PSRAM" : "internal RAM");

    for (int i = 0; i < TABLE;  i++) used_[i] = false;
    nOrder = 0;
    for (int i = 0; i < RECENT; i++) recent_[i].live = false;
    hashSeed = esp_random();      // Wi-Fi is already up: hardware RNG; before the scan

    BLEDevice::init("");
    scan = BLEDevice::getScan();
    // shouldParse stays false: the raw payload is all the signature matcher
    // needs, and parsing leaks.
    scan->setAdvertisedDeviceCallbacks(&cb, /*wantDuplicates=*/true, /*shouldParse=*/false);
    scan->setActiveScan(false);     // listen only

    // Duty cycle is a direct trade against WiFi, because there is one radio.
    //
    // Measured: window 99 of interval 100 (99% duty) took WiFi from 7 APs and
    // 30 frames down to 2 APs and 3 frames -- BLE was starving the sniffer.
    // 30 of 100 leaves the sniffer most of the air while still catching
    // advertisers, which repeat every few hundred ms anyway.
    scan->setInterval(BLE_INTERVAL_MS);
    scan->setWindow((uint16_t)(BLE_INTERVAL_MS * duty / 100));

    // duration 0 = run until stopped.
    scan->start(0, nullptr, false);
    Serial.println("capture: BLE passive scan started");
}

void fill(ui::State& s, uint32_t nowMs) {
    portENTER_CRITICAL(&mux);
    const uint32_t u = nUnique;
    Blip snap[RECENT];
    for (int i = 0; i < RECENT; i++) snap[i] = recent_[i];
    portEXIT_CRITICAL(&mux);

    s.ble = (uint16_t)(u > 65535 ? 65535 : u);

    int n = s.nContacts;
    for (int i = 0; i < RECENT && n < ui::MAX_CONTACTS; i++) {
        if (!snap[i].live) continue;
        const uint32_t age = nowMs - snap[i].lastMs;
        if (age > 60000) continue;
        ui::Contact& c = s.contacts[n++];
        c.angle = snap[i].angle;
        c.dist  = snap[i].dist;
        c.band  = ui::BAND_BLE;
        c.age   = (uint8_t)(age / 240 > 255 ? 255 : age / 240);
    }
    s.nContacts = (uint8_t)n;
}

namespace {
const char* HEXDIG = "0123456789ABCDEF";
}

void fillDetail(ui::State& s, int index, uint32_t nowMs) {
    s.detail = ui::Detail{};
    if (index < 0 || (uint32_t)index >= nOrder) return;
    const uint16_t slot = order_[nOrder - 1 - index];

    ui::Detail& d = s.detail;
    d.valid = true;
    for (int k = 0; k < 6; k++) d.mac[k] = addrs[slot][k];
    d.rssi = rssis[slot];
    d.band = ui::BAND_BLE;
    d.chan = 0;
    // BLE advertisers are not tracked individually over time the way APs
    // are: the address usually rotates, so a history would be a history of
    // several devices wearing the same name.
    d.histN = 0;
    d.firstAgoS = d.lastAgoS = 0;
    (void)nowMs;
}

uint16_t sightCount() { return (uint16_t)(nOrder > 65535 ? 65535 : nOrder); }

void fillSights(ui::State& s, int offset) {
    s.totalSights = sightCount();
    int n = 0, skipped = 0;
    for (int i = (int)nOrder - 1; i >= 0 && n < ui::MAX_SIGHTS; i--) {
        if (skipped < offset) { skipped++; continue; }
        const uint16_t slot = order_[i];
        ui::Sighting& g = s.sights[n++];

        // BLE addresses are frequently randomised, so the OUI is often
        // meaningless. The last three octets are what distinguishes one
        // advertiser from another on screen.
        const uint8_t* a = addrs[slot];
        if (extra[slot].name[0]) {
            int k = 0;
            for (; k < ui::SSID_MAX - 1 && extra[slot].name[k]; k++)
                g.ssid[k] = extra[slot].name[k];
            g.ssid[k] = 0;
        } else {
            int k = 0;
            for (int b = 3; b < 6; b++) {
                g.ssid[k++] = HEXDIG[(a[b] >> 4) & 0xF];
                g.ssid[k++] = HEXDIG[a[b] & 0xF];
                if (b < 5) g.ssid[k++] = ':';
            }
            g.ssid[k] = 0;
        }
        for (int m = 0; m < 6; m++) g.mac[m] = a[m];
        g.rssi = rssis[slot];
        g.band = ui::BAND_BLE;
        g.chan = 0;
    }
    s.nSights = (uint8_t)n;
}

namespace {
uint32_t lastCycleMs = 0;     // last stop/start, by the recycle or setDuty
uint32_t nRestarts = 0;       // scans found stopped and started again
elog::Gate restartGate;

// Bound the library's result map and keep the scan alive (blerecycle.h).
// stop() then start(0, nullptr, false) is the sequence setDuty() has always
// used; is_continue = false is what clears the map. start() runs only after a
// successful stop(), so the map is never cleared while discovery can still
// add to it. On this single-core part the NimBLE host task outranks the loop,
// so it is never part-way through the scan callback when this runs.
void recycle(uint32_t nowMs) {
    if (!scan) return;
    const uint32_t count = (uint32_t)scan->getResults()->getCount();
    const blerecycle::Action a =
        blerecycle::shouldRecycle(count, nowMs, lastCycleMs, scan->isScanning(), duty);
    if (a == blerecycle::NONE) return;
    lastCycleMs = nowMs;
    if (!scan->stop()) return;               // try again next time round
    const bool ok = scan->start(0, nullptr, false);
    if (a == blerecycle::RESTART) {
        nRestarts++;
        if (restartGate.allow(nowMs, 60000))
            elog::event(elog::WARN, "ble", "scan had stopped, restarted (%s, %lu so far, "
                        "heap %uKB)", ok ? "ok" : "start failed", (unsigned long)nRestarts,
                        (unsigned)(ESP.getFreeHeap() >> 10));
    }
}
} // namespace

// Swept on a timer rather than per advert: the distance maths is cheap but
// not free, and nothing here changes meaningfully inside five seconds.
void tick(uint32_t nowMs) {
    recycle(nowMs);

    static uint32_t lastScan = 0;
    if (!extra || nowMs - lastScan < FOLLOW_SCAN_MS) return;
    lastScan = nowMs;

    int32_t la, lo;
    if (!gpstrack::at(nowMs, 0, la, lo)) return;    // no fix: no distance

    uint16_t following = 0;
    for (uint32_t i = 0; i < nOrder; i++) {
        const uint16_t slot = order_[i];
        Extra& e = extra[slot];
        if (e.threat != ui::TH_TRACKER || !e.hasFirst) continue;
        if ((nowMs - e.firstMs) < FOLLOW_MIN_MS) continue;
        if ((nowMs - e.lastMs) > 120000) continue;   // gone; not following

        const int32_t moved = metresBetween(e.firstLat7, e.firstLon7, la, lo);
        if (moved < FOLLOW_MIN_M) continue;

        following++;
        if (e.following) continue;                   // already reported
        e.following = true;

        portENTER_CRITICAL(&mux);
        alert_.threat = ui::TH_FOLLOW;
        alert_.conf   = ui::CONF_HIGH;
        alert_.why    = "TRAVELLED WITH YOU";
        if (e.name[0]) {
            int k = 0;
            for (; k < ui::ALERT_NAME - 1 && e.name[k]; k++) alert_.name[k] = e.name[k];
            alert_.name[k] = 0;
        } else {
            // Formatted wide and then truncated on purpose: the address plus
            // an unbounded distance can exceed the 20-byte alert name, which
            // -Wformat-truncation is right to object to.
            char wide[40];
            if (moved >= 1000)
                snprintf(wide, sizeof wide, "%02X:%02X:%02X %d.%dKM",
                         addrs[slot][3], addrs[slot][4], addrs[slot][5],
                         (int)(moved / 1000), (int)((moved % 1000) / 100));
            else
                snprintf(wide, sizeof wide, "%02X:%02X:%02X %dM",
                         addrs[slot][3], addrs[slot][4], addrs[slot][5], (int)moved);
            int k = 0;
            for (; k < ui::ALERT_NAME - 1 && wide[k]; k++) alert_.name[k] = wide[k];
            alert_.name[k] = 0;
        }
        alert_.atMs = nowMs;
        portEXIT_CRITICAL(&mux);

        Serial.printf("blescan: tracker %02X:%02X:%02X has followed %ldm over %lus\n",
                      addrs[slot][3], addrs[slot][4], addrs[slot][5],
                      (long)moved, (unsigned long)((nowMs - e.firstMs) / 1000));
    }
    nFollowing = following;
}

uint16_t followCount() { return nFollowing; }

void setDuty(uint8_t pct) {
    if (pct > 100) pct = 100;
    if (pct == duty || !scan) return;
    duty = pct;
    scan->stop();
    lastCycleMs = millis();       // a fresh scan: the recycle clock restarts
    if (duty == 0) return;        // 0 means hand the whole radio to WiFi
    scan->setWindow((uint16_t)(BLE_INTERVAL_MS * duty / 100));
    scan->start(0, nullptr, false);
}

void reset() {
    portENTER_CRITICAL(&mux);
    for (int i = 0; i < TABLE;  i++) used_[i] = false;
    for (int i = 0; i < RECENT; i++) recent_[i].live = false;
    nUsed = 0; nOrder = 0; nAdverts = 0; nUnique = 0;
    maxProbe_ = 0; nTableDrops = 0; nLongProbes = 0;
    alert_ = ui::AlertRec{};
    for (int i = 0; i < TABLE; i++) extra[i] = Extra{};
    nFollowing = 0;
    portEXIT_CRITICAL(&mux);
}

bool lastAlert(ui::AlertRec& out) {
    portENTER_CRITICAL(&mux);
    out = alert_;
    portEXIT_CRITICAL(&mux);
    return out.threat != ui::TH_NONE;
}

// The rollover clear: the address table and its counts. The dial's blips and
// the alert stay. Main loop only (logging::tick), like RESET COUNTERS; the
// lock keeps the scan callback out while it empties. Extra is re-initialised
// per slot on insert, so it need not be wiped here.
void rollover() {
    portENTER_CRITICAL(&mux);
    memset(used_, 0, TABLE);
    nUsed = 0; nOrder = 0; nUnique = 0;
    maxProbe_ = 0;
    nFollowing = 0;
    portEXIT_CRITICAL(&mux);
}

namespace {

// A row snapshot, copied out under the lock so a sighting cannot tear it.
struct Row { uint8_t addr[6]; int8_t rssi; Extra e; };

// Same rule as WiFi: no position yet -> retried; no UTC at first sight
// (firstEpoch is set once) -> never.
applog::Verdict verdictOf(const Extra& x, uint32_t nowMs, bool final) {
    return applog::holdVerdict(x.located, x.firstEpoch, x.firstMs, x.lastMs, nowMs, final);
}
// Exportable at all (the console dump's rule: no hold).
bool exportable(const Extra& x) { return x.located && x.firstEpoch; }

int formatRow(const Row& r, char* buf, int cap) {      // bytes pinned in wiglerow.h
    return wiglerow::ble(buf, cap, r.addr, r.e.name, r.e.firstEpoch, r.rssi,
                         r.e.lat7, r.e.lon7);
}

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
        if (i < nOrder) v = verdictOf(extra[order_[i]], nowMs, final);
        portEXIT_CRITICAL(&mux);
        return v;
    }
    int format(uint32_t i, char* buf, int cap) override {
        Row r;
        bool have = false;
        portENTER_CRITICAL(&mux);
        if (i < nOrder) {
            const uint16_t slot = order_[i];
            memcpy(r.addr, addrs[slot], 6);
            r.rssi = rssis[slot];
            r.e = extra[slot];
            have = true;
        }
        portEXIT_CRITICAL(&mux);
        if (!have || !exportable(r.e)) return 0;
        return formatRow(r, buf, cap);
    }
} logSrc;

} // namespace

applog::Source& logSource() { return logSrc; }

// The whole table at once, for the console WIGLE dump; same bytes as the file.
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

uint32_t uniques() { return nUnique; }
uint32_t adverts() { return nAdverts; }
uint32_t tableDrops() { return nTableDrops; }
uint32_t longProbes() { return nLongProbes; }

} // namespace blescan
