#include "alertlog.h"
#include <Arduino.h>

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
};

Entry ring[DEPTH];
int   head = 0;          // next slot to write
uint16_t total = 0;

bool sameAs(const Entry& e, const ui::AlertRec& a) {
    if (e.threat != a.threat) return false;
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
        if (a.conf > ring[last].conf) ring[last].conf = a.conf;
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
    head = (head + 1) % DEPTH;
    if (total < 0xFFFF) total++;
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
        o.threat = e.threat;
        o.conf   = e.conf;
        for (int i = 0; i < ui::SSID_MAX; i++) o.name[i] = e.name[i];
        o.epoch   = e.epoch;
        o.agoS    = (nowMs - e.atMs) / 1000;
        o.located = e.located;
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
