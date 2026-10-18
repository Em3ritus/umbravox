#include "gpstrack.h"

namespace gpstrack {
namespace {

struct Sample { uint32_t atMs; int32_t lat7, lon7; bool valid; };
Sample ring[DEPTH];
int    head = 0;
bool   seeded = false;

} // namespace

void push(uint32_t nowMs, bool valid, int32_t lat7, int32_t lon7) {
    if (!seeded) {
        for (int i = 0; i < DEPTH; i++) ring[i] = Sample{0, 0, 0, false};
        seeded = true;
    }
    // One sample a second is plenty: GPS updates at 1Hz and a vehicle does
    // not change position meaningfully faster than the receiver reports it.
    if (ring[head].atMs && (nowMs - ring[head].atMs) < 1000) return;
    head = (head + 1) % DEPTH;
    ring[head] = Sample{nowMs, lat7, lon7, valid};
}

bool at(uint32_t nowMs, uint32_t agoMs, int32_t& lat7, int32_t& lon7) {
    if (!seeded) return false;
    const uint32_t want = nowMs - agoMs;

    int best = -1;
    uint32_t bestErr = 0xFFFFFFFF;
    for (int i = 0; i < DEPTH; i++) {
        const Sample& s = ring[i];
        if (!s.valid || !s.atMs) continue;
        const uint32_t err = s.atMs > want ? s.atMs - want : want - s.atMs;
        if (err < bestErr) { bestErr = err; best = i; }
    }
    // A sample more than a few seconds from the moment we want is not an
    // answer to the question that was asked.
    if (best < 0 || bestErr > 4000) return false;
    lat7 = ring[best].lat7;
    lon7 = ring[best].lon7;
    return true;
}

bool sample(int i, int32_t& lat7, int32_t& lon7) {
    if (!seeded || i < 0 || i >= DEPTH) return false;
    const Sample& s = ring[(head + 1 + i) % DEPTH];   // oldest first
    if (!s.valid || !s.atMs) return false;
    lat7 = s.lat7; lon7 = s.lon7;
    return true;
}

int count() { return seeded ? DEPTH : 0; }

} // namespace gpstrack
