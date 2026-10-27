// NM-CYD-C5 bring-up: draw UMBRAVOX on the real panel.
//
// This is the whole device side. Everything visual lives in ui/, compiled
// unchanged from what the simulator builds -- the only job here is to own the
// panel, own the backlight, and blit.
#include <Arduino.h>
#include <stdarg.h>
#include <Arduino_GFX_Library.h>

#include "screens.h"
#include "gnss.h"
#include "gpstrack.h"
#include <math.h>
#include "capture.h"
#include "blescan.h"
#include "touch.h"
#include "prefs.h"
#include "logging.h"
#include "sdlog.h"
#include "elog.h"
#include "enrollwin.h"
#include "perflog.h"
#include "alertlog.h"
#include "meshcore.h"
#include "mesh.h"
#include "chanplan.h"
#include "balance.h"
#include "idlemode.h"
#include "rehome.h"
#include "phase.h"
#include <esp_now.h>
#include "signatures.h"
#include "nav.h"
#include "layout.h"
#include "palette.h"
#include "wigle.h"

// ---- pins (RockBase NM-CYD-C5, confirmed against Documention/2-sch) -------
// LCD, touch and the SD card all share one SPI bus.
static constexpr int8_t PIN_SCK  = 6;
static constexpr int8_t PIN_MOSI = 7;
static constexpr int8_t PIN_MISO = 2;
static constexpr int8_t PIN_CS   = 23;
static constexpr int8_t PIN_DC   = 24;
static constexpr int8_t PIN_RST  = -1;   // tied to the C5's own reset
static constexpr int8_t PIN_BL   = 25;   // NOT 27 -- 27 is the WS2812
static constexpr int    PANEL_W  = 320;
static constexpr int    PANEL_H  = 240;

// GPS on header P5 (NOT P1 -- GPIO 4 appears on both, and only P5 carries the
// receiver's TX). Confirmed on the board: ~9600 baud, measured from a 98us
// minimum pulse width rather than guessed.
static constexpr int8_t PIN_GPS_RX = 4;    // we receive on 4
static constexpr int8_t PIN_GPS_TX = 5;
static constexpr int    GPS_BAUD   = 9600;

// 40MHz is the ceiling on this board, not a choice: measured 35ms/frame at
// both 40 and 80MHz, and 112ms at 10MHz, so the parameter works and 80 is
// being clamped. SPI above 40MHz needs the dedicated IOMUX pins, and SCK 6 /
// MOSI 7 here route through the GPIO matrix. The transfer cannot be made
// faster -- only smaller, which is what the dirty rectangles are for.
static constexpr int32_t SPI_HZ  = 40000000;

static Arduino_DataBus* bus = new Arduino_ESP32SPI(PIN_DC, PIN_CS, PIN_SCK, PIN_MOSI, PIN_MISO);
// Native panel is 240x320 portrait; rotation 3 lands it landscape, which is
// how the board is cased and how the UI is laid out.
static Arduino_TFT* gfx = new Arduino_ST7789(bus, PIN_RST, 3 /*rotation*/, false /*IPS*/,
                                             240, 320);

static ui::Canvas canvas;          // 38,400 bytes, internal SRAM by design
static uint16_t   lineBuf[PANEL_W];
static HardwareSerial gpsPort(1);

// One canvas rect -> the panel at 2x, nearest neighbour. Doubling into a line
// buffer and pushing each row twice keeps the transfer sequential, which is
// what matters: on a single-core C5 every millisecond in here is a
// millisecond not spent capturing.
static void blitRect(const uint16_t* src, const ui::Rect& r) {
    const int x0 = r.x < 0 ? 0 : r.x;
    const int y0 = r.y < 0 ? 0 : r.y;
    const int x1 = (r.x + r.w) > ui::W ? ui::W : (r.x + r.w);
    const int y1 = (r.y + r.h) > ui::H ? ui::H : (r.y + r.h);
    if (x1 <= x0 || y1 <= y0) return;
    const int w = x1 - x0;

    gfx->startWrite();
    gfx->writeAddrWindow(x0 * 2, y0 * 2, w * 2, (y1 - y0) * 2);
    for (int y = y0; y < y1; y++) {
        const uint16_t* s = src + (size_t)y * ui::W + x0;
        for (int x = 0; x < w; x++) {
            lineBuf[x * 2]     = s[x];
            lineBuf[x * 2 + 1] = s[x];
        }
        gfx->writePixels(lineBuf, w * 2);
        gfx->writePixels(lineBuf, w * 2);
    }
    gfx->endWrite();
}

// Defined with the console helpers below.
namespace { static uint32_t writeWigle(Print& out, uint32_t& skipped); }

// What the logger needs from the capture tables (logging.h). The clear runs
// inside logging::tick, in the main loop between cap::tick and the draw --
// the same task and point as RESET COUNTERS -- so nothing draws from a table
// mid-clear.
namespace logwire {
uint32_t wifiUniques() { return cap::uniques(); }
uint32_t bleUniques()  { return blescan::uniques(); }
void clearTables()     { cap::rollover(); blescan::rollover(); }
void onRollover(const logging::RollInfo& r) {
    const char* why = r.reset ? "reset" : "rollover";
    if (!r.newName[0])
        elog::event(elog::INFO, "log", "%s at %u APs (%u BLE), not logging", why,
                    (unsigned)r.wifiAps, (unsigned)r.bleAddrs);
    else if (r.forced)
        elog::event(elog::WARN, "log", "%s at %u APs (%u BLE), new file %s, forced: "
                    "%u rows not written", why, (unsigned)r.wifiAps, (unsigned)r.bleAddrs,
                    r.newName, (unsigned)r.lost);
    else
        elog::event(elog::INFO, "log", "%s at %u APs (%u BLE), new file %s", why,
                    (unsigned)r.wifiAps, (unsigned)r.bleAddrs, r.newName);
}
uint32_t clockMs() { return millis(); }
void onStopLoss(uint32_t lost, bool reset) {
    elog::event(elog::WARN, "log", "%u rows not written at stop%s", (unsigned)lost,
                reset ? " (reset pending)" : "");
}
logging::Hooks hooks() {
    logging::Hooks h;
    h.wifi = &cap::logSource();
    h.ble  = &blescan::logSource();
    h.wifiUniques = wifiUniques;
    h.bleUniques  = bleUniques;
    h.clearTables = clearTables;
    h.onRollover  = onRollover;
    h.clockMs     = clockMs;
    h.onStopLoss  = onStopLoss;
    return h;
}
// RESET COUNTERS: the full reset, which logging turns into a rollover to a
// new file while logging (never a rewrite of the current one).
void resetAll() { cap::reset(); blescan::reset(); }
} // namespace logwire

