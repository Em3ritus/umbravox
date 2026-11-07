#include "alertlog.h"
#include <cstring>
#include <cstdio>


namespace alertlog {
namespace {

// Long enough that sitting beside something does not fill the log, short
// enough that passing it twice on a loop records twice.
constexpr uint32_t COALESCE_MS = 60000;

struct Entry {
    uint8_t  threat, conf;
    char     name[ui::SSID_MAX];
    uint32_t atMs, epoch;
    int32_t  lat7, lon7;
    bool     located;
    bool     used;
    bool     test;
    uint32_t id;
    uint8_t mac[6];
    ui::AlertAddress address;
    char why[32];
};

Entry ring[DEPTH];
int   head = 0;          // next slot to write
uint16_t total = 0;
uint32_t nextId=0;
uint32_t newId(){if(++nextId==0)++nextId;return nextId;}

bool sameAs(const Entry& e, const ui::AlertRec& a) {
    if (e.test || e.threat != a.threat || e.address!=a.address) return false;
    if(a.address!=ui::AlertAddress::Missing && memcmp(e.mac,a.mac,6))return false;
    for (int i = 0; i < ui::SSID_MAX; i++) {
        if (e.name[i] != a.name[i]) return false;
        if (!e.name[i]) break;
    }
    return true;
}

} // namespace

void push(const ui::AlertRec& a, uint32_t nowMs, uint32_t epoch,
          bool located, int32_t lat7, int32_t lon7) {
    if (a.threat == ui::TH_NONE) return;

    const int last = (head + DEPTH - 1) % DEPTH;
    if (ring[last].used && sameAs(ring[last], a) &&
        (nowMs - ring[last].atMs) < COALESCE_MS) {
        ring[last].atMs = nowMs;                  // still here; just refresh
        if (a.conf >= ring[last].conf) {
            ring[last].conf = a.conf;
            snprintf(ring[last].why,sizeof ring[last].why,"%s",a.why?a.why:"");
        }
        return;
    }

    Entry& e = ring[head];
    e.threat = a.threat;
    e.conf   = a.conf;
    for (int i = 0; i < ui::SSID_MAX; i++) e.name[i] = a.name[i];
    e.atMs    = nowMs;
    e.epoch   = epoch;
    e.lat7    = lat7;
    e.lon7    = lon7;
    e.located = located;
    e.used    = true;
    e.test    = false;e.id=newId();e.address=a.address;memcpy(e.mac,a.mac,6);
    snprintf(e.why,sizeof e.why,"%s",a.why?a.why:"");
    head = (head + 1) % DEPTH;
    if (total < 0xFFFF) total++;
}

void pushTest(uint32_t nowMs) {
    // One synthetic history entry, no radio match, location or external export.
    for(int k=1;k<=DEPTH;k++){
        const int idx=(head-k+DEPTH)%DEPTH;
        if(!ring[idx].used || !ring[idx].test)continue;
        Entry test=ring[idx];test.atMs=nowMs;
        for(int j=k;j>1;j--)ring[(head-j+DEPTH)%DEPTH]=ring[(head-j+1+DEPTH)%DEPTH];
        ring[(head-1+DEPTH)%DEPTH]=test;return;
    }
    Entry& e=ring[head];e=Entry{};e.used=true;e.test=true;e.atMs=nowMs;e.id=newId();
    const char* label="SIMULATED - NO RF";
    for(int i=0;label[i]&&i<ui::SSID_MAX-1;i++)e.name[i]=label[i];
    head=(head+1)%DEPTH;
}

static void project(const Entry& e,ui::Omen& o,uint32_t nowMs){
    o=ui::Omen{};o.threat=e.threat;o.conf=e.conf;memcpy(o.name,e.name,sizeof o.name);
    o.epoch=e.epoch;o.agoS=(nowMs-e.atMs)/1000;o.located=e.located;o.test=e.test;
    o.id=e.id;o.address=e.address;memcpy(o.mac,e.mac,6);memcpy(o.why,e.why,sizeof o.why);
}
bool find(uint32_t id,uint32_t nowMs,ui::Omen& out){
    if(id)for(const auto& e:ring)if(e.used&&e.id==id){project(e,out,nowMs);return true;}
    return false;
}

void fill(ui::State& s, int offset, uint32_t nowMs) {
    s.omenTotal = count();
    int n = 0, skipped = 0;
    // Newest first.
    for (int k = 1; k <= DEPTH && n < ui::MAX_OMEN_ROWS; k++) {
        const Entry& e = ring[(head - k + DEPTH * 2) % DEPTH];
        if (!e.used) continue;
        if (skipped < offset) { skipped++; continue; }
        ui::Omen& o = s.omens[n++];
        project(e,o,nowMs);
    }
    s.nOmens = (uint8_t)n;
}

uint16_t count() {
    uint16_t c = 0;
    for (int i = 0; i < DEPTH; i++) if (ring[i].used) c++;
    return c;
}

void clear() {
    for (int i = 0; i < DEPTH; i++) ring[i].used = false;
    head = 0; total = 0;
}

} // namespace alertlog