// --- collector tier -------------------------------------------------------
//
// The CYD's half of the three-tier mesh. When coordinators register, the CYD
// stops being the nodes' Core and becomes the aggregator: it hands each
// coordinator a disjoint band slice, balances nodes across them, and folds
// the ClusterBatch frames they deliver through the same cap::ingest the Core
// uses. With NO coordinator registered this whole path is inert -- every hook
// returns false and meshcore runs the node mesh exactly as it does today, so
// the CYD<->node path is byte-for-byte unchanged.
//
// COLLECT_CHAN is mesh::REPORT_CHAN: the channel the CYD camps to hear both
// coordinator batches and, when running the mesh alone, node reports.
namespace collector {
namespace {

constexpr uint8_t  COLLECT_CHAN = mesh::REPORT_CHAN;
constexpr uint32_t COORD_TIMEOUT_MS = 20000;   // silent this long => gone

struct Coord {
    uint8_t  mac[6];
    bool     live;
    uint32_t lastMs;
    uint8_t  clusterId;
    uint16_t nodes;        // nodes balanced onto this coordinator
    uint8_t  lossPct;      // its cluster's report loss, from its last batch
};
// At most two slices exist (chanplan::partition never makes a third), so at
// most two coordinators ever get one -- ruling: a slice-less coordinator is
// never assigned.
Coord    coords_[2];
int      nCoord_ = 0;

// A node is pinned to the coordinator it was first balanced onto, so repeated
// Hellos do not bounce it between clusters or double-count.
struct NodeMap { uint8_t mac[6]; uint8_t coord; bool used; };
NodeMap  nmap_[meshcore::MAX_NODES];

// The receive callback runs in the Wi-Fi task; tick() runs in loop(). Frames
// that must send a reply are queued here and drained in tick(), because
// esp_now_send() does not belong in the receive path.
enum EvKind : uint8_t { EV_REG, EV_HELLO };
struct Ev { uint8_t kind; uint8_t mac[6]; uint8_t wants5; };
Ev       evq_[16];
int      evN_ = 0;
portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;

uint8_t  bcast[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
uint32_t batchesRx = 0, recsRx = 0;

bool macEq(const uint8_t* a, const uint8_t* b) {
    for (int i = 0; i < 6; i++) if (a[i] != b[i]) return false;
    return true;
}

void pushEv(uint8_t kind, const uint8_t* mac, uint8_t wants5) {
    portENTER_CRITICAL(&mux);
    if (evN_ < (int)(sizeof(evq_) / sizeof(evq_[0]))) {
        Ev& e = evq_[evN_++];
        e.kind = kind;
        for (int i = 0; i < 6; i++) e.mac[i] = mac[i];
        e.wants5 = wants5;
    }
    portEXIT_CRITICAL(&mux);
}

// Hand every live coordinator its slice (re-partitioning for the current
// count). partition returns the slice count; we honour it and never address a
// coordinator beyond it.
void reassignAll(int only = -1) {
    chanplan::Slice sl[2];
    const int n = chanplan::partition(nCoord_, sl);
    for (int i = 0; i < nCoord_ && i < n; i++) {
        if (only >= 0 && i != only) continue;
        mesh::CoordAssign a = {};
        a.h.magic = mesh::MAGIC;
        a.h.ver   = mesh::VERSION;
        a.h.netId = mesh::netId;
        a.h.kind  = mesh::MSG_CASSIGN;
        memcpy(a.h.mac, coords_[i].mac, 6);
        a.clusterId   = coords_[i].clusterId;
        a.reportChan  = sl[i].reportChan;
        a.collectChan = COLLECT_CHAN;
        a.nChan = sl[i].nChan > mesh::MAX_SLICE ? mesh::MAX_SLICE : sl[i].nChan;
        for (int k = 0; k < a.nChan; k++) a.chans[k] = sl[i].chans[k];
        esp_now_send(bcast, (const uint8_t*)&a, sizeof a);
    }
}

void handleReg(const uint8_t* mac, uint8_t wants5, uint32_t now) {
    (void)wants5;   // ignored for now: every board is dual-band, slices go out in registration order
    for (int i = 0; i < nCoord_; i++)
        if (coords_[i].live && macEq(coords_[i].mac, mac)) {
            coords_[i].lastMs = now;      // already ours; just re-assure it
            reassignAll(i);
            return;
        }
    // New coordinator. Only accept it if partition would actually give the
    // extra slice -- for a third coordinator it would not (partition caps at
    // two), so it is refused rather than left stranded with no channels.
    chanplan::Slice sl[2];
    const int want = chanplan::partition(nCoord_ + 1, sl);
    if (nCoord_ >= want) {
        static uint32_t lastWarn = 0;
        if (now - lastWarn > 5000) {
            lastWarn = now;
            Serial.printf("collector: coordinator %02X:%02X:%02X refused -- only %d "
                          "slices available\n", mac[3], mac[4], mac[5], want);
        }
        return;
    }
    const int idx = nCoord_++;
    for (int k = 0; k < 6; k++) coords_[idx].mac[k] = mac[k];
    coords_[idx].live = true;
    coords_[idx].lastMs = now;
    coords_[idx].clusterId = (uint8_t)idx;
    coords_[idx].nodes = 0;
    coords_[idx].lossPct = 0;
    Serial.printf("collector: coordinator %02X:%02X:%02X -> cluster %d (%d live)\n",
                  mac[3], mac[4], mac[5], idx, nCoord_);
    reassignAll();
}

int findNode(const uint8_t* mac) {
    for (int i = 0; i < meshcore::MAX_NODES; i++)
        if (nmap_[i].used && macEq(nmap_[i].mac, mac)) return i;
    return -1;
}

// Pick the coordinator for a node: the live one with the fewest nodes, among
// those whose slice the node's radio can actually cover. A 2.4-only node
// never goes to the 5GHz slice (reportChan > 14 marks it).
int pickCoord(uint8_t wants5, const chanplan::Slice* sl, int nSlice) {
    // Marshal the live coordinator table into the flat arrays balance::pick
    // takes; the policy itself lives in balance.h so it can be tested at scale.
    bool     live[2]  = {false, false};
    uint16_t nodes[2] = {0, 0};
    uint8_t  rc[2]    = {0, 0};
    const int nc = nCoord_ < 2 ? nCoord_ : 2;
    for (int i = 0; i < nc; i++) { live[i] = coords_[i].live; nodes[i] = coords_[i].nodes; }
    const int ns = nSlice < 2 ? nSlice : 2;
    for (int i = 0; i < ns; i++) rc[i] = sl[i].reportChan;
    return balance::pick(wants5, live, nodes, rc, nCoord_, nSlice);
}

void handleHello(const uint8_t* mac, uint8_t wants5, uint32_t now) {
    if (nCoord_ == 0) return;
    chanplan::Slice sl[2];
    const int nSlice = chanplan::partition(nCoord_, sl);

    int c = findNode(mac);
    if (c >= 0) {
        c = nmap_[c].coord;
        if (c >= nCoord_ || !coords_[c].live) return;   // its coordinator went away
    } else {
        c = pickCoord(wants5, sl, nSlice);
        if (c < 0) return;
        int slot = -1;
        for (int i = 0; i < meshcore::MAX_NODES; i++) if (!nmap_[i].used) { slot = i; break; }
        if (slot >= 0) {
            for (int k = 0; k < 6; k++) nmap_[slot].mac[k] = mac[k];
            nmap_[slot].coord = (uint8_t)c;
            nmap_[slot].used = true;
        }
        coords_[c].nodes++;
    }
    (void)now;

    // Provisional report phase: this node's ordinal among the nodes pinned to
    // its coordinator, spread across the report window. The coordinator sends
    // its own Assign (the authoritative offset) once the node reports in;
    // until then this staggers a cluster's first deliveries instead of letting
    // them all land together. A full pin table falls back to "last in line".
    int ord = -1, total = 0;
    for (int i = 0; i < meshcore::MAX_NODES; i++) {
        if (!nmap_[i].used || nmap_[i].coord != c) continue;
        if (macEq(nmap_[i].mac, mac)) ord = total;
        total++;
    }
    if (ord < 0) { total = coords_[c].nodes; ord = total > 0 ? total - 1 : 0; }

    // Redirect the (unchanged) node: point it at the coordinator as its Core,
    // on the coordinator's report channel, sweeping that coordinator's slice.
    // The coordinator refines the per-node plan once the node reports in.
    mesh::Assign a = {};
    a.h.magic = mesh::MAGIC;
    a.h.ver   = mesh::VERSION;
    a.h.netId = mesh::netId;
    a.h.kind  = mesh::MSG_ASSIGN;
    memcpy(a.h.mac, mac, 6);
    memcpy(a.coreMac, coords_[c].mac, 6);
    a.reportChan = sl[c].reportChan;
    a.nChan = sl[c].nChan > mesh::MAX_ASSIGN ? mesh::MAX_ASSIGN : sl[c].nChan;
    for (int k = 0; k < a.nChan; k++) a.chans[k] = sl[c].chans[k];
    a.camp = 0;
    a.phaseMs = (uint16_t)mesh_phase::offset(ord, total, mesh::REPORT_MS);
    esp_now_send(bcast, (const uint8_t*)&a, sizeof a);
}

} // namespace

// True once at least one coordinator is live: the CYD then camps the
// collection channel and leaves node handling to the coordinators.
bool active() { return nCoord_ > 0; }

// Live coordinators, and the worst report loss among them. Both read the
// coordinator table the receive callback also writes, so take the same lock.
int count() {
    portENTER_CRITICAL(&mux);
    const int n = nCoord_;
    portEXIT_CRITICAL(&mux);
    return n;
}

uint8_t maxLossPct() {
    uint8_t m = 0;
    portENTER_CRITICAL(&mux);
    for (int i = 0; i < nCoord_; i++)
        if (coords_[i].live && coords_[i].lossPct > m) m = coords_[i].lossPct;
    portEXIT_CRITICAL(&mux);
    return m;
}

// The frame hook meshcore calls before its own node handling. Returns true to
// claim a frame. With no coordinator yet, only a CoordReg is claimed; node
// Hellos and Reports fall through to meshcore untouched (byte-for-byte today).
bool onFrame(const uint8_t* src, const uint8_t* data, int len) {
    (void)src;
    // The CYD enrols others; it never adopts an invite itself. Claim and drop.
    if (len >= (int)sizeof(mesh::Hdr) &&
        ((const mesh::Hdr*)data)->kind == mesh::MSG_ENROLL) return true;
    if (mesh::valid(data, len, mesh::MSG_CREG, (int)sizeof(mesh::CoordReg))) {
        const mesh::CoordReg* c = (const mesh::CoordReg*)data;
        pushEv(EV_REG, c->h.mac, c->wants5);
        return true;
    }
    if (mesh::valid(data, len, mesh::MSG_CBATCH, (int)sizeof(mesh::ClusterBatch))) {
        const mesh::ClusterBatch* b = (const mesh::ClusterBatch*)data;
        // Only a coordinator we have registered may deliver. A batch never
        // creates a coordinator entry (only a CoordReg does), so a stray or
        // spoofed batch cannot fill the two slots and lock the real one out; a
        // coordinator we timed out re-registers with its own CoordReg.
        bool known = false;
        portENTER_CRITICAL(&mux);
        for (int i = 0; i < nCoord_; i++)
            if (coords_[i].live && macEq(coords_[i].mac, b->h.mac)) {
                coords_[i].lastMs = millis();
                coords_[i].lossPct = b->lossPct;
                known = true;
            }
        portEXIT_CRITICAL(&mux);
        if (!known) return true;          // claimed, but not folded
        batchesRx++;
        // Answer every batch with the sender's CoordAssign. A coordinator is
        // only on this channel while it delivers, so this is how a missed or
        // changed assignment reaches it.
        pushEv(EV_REG, b->h.mac, 1);
        char ssid[21];
        for (int k = 0; k < b->count && k < mesh::MAX_BATCH; k++) {
            const mesh::Rec& rc = b->recs[k];
            const int n = rc.ssidLen > 20 ? 20 : rc.ssidLen;
            for (int j = 0; j < n; j++) ssid[j] = rc.ssid[j];
            ssid[n] = 0;
            cap::ingest(rc.bssid, rc.rssi, rc.chan,
                        rc.band ? ui::BAND_5 : ui::BAND_24, ssid, rc.ageMs,
                        (uint8_t)(b->clusterId + 1), rc.auth);
            recsRx++;
        }
        return true;
    }
    // A node announcing itself. Claim it only once coordinators exist, so the
    // zero-coordinator path is left entirely to meshcore.
    if (nCoord_ > 0 &&
        mesh::valid(data, len, mesh::MSG_HELLO, (int)sizeof(mesh::Hello))) {
        const mesh::Hello* hh = (const mesh::Hello*)data;
        pushEv(EV_HELLO, hh->h.mac, hh->wants5);
        return true;
    }
    // A Report still arriving straight at the CYD means its sender never moved
    // onto a coordinator -- it bound to the CYD before any coordinator existed,
    // or across a CYD reboot. Nudge it across with the same redirect a Hello
    // gets (rate-limited so a chatty node cannot storm us), then let the Report
    // fall through to meshcore so the node's catch is not lost during the cycle
    // it takes to migrate. wants5 is unknown from a Report, so assume dual-band;
    // a genuinely 2.4-only node mis-sent to a 5GHz cluster fails once and
    // re-Hellos with the right flag, self-correcting.
    static uint32_t lastStragglerMs = 0;
    if (mesh::valid(data, len, mesh::MSG_REPORT, (int)sizeof(mesh::Report)) &&
        straggler::shouldRedirect(nCoord_ > 0, millis(), lastStragglerMs, 400)) {
        lastStragglerMs = millis();
        pushEv(EV_HELLO, ((const mesh::Report*)data)->h.mac, 1);
    }
    return false;
}

void tick(uint32_t now) {
    // Drain the receive-path events into replies.
    for (;;) {
        Ev e;
        portENTER_CRITICAL(&mux);
        if (evN_ == 0) { portEXIT_CRITICAL(&mux); break; }
        e = evq_[--evN_];
        portEXIT_CRITICAL(&mux);
        if (e.kind == EV_REG)   handleReg(e.mac, e.wants5, now);
        if (e.kind == EV_HELLO) handleHello(e.mac, e.wants5, now);
    }

    // Expire a coordinator that has gone quiet, compacting the array so the
    // surviving one keeps cluster 0 (the whole-band slice) when it is alone.
    bool changed = false;
    uint8_t goneId[2];
    int nGone = 0;
    portENTER_CRITICAL(&mux);
    for (int i = 0; i < nCoord_; i++) {
        if (coords_[i].live && (now - coords_[i].lastMs) > COORD_TIMEOUT_MS) {
            if (nGone < 2) goneId[nGone++] = coords_[i].clusterId;
            // Free the dead coordinator's pins and shift the survivors' down
            // with the array, so each survivor keeps its real node count and a
            // migrating node is balanced against true loads.
            for (int k = 0; k < meshcore::MAX_NODES; k++) {
                if (!nmap_[k].used) continue;
                if (nmap_[k].coord == i) nmap_[k].used = false;
                else if (nmap_[k].coord > i) nmap_[k].coord--;
            }
            for (int j = i; j < nCoord_ - 1; j++) coords_[j] = coords_[j + 1];
            nCoord_--;
            i--;
            changed = true;
        }
    }
    portEXIT_CRITICAL(&mux);
    for (int g = 0; g < nGone; g++)
        Serial.printf("collector: coordinator cluster %u timed out\n", goneId[g]);
    if (changed) {
        for (int i = 0; i < nCoord_; i++) coords_[i].clusterId = (uint8_t)i;
        if (nCoord_ > 0) reassignAll();
    }
}

} // namespace collector

void setup() {
    Serial.begin(115200);
    delay(200);
    Serial.println();
    Serial.println("UMBRAVOX bring-up");
    Serial.printf("chip  : %s rev %d, %d core(s)\n",
                  ESP.getChipModel(), ESP.getChipRevision(), ESP.getChipCores());
    Serial.printf("flash : %u bytes\n", (unsigned)ESP.getFlashChipSize());
    Serial.printf("heap  : %u free\n", (unsigned)ESP.getFreeHeap());
    Serial.printf("canvas: %u bytes\n", (unsigned)sizeof(canvas));
    Serial.printf("psram : %u bytes (%u free)\n",
                  (unsigned)ESP.getPsramSize(), (unsigned)ESP.getFreePsram());

    if (!gfx->begin(SPI_HZ)) {
        Serial.println("gfx->begin() FAILED");
        for (;;) delay(1000);
    }
    gfx->fillScreen(RGB565_BLACK);

    // Arduino core 3.x LEDC. The old ledcSetup()/ledcAttachPin() pair is gone,
    // and ledcWrite() now takes the pin rather than a channel -- this is the
    // API break that makes porting core-2 firmware to the C5 expensive.
    ledcAttach(PIN_BL, 5000, 8);
    ledcWrite(PIN_BL, 204);     // 80%, matching Settings' default

    gpsPort.begin(GPS_BAUD, SERIAL_8N1, PIN_GPS_RX, PIN_GPS_TX);
    cap::begin();
    blescan::begin();
    meshcore::begin();
    // The collector tier watches the same ESP-NOW receive path. Until a
    // coordinator registers it claims nothing, so the node mesh is unchanged.
    meshcore::setCollector(collector::onFrame);   // netId is loaded + logged in loop()
    touch::begin();
    // Card first, then the session machine: it resumes into a fresh file when
    // the last run ended mid-session -- never the old one, which is left as
    // the power cut found it. A name already on the card is never reused
    // (logging counts past WD-NNNN files and skips any existing name).
    sdlog::begin();
    elog::begin();     // boot banner + reset reason to /diag.log (after the card)
    logging::begin(prefs::loggingWas(), logwire::hooks(), millis(), ui::Gnss::utcEpoch(millis()));
    Serial.printf("panel up @ %d MHz SPI, GPS on GPIO %d @ %d\n",
                  (int)(SPI_HZ / 1000000), (int)PIN_GPS_RX, GPS_BAUD);
}

// Console logging that the compiler checks.
//
// Serial.printf() has no printf format attribute, so passing an int to a %s
// compiles clean and then dereferences it as a pointer. That crashed the
// board once already: an edit changed the arguments of the status line but
// its format string did not match, and `%s` got an integer 0.
__attribute__((format(printf, 1, 2)))
static void logf(const char* fmt, ...) {
    char buf[400];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    Serial.print(buf);
}

// --- test alerts --------------------------------------------------------
// Send "TEST" over the console to walk the alert card through all three
// confidence levels. Kept rather than scaffolded out: there is no other way
// to see what a HIGH-confidence Flock alert looks like without standing in
// front of a Flock camera, and the card's colours are the thing most likely
// to need adjusting.
namespace {

struct TestStep { uint8_t threat, conf; const char* why; const char* name; };
const TestStep TEST_SEQ[] = {
    {ui::TH_FLOCK,   ui::CONF_HIGH, "FLOCK-MA-L",      "B4:1E:52"},
    {ui::TH_GLASSES, ui::CONF_MED,  "RAYBANMETA",      "4F:2A:08"},
    {ui::TH_CAMERA,  ui::CONF_LOW,  "TUYA",            "A4:C1:38"},
    // The two behavioural findings: measured, not matched.
    {ui::TH_DEAUTH,  ui::CONF_HIGH, "FLOOD",           "3C:A7:19 92/S"},
    {ui::TH_ROGUE,   ui::CONF_MED,  "SSID AUTH CLASH", "HOLLOWBROOK"},
    {ui::TH_FOLLOW,  ui::CONF_HIGH, "TRAVELLED WITH YOU", "A4:2C:1E 1.9KM"},
};
constexpr int  TEST_N  = (int)(sizeof(TEST_SEQ) / sizeof(TEST_SEQ[0]));
constexpr uint32_t TEST_STEP_MS = 5000;

int      testStep  = -1;          // -1 = not running
uint32_t testAt    = 0;

// Bridges the pure header sink to a Print, then appends both exporters.
struct PrintSink : ui_wigle::Sink {
    Print& p;
    PrintSink(Print& pp) : p(pp) {}
    void put(const char* s) override { p.print(s); }
};

static uint32_t writeWigle(Print& out, uint32_t& skipped) {
    PrintSink sink(out);
    ui_wigle::header(sink);
    const uint32_t w = cap::exportRows(out, skipped);
    const uint32_t b = blescan::exportRows(out, skipped);
    return w + b;
}

// Feed a synthesized NMEA sentence to the parser (computes the checksum), so a
// canned fix can be injected over the console to test the geolocated-row path
// without sky. `body` is everything between '$' and '*'.
static void injectNmea(const char* body) {
    uint8_t ck = 0;
    for (const char* p = body; *p; p++) ck ^= (uint8_t)*p;
    char s[96];
    snprintf(s, sizeof s, "$%s*%02X", body, (unsigned)ck);
    ui::Gnss::sentence(s, millis());
}

// --- enrolment: pairing a node/coordinator onto this CYD's private network ---
// ENROLL (from the UI or the console) mints a random private network id if this
// CYD is still open (id 0), saves it, and broadcasts an invite for 60s. An OPEN
// node/coordinator in range adopts the id and locks to it; already-enrolled gear
// ignores the invite, so a neighbour cannot steal a node that is already yours.
namespace enroll {
const uint8_t bcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
enrollwin::State win;                // when to transmit, and for how long (enrollwin.h)
uint8_t  oldNet = 0;                 // the net we are telling our nodes to leave

// Any board still answering on this net: nodes in the Core's table, or live
// coordinators. A reset stays up for as long as this is true.
bool boardsHeard() { return meshcore::count() > 0 || collector::active(); }

// ADOPT: mint a private net if still open, then invite open devices onto it.
void open(uint32_t nowMs) {
    if (win.mode == enrollwin::UNENROL) {
        Serial.println("enroll: a reset is still running -- wait for it to finish");
        return;                              // never abandon an un-enrol half way
    }
    if (mesh::netId == 0) {
        uint8_t id = 0;
        while (id == 0) id = (uint8_t)(esp_random() & 0xFF);   // 1..255
        mesh::netId = id;
        prefs::setNetId(id);
    }
    enrollwin::adopt(win, nowMs);
    Serial.printf("enroll: network %u open for %us -- power up an OPEN node now\n",
                  (unsigned)mesh::netId, (unsigned)(enrollwin::ADOPT_MS / 1000));
    elog::event(elog::INFO, "mesh", "enroll open, network=%u", (unsigned)mesh::netId);
}

// UNENROL: tell our own boards (on our net) to go open, and stay on this net
// until they have all gone, then go open ourselves.
void reset(uint32_t nowMs) {
    if (mesh::netId == 0) { win.mode = enrollwin::IDLE; return; }   // already open
    if (win.mode == enrollwin::UNENROL) return;                     // a double tap
    oldNet = mesh::netId;
    enrollwin::unenrol(win, nowMs);
    Serial.printf("enroll: resetting network %u -- staying on it until every board has left\n",
                  (unsigned)oldNet);
    elog::event(elog::INFO, "mesh", "enroll reset begins, network %u", (unsigned)oldNet);
}

// Recovery: put this CYD back on a known net, e.g. to reach boards a reset
// missed (their old net is in the diag log). 0 means open.
void setNet(uint8_t id) {
    mesh::netId = id;
    prefs::setNetId(id);
    win.mode = enrollwin::IDLE;
    Serial.printf("enroll: network set to %u%s\n", (unsigned)id, id ? "" : " (open)");
    elog::event(elog::INFO, "mesh", "network set to %u", (unsigned)id);
}

bool active(uint32_t nowMs) { (void)nowMs; return win.mode != enrollwin::IDLE; }
int  secsLeft(uint32_t nowMs) { return enrollwin::secsLeft(win, nowMs); }

void tick(uint32_t nowMs) {
    const bool unenrolling = win.mode == enrollwin::UNENROL;
    const enrollwin::Tick k = enrollwin::tick(win, nowMs, boardsHeard());
    if (k.done) {
        if (unenrolling) {                           // the CYD goes open last
            mesh::netId = 0; prefs::setNetId(0);
            Serial.println("enroll: back on the open network (id 0)");
            if (k.gaveUp) {
                // Boards were still answering at the cap: say so, and keep the
                // old net on record so NETSET can go back for them.
                Serial.printf("enroll: WARNING boards still answered on net %u -- "
                              "NETSET %u to go back for them\n", (unsigned)oldNet, (unsigned)oldNet);
                elog::event(elog::WARN, "mesh",
                            "reset gave up with boards still on network %u (NETSET %u to retry)",
                            (unsigned)oldNet, (unsigned)oldNet);
            } else {
                elog::event(elog::INFO, "mesh", "network %u cleared, back on the open network",
                            (unsigned)oldNet);
            }
        }
        return;
    }
    if (!k.send) return;
    mesh::Enroll e = {};
    e.h.magic = mesh::MAGIC;  e.h.ver  = mesh::VERSION;  e.h.kind = mesh::MSG_ENROLL;
    e.token   = mesh::ENROLL_TOKEN;
    const bool adopting = win.mode == enrollwin::ADOPT;
    e.h.netId = adopting ? mesh::netId : oldNet;
    e.reset   = adopting ? 0 : 1;
    esp_now_send(bcast, (const uint8_t*)&e, sizeof e);
}
} // namespace enroll

void pollConsole() {
    static char line[32];
    static int  n = 0;
    while (Serial.available()) {
        const char c = (char)Serial.read();
        if (c == '\n' || c == '\r') {
            if (!n) continue;
            line[n] = 0;
            n = 0;
            if (!strncmp(line, "TEST", 4)) {
                testStep = 0;
                testAt   = millis();
                Serial.printf("test alert: %d steps, %d ms each\n", TEST_N, (int)TEST_STEP_MS);
            } else if (!strncmp(line, "MESHSTAT", 8)) {
                uint32_t tot, mg, vr, ln, ns;
                meshcore::rxStats(tot, mg, vr, ln, ns);
                logf("meshstat: callbacks=%lu  rejected: magic=%lu ver=%lu "
                     "len=%lu noslot=%lu  accepted reports=%lu\n",
                     (unsigned long)tot, (unsigned long)mg, (unsigned long)vr,
                     (unsigned long)ln, (unsigned long)ns,
                     (unsigned long)meshcore::reportsRx());
            } else if (!strncmp(line, "WIGLE", 5)) {
                // One file, two sources. Main owns the markers; the header
                // lives in wigle.h so Serial and the SD file cannot disagree.
                Serial.println("=== WIGLE BEGIN ===");
                uint32_t skipped = 0;
                const uint32_t n = writeWigle(Serial, skipped);
                logf("=== WIGLE END === %lu rows, %lu skipped "
                     "(no position or no UTC)\n",
                     (unsigned long)n, (unsigned long)skipped);
            } else if (!strncmp(line, "SDLS", 4)) {
                sdlog::list(Serial);
            } else if (!strncmp(line, "SDCAT", 5)) {
                const char* nm = line + 5;
                while (*nm == ' ') nm++;
                char path[40];
                if (*nm != '/') { snprintf(path, sizeof path, "/%s", nm); nm = path; }
                Serial.printf("=== SDCAT %s ===\n", nm);
                if (!sdlog::dump(Serial, nm)) Serial.println("sdlog: no such file");
                Serial.println("=== SDCAT END ===");
            } else if (!strncmp(line, "GPSFIX", 6)) {
                // Canned fix for bench testing without sky. Run it a few times
                // over ~10s so re-heard APs get stamped located.
                injectNmea("GPRMC,120000.00,A,3746.4940,N,12225.1640,W,0.0,0.0,190126,,,A");
                injectNmea("GPGGA,120000.00,3746.4940,N,12225.1640,W,1,08,0.9,10.0,M,0.0,M,,");
                Serial.println("gpsfix: injected 37.7749,-122.4194 (2026-01-19)");
            } else if (!strncmp(line, "PERF", 4)) {
                Serial.println("=== PERF BEGIN ===");
                perflog::dump(Serial);
                Serial.println("=== PERF END ===");
            } else if (!strncmp(line, "DIAG", 4)) {
                Serial.println("=== DIAG BEGIN ===");
                elog::dump(Serial);
                Serial.println("=== DIAG END ===");
            } else if (!strncmp(line, "ENROLL", 6)) {
                enroll::open(millis());
            } else if (!strncmp(line, "NETRESET", 8)) {
                enroll::reset(millis());
            } else if (!strncmp(line, "NETSET", 6)) {
                const char* a = line + 6;
                while (*a == ' ') a++;
                int v = atoi(a);
                if (v < 0 || v > 255 || !*a) Serial.println("usage: NETSET <0..255>  (0 = open)");
                else enroll::setNet((uint8_t)v);
            } else {
                Serial.printf("unknown command '%s' "
                              "(try TEST, WIGLE, SDLS, SDCAT <file>, DIAG, PERF, GPSFIX, ENROLL, NETRESET, NETSET <id>)\n", line);
            }
        } else if (n < 31) {
            line[n++] = c;
        }
    }
}

// Fills `s` from the test sequence if one is running. Deliberately ignores
// the ALERT floor -- the point is to see the card, including the LOW one the
// floor would normally hide.
bool testAlert(ui::State& s, uint32_t nowMs) {
    if (testStep < 0) return false;
    if (nowMs - testAt >= TEST_STEP_MS) {
        testAt = nowMs;
        if (++testStep >= TEST_N) { testStep = -1; return false; }
    }
    const TestStep& st = TEST_SEQ[testStep];
    s.alertActive = true;
    s.alertThreat = st.threat;
    s.alertConf   = st.conf;
    snprintf(s.alertWhy,  sizeof(s.alertWhy),  "%s", st.why);
    snprintf(s.alertName, sizeof(s.alertName), "%s", st.name);
    return true;
}

} // namespace

void loop() {
    static uint32_t frame = 0;
    static ui::Dirty dirty;
    const uint32_t t0 = millis();
    perflog::loopStart(t0);        // the gap between loops is the stall we want to see

    // Drain the receiver every frame. At 9600 baud a frame's worth is well
    // under 100 bytes, so this never blocks long enough to matter.
    while (gpsPort.available()) ui::Gnss::feed((char)gpsPort.read(), millis());

    cap::tick(millis());
    // While a coordinator is live the CYD is a pure collector: its Core stops
    // handing out assignments so it never re-claims a node the collector has
    // placed on a coordinator (which would otherwise fight the redirect).
    meshcore::setAssigning(!collector::active());
    meshcore::tick(millis());
    collector::tick(millis());
    enroll::tick(millis());
    blescan::tick(millis());
    logging::tick(millis(), ui::Gnss::utcEpoch(millis()));

    // Diagnostics to /diag.log: an SD write failure, once per occurrence, and a
    // low-heap warning once (re-armed after the heap recovers) so a slow leak
    // leaves a fingerprint instead of just a silent reboot.
    if (logging::takeWriteError())
        elog::event(elog::ERR, "sd", "log write failed (card full or I/O error)");
    if (logging::takeFileRestart())
        elog::event(elog::WARN, "sd", "log file shorter than written, continuing in %s",
                    logging::fileName());
    {
        static uint32_t lastHeapChk = 0;
        static bool heapWarned = false;
        const uint32_t now = millis();
        if (now - lastHeapChk >= 5000) {
            lastHeapChk = now;
            const uint32_t freeK = ESP.getFreeHeap() >> 10;
            if (freeK < 40 && !heapWarned) {
                elog::event(elog::WARN, "heap", "low, %uKB free", (unsigned)freeK);
                heapWarned = true;
            } else if (freeK > 60) {
                heapWarned = false;     // recovered: re-arm the one-shot
            }
        }
    }

    // With nodes in the mesh the Core stops sweeping and holds the report
    // channel. Measured: a sweeping Core hears only 4% of what a node sends,
    // because it is on channel 1 for one dwell in twenty-one. Holding it
    // costs the Core's own sweep and buys every node's entire catch.
    {
        static bool camped = false;
        // Camp the report/collection channel either when the CYD runs the mesh
        // itself and has nodes, or when coordinators are live and deliver their
        // batches there. With no coordinator this is exactly today's rule.
        // An open enrol window also parks the Core on the report channel: that is
        // where unassigned boards wait and where coordinators visit, so a
        // broadcast sent while sweeping would only land by luck.
        const bool want = meshcore::count() > 0 || collector::active()
                       || enroll::active(millis());
        if (want != camped) {
            camped = want;
            cap::setCamp(want ? mesh::REPORT_CHAN : 0);
            Serial.printf("mesh: %s\n", want
                ? (meshcore::count() > 0
                       ? "nodes present -- core holding the report channel"
                   : collector::active()
                       ? "coordinator live -- holding the collection channel"
                       : "enrol window open -- core holding the report channel")
                : "no nodes -- core sweeping again");
        }
    }

    // A default-constructed State, filled only from real sources. It used to
    // start from demoState(), which quietly left four invented values on the
    // glass -- uptime, battery, and an SD status claiming a card was present
    // with a log file name to match. Anything not filled below now shows its
    // honest default instead.
    ui::State s;
    s.uptimeS = millis() / 1000;

    // The animation clock. Driven by time, not by the frame counter: the
    // frame rate is not constant (an alert animating costs 32ms against a
    // steady 13ms), so a per-frame tick made the sweep visibly slow down
    // exactly when something interesting was on screen.
    //
    // Losing this is silent -- the dial simply freezes, with nothing logged
    // and nothing failing. It went missing when the device stopped borrowing
    // demoState(), which had been setting it.
    s.frame = millis() / 16;
    s.sdOk    = sdlog::present();
    cap::fill(s, millis());
    blescan::fill(s, millis());
    meshcore::fill(s, millis());
    s.coreCamping = cap::camping();
    s.coreChan    = s.chan;
    s.coreFrames  = cap::localFrames();
    s.meshHits    = cap::frames();
    // The CYD's own node table is empty once coordinators are live, so loss
    // is the worse of that and what the coordinators report for their clusters.
    {
        const uint8_t own = meshcore::lossPct(), cl = collector::maxLossPct();
        s.meshLossPct = own > cl ? own : cl;
        s.meshCoords  = (uint8_t)collector::count();
        s.netId       = mesh::netId;
        s.enrollSecs  = (uint8_t)enroll::secsLeft(millis());
    }
    s.coreFound   = cap::foundBy(0);

    // Declared before the dimmer, which needs the brightness and timeout
    // settings to decide anything.
    static ui::Nav nav;
    static ui::Settings set;
    static bool loaded = false;
    if (!loaded) {
        prefs::load(set);
        loaded = true;
        // Touch hit-testing reads the theme's geometry (dial, spectrum), and
        // the first navTouch runs before the draw-time apply below. Same
        // task, before any draw, so this is safe.
        pal::setTheme(set.theme);
        mesh::netId = prefs::netId(mesh::netId);   // NVS overrides the build default
        Serial.printf("mesh: network id %u%s\n", (unsigned)mesh::netId,
                      mesh::netId == 0 ? " (open -- ENROLL to make a private net)" : "");
    }

    const touch::Point tp = touch::read();

    // --- idle dimming ---------------------------------------------------
    //
    // Worth more than comfort on a night drive: with the panel dark there is
    // no reason to draw or blit, and that is ~13ms of every frame handed
    // back to packet capture.
    //
    // The touch that wakes the screen is swallowed rather than passed on. A
    // panel you cannot see is a panel you cannot aim at, so the first
    // contact should only ever mean "wake" -- otherwise reaching for a dark
    // screen navigates somewhere at random.
    static uint32_t lastTouchMs = 0;
    static bool     dimmed = false;
    static bool     wakeScreen = false;
    static uint8_t  pwmNow = 0;
    static bool     pwmInit = false;
    static bool     animDirtied = false;   // the animation overwrote the canvas

    if (wakeScreen) {
        wakeScreen = false;
        lastTouchMs = millis();
        dimmed = false;
    }

    bool tpDown = tp.down;
    if (tpDown) {
        if (dimmed) { tpDown = false; }        // this one only wakes
        lastTouchMs = millis();
        dimmed = false;
    }
    const uint16_t idleS = ui::IDLE_S[set.idleIdx % ui::IDLE_N];
    if (idleS && !dimmed && (millis() - lastTouchMs) > (uint32_t)idleS * 1000)
        dimmed = true;

    // Dim to a fraction of the chosen brightness rather than a fixed level,
    // so someone running at 3% does not get *brighter* when it dims.
    const uint8_t dimPct = set.bright > 16 ? set.bright / 8 : 2;
    const uint8_t brightPwm = (uint8_t)((uint32_t)set.bright * 255 / 100);
    const uint8_t dimPwm    = (uint8_t)((uint32_t)dimPct * 255 / 100);
    // The mode picks the dimmed level (OFF -> 0) and whether the dark panel
    // runs the animation; DIM reproduces the original numbers exactly.
    const idlemode::Plan ip = idlemode::plan(set.idleMode, dimmed, pwmNow,
                                             brightPwm, dimPwm, idleS != 0);
    const uint8_t wantPwm = ip.pwm;
    if (!pwmInit) { pwmNow = wantPwm; pwmInit = true; }
    if (pwmNow != wantPwm) {
        // Ramp rather than snap: a backlight that steps is startling in the
        // dark, and the fade costs nothing.
        const int step = 6;
        if (pwmNow < wantPwm) pwmNow = (uint8_t)(pwmNow + step > wantPwm ? wantPwm : pwmNow + step);
        else                  pwmNow = (uint8_t)(pwmNow - step < wantPwm ? wantPwm : pwmNow - step);
        ledcWrite(PIN_BL, pwmNow);
    }
    // plan() judged the ramp with last pass's pwmNow; the ramp may have
    // arrived just now, so re-judge with the current value (as before).
    const idlemode::Plan ipNow = idlemode::plan(set.idleMode, dimmed, pwmNow,
                                                brightPwm, dimPwm, idleS != 0);
    const bool screenOff = ipNow.suspend;
    const bool animOn    = ipNow.anim;

    // Nav consumes the touch before the screen sees it, so a swipe that
    // changes screens does not also land as a tap on the one arriving.
    const uint16_t rows = nav.screen == ui::SCR_ECHOES ? cap::echoCount()
                        : nav.screen == ui::SCR_OMENS ? alertlog::count()
                        : nav.filter == ui::F_BLE ? blescan::sightCount()
                        : cap::sightCount(nav.filter == ui::F_5G);
    const int visible = nav.screen == ui::SCR_OMENS  ? ui::MAX_OMEN_ROWS
                      : nav.screen == ui::SCR_ECHOES ? ui::MAX_ECHO_ROWS : ui::ROWS;
    nav.maxScroll = (int16_t)(rows > visible ? rows - visible : 0);
    const bool switched = ui::navTouch(nav, set, tpDown, tp.x, tp.y);

    // Push settings to the hardware only when they actually move: changing
    // the BLE duty restarts the scanner, so doing it every frame would mean
    // never scanning at all.
    static ui::Settings applied;
    if (set.dwellMs != applied.dwellMs) cap::setDwell(set.dwellMs);
    if (set.bands   != applied.bands)   cap::setBands(set.bands);
    if (set.bleDuty != applied.bleDuty) blescan::setDuty(set.bleDuty);
    applied = set;
    prefs::tick(set, millis());

    if (nav.logToggleRequested) {
        logging::toggle(millis(), ui::Gnss::utcEpoch(millis()));
        prefs::setLogging(logging::wantsActive());
        nav.logToggleRequested = false;
    }

    if (nav.enrollRequested) {
        nav.enrollRequested = false;
        enroll::open(millis());
    }
    if (nav.resetNetRequested) {
        nav.resetNetRequested = false;
        enroll::reset(millis());
    }

    if (nav.resetRequested) {
        nav.resetRequested = false;
        logging::resetTables(logwire::resetAll, millis(), ui::Gnss::utcEpoch(millis()));
        alertlog::clear();
        Serial.println("counters cleared");
    }

    // Alerts: take the strongest recent hit from either radio that clears
    // the confidence floor. The card fades rather than latching, so one on
    // the glass means the thing is still in range.
    pollConsole();

    constexpr uint32_t ALERT_MS = 6000;
    s.alertActive = false;
    if (testAlert(s, millis())) {
        // a test alert is up; leave it alone
    } else if (set.alertMin != ui::CONF_OFF) {
        ui::AlertRec w, b, best;
        bool have = false;
        if (cap::lastAlert(w) && w.conf >= set.alertMin &&
            (millis() - w.atMs) < ALERT_MS) { best = w; have = true; }
        if (blescan::lastAlert(b) && b.conf >= set.alertMin &&
            (millis() - b.atMs) < ALERT_MS &&
            (!have || b.atMs > best.atMs))  { best = b; have = true; }
        if (have) {
            s.alertActive = true;
            s.alertThreat = best.threat;
            s.alertConf   = best.conf;
            for (int k = 0; k < 19 && best.why[k]; k++) s.alertWhy[k] = best.why[k];
            for (int k = 0; k < ui::SSID_MAX; k++)      s.alertName[k] = best.name[k];
        }
    }

    // Age the alert so the card can animate. The clock restarts only when
    // the alert is actually a different one -- a device that keeps being
    // seen holds its card steady rather than re-striking every frame.
    {
        static uint8_t  lastThreat = 0;
        static char     lastName[ui::SSID_MAX] = {0};
        static uint32_t startedAt = 0;
        if (s.alertActive) {
            if (s.alertThreat != lastThreat || strncmp(s.alertName, lastName, sizeof lastName)) {
                startedAt  = millis();
                lastThreat = s.alertThreat;
                snprintf(lastName, sizeof lastName, "%s", s.alertName);

                // Wake the panel. Dim-on-idle suspends the whole UI, which
                // meant an alert raised while the screen was dark was drawn
                // nowhere and seen by nobody -- the feature silently undid
                // the one thing the device exists to do.
                //
                // Only a *new* alert wakes it: the same camera re-detected
                // every few seconds would otherwise hold the screen on for
                // as long as you were parked beside it.
                wakeScreen = true;
            }
            const uint32_t a = millis() - startedAt;
            s.alertAge = (uint16_t)(a > 60000 ? 60000 : a);
        } else {
            lastThreat = 0;
            lastName[0] = 0;
        }
    }

    if (s.alertActive && testStep < 0) {
        ui::AlertRec rec;
        rec.threat = s.alertThreat;
        rec.conf   = s.alertConf;
        for (int k = 0; k < ui::SSID_MAX; k++) rec.name[k] = s.alertName[k];
        const ui::Gnss::Fix& fx = ui::Gnss::fix();
        alertlog::push(rec, millis(), ui::Gnss::utcEpoch(millis()),
                       ui::Gnss::fresh(millis()), fx.lat7, fx.lon7);
    }

    s.set    = set;
    s.screen = nav.screen;
    s.filter = nav.filter;
    s.scroll = nav.scroll;
    if (nav.screen == ui::SCR_CODEX) {
        if (nav.filter == ui::F_BLE) blescan::fillSights(s, nav.scroll);
        else                         cap::fillSights(s, nav.scroll, nav.filter == ui::F_5G);
    } else if (nav.screen == ui::SCR_ECHOES) {
        cap::fillEchoes(s, nav.scroll, millis());
    } else if (nav.screen == ui::SCR_OMENS) {
        alertlog::fill(s, nav.scroll, millis());
        s.omenScroll = nav.scroll;
    } else if (nav.screen == ui::SCR_DOMAIN) {
        const ui::Gnss::Fix& fx = ui::Gnss::fix();
        cap::fillPlots(s, ui::RANGES[nav.rangeIdx % ui::RANGE_N],
                       fx.lat7, fx.lon7, ui::Gnss::fresh(millis()));
        if (ui::Gnss::fresh(millis())) {
            const float latDeg = (float)fx.lat7 / 1e7f;
            const float mLat = 111320.0f;
            const float mLon = 111320.0f * cosf(latDeg * 3.14159265f / 180.0f);
            const float pxPerM = (float)ui::MAP_R / (float)s.rangeM;
            int n = 0;
            for (int i = 0; i < gpstrack::count() && n < ui::MAX_TRACK; i++) {
                int32_t la, lo;
                if (!gpstrack::sample(i, la, lo)) continue;
                const int px = (int)lrintf(((float)(lo - fx.lon7) / 1e7f) * mLon * pxPerM);
                const int py = (int)lrintf(-((float)(la - fx.lat7) / 1e7f) * mLat * pxPerM);
                if (px * px + py * py > ui::MAP_R * ui::MAP_R) continue;
                s.track[n].dx = (int16_t)px;
                s.track[n].dy = (int16_t)py;
                n++;
            }
            s.nTrack = (uint8_t)n;
        }
    } else if (nav.screen == ui::SCR_DETAIL) {
        if (nav.filter == ui::F_BLE) blescan::fillDetail(s, nav.selected, millis());
        else cap::fillDetail(s, nav.selected, nav.filter == ui::F_5G, millis());
    }

    s.touchDown = tpDown;
    s.touchX    = tp.x;
    s.touchY    = tp.y;

    // Report every press once, with the raw values -- the mapping below is a
    // first guess from the board's calibration constants and needs a finger
    // to confirm the axis order and inversion.
    static bool wasDown = false;
    if (tp.down && !wasDown)
        Serial.printf("TOUCH raw x=%4u y=%4u z=%4u  ->  canvas %3d,%3d\n",
                      tp.rawX, tp.rawY, tp.z, tp.x, tp.y);
    wasDown = tp.down;

    const bool live = ui::Gnss::fresh(millis());
    s.gpsFix = live;
    s.sats   = live ? ui::Gnss::fix().used : 0;

    // Feed the position history that dates node reports.
    gpstrack::push(millis(), live, ui::Gnss::fix().lat7, ui::Gnss::fix().lon7);

    // One CSV row per interval to /perf.csv -- here, before the dark-screen
    // branch below, so intervals spent with the screen off are logged as well.
    perflog::tick(millis(), s, screenOff);

    // Nothing to look at: skip draw and blit entirely and give the time to
    // the radio. The next wake forces a full repaint, because every screen's
    // dirty tracking has been standing still.
    if (screenOff) {
        static bool     said = false;
        static uint32_t lastLog = 0;
        if (!said) { said = true; Serial.println("screen: dark, UI suspended"); }

        // Yield. Without the draw and blit this loop runs about 5700 times a
        // second, which is 80x more often than anything here needs and
        // starves the WiFi task of scheduling. 5ms is still 200Hz of touch
        // polling -- far faster than a finger.
        delay(5);

        // ANIM: a dot breathing at <= 2 fps, one 11x11 blit per frame. The
        // whole canvas is blanked and blitted once on entry; after that
        // only the dot's own rectangle is touched.
        {
            static bool     animActive = false;
            static uint32_t animStart  = 0;
            static uint32_t animLast   = 0xFFFFFFFFu;
            if (animOn) {
                if (!animActive) {
                    animActive = true;
                    animDirtied = true;
                    animStart  = millis();
                    animLast   = 0xFFFFFFFFu;
                    canvas.clear(pal::VOID);
                    const ui::Rect all{0, 0, ui::W, ui::H};
                    blitRect(canvas.px, all);
                }
                const uint32_t af = idlemode::animFrame(millis() - animStart);
                if (af != animLast) {
                    animLast = af;
                    const idlemode::Rect r = idlemode::drawFrame(canvas, af, pal::VOID, pal::WITCH_HI);
                    const ui::Rect ur{r.x, r.y, r.w, r.h};
                    blitRect(canvas.px, ur);
                }
            } else {
                animActive = false;
            }
        }

        // Log on a clock, not a frame count: frame counts mean something
        // different on each side of this branch.
        if (millis() - lastLog >= 10000) {
            lastLog = millis();
            logf("[dark] wifi %4u  hits %u  deauth %u  heap %u\n",
                 (unsigned)cap::uniques(), (unsigned)cap::frames(),
                 (unsigned)cap::deauths(), (unsigned)ESP.getFreeHeap());
        }
        frame++;
        return;
    }
    { static bool said = false; (void)said; }

    // Publish the logging session for the footer, after any toggle above.
    s.logState = (uint8_t)logging::state();
    memset(s.logName, 0, sizeof s.logName);
    strncpy(s.logName, logging::fileName(), sizeof s.logName - 1);

    // The palette and the dial geometry are the theme. Apply it before
    // anything draws, and repaint everything when it changes -- the dirty
    // regions of the old theme say nothing about the new one.
    //
    // This runs here, in the task that draws, and nowhere else: the palette
    // globals are plain uint16_t with no atomics, so setTheme must never be
    // called from a touch handler or another thread mid-draw. navTouch only
    // edits `set`; the change takes effect here, at the top of the next draw.
    static uint8_t lastTheme = 0xFF;
    bool themeChanged = false;
    if (lastTheme != s.set.theme) {
        pal::setTheme(s.set.theme);
        lastTheme = s.set.theme;
        themeChanged = true;
    }
    if      (nav.screen == ui::SCR_CODEX)  ui::drawCodex(canvas, s, &dirty);
    else if (nav.screen == ui::SCR_DETAIL) ui::drawDetail(canvas, s, &dirty);
    else if (nav.screen == ui::SCR_ECHOES) ui::drawEchoes(canvas, s, &dirty);
    else if (nav.screen == ui::SCR_GRIMOIRE) ui::drawGrimoire(canvas, s, &dirty);
    else if (nav.screen == ui::SCR_OMENS)  ui::drawOmens(canvas, s, &dirty);
    else if (nav.screen == ui::SCR_DOMAIN) ui::drawDomain(canvas, s, &dirty);
    else if (nav.screen == ui::SCR_MESH)   ui::drawMesh(canvas, s, &dirty);
    else if (nav.screen == ui::SCR_ATTUNE) ui::drawAttune(canvas, s, &dirty);
    else if (nav.screen == ui::SCR_ASPECT) ui::drawAspect(canvas, s, &dirty);
    else                                   ui::drawScan(canvas, s, &dirty);

    // A screen change invalidates every region the outgoing screen reported.
    // The incoming one has its own first-frame logic, but it has already run
    // for this frame, so force the repaint here.
    static bool justWoke = true;
    // Load-bearing coupling: every screen keeps its own `static State prev` /
    // `static bool first` for dirty tracking, and those mean nothing across a
    // theme change. That is safe only because the theme can change solely
    // while ASPECT is on screen, so the user always leaves via a screen
    // switch, and `switched` forces this full blit, which clears the other
    // screens' stale dirty state. A future navigation path that changes the
    // theme or leaves ASPECT without setting `switched` will show leftover
    // pixels from the old theme.
    if (switched || justWoke || themeChanged || animDirtied) {
        animDirtied = false;
        dirty.clear();
        dirty.add(0, 0, ui::W, ui::H);
        justWoke = false;
    }
    if (screenOff) justWoke = true;
    const uint32_t tDraw = millis();

    uint32_t pushed = 0;
    for (int i = 0; i < dirty.n; i++) {
        blitRect(canvas.px, dirty.r[i]);
        pushed += (uint32_t)dirty.r[i].w * dirty.r[i].h;
    }
    const uint32_t tBlit = millis();
    perflog::frame(tDraw - t0, tBlit - tDraw);

    if ((frame % 30) == 0) {
        logf("f%-5u [%s] draw %2u blit %2u ms | wifi %4u/%-4u (2.4 %u, 5 %u) "
             "%u fr ch%-3d %s | ble %4u/%-6u | gps %s %2d sv | touch %s | "
             "alert %s %s | mesh %dn %ur %u%%lost | hits %u rem %u | heap %u\n",
             (unsigned)frame,
             nav.screen == ui::SCR_CODEX   ? "codex"
             : nav.screen == ui::SCR_DETAIL ? "detal"
             : nav.screen == ui::SCR_ECHOES ? "echos"
             : nav.screen == ui::SCR_GRIMOIRE ? "grim "
             : nav.screen == ui::SCR_OMENS  ? "omens"
             : nav.screen == ui::SCR_DOMAIN ? "domn "
             : nav.screen == ui::SCR_MESH   ? "mesh "
             : nav.screen == ui::SCR_ATTUNE ? "tune "
            : nav.screen == ui::SCR_ASPECT ? "aspct" : "scan ",
             (unsigned)(tDraw - t0), (unsigned)(tBlit - tDraw),
             (unsigned)cap::uniques(), (unsigned)cap::tableCap(),
             (unsigned)s.nets24, (unsigned)s.nets5,
             (unsigned)cap::frames(), (int)s.chan,
             s.on5 ? "5G " : "2.4",
             (unsigned)blescan::uniques(), (unsigned)blescan::adverts(),
             live ? "FIX" : "---", (int)s.sats,
             tp.down ? "DOWN" : "up  ",
             s.alertActive ? ui::threatName(s.alertThreat) : "-",
             s.alertActive ? ui::confName(s.alertConf) : "",
             meshcore::count(), (unsigned)meshcore::reportsRx(),
             (unsigned)meshcore::lossPct(),
             (unsigned)cap::frames(),
             (unsigned)(cap::frames() - cap::localFrames()),
             (unsigned)ESP.getFreeHeap());
    }
    frame++;
}
