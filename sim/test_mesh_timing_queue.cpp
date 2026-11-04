// Host test for the paced timing-send queue (device/mesh_timing_queue.h): the Core
// sends one Config per node, spaced out, so the small ESP-NOW transmit queue is
// never filled in one burst.
#include "../device/mesh_timing_queue.h"
#include <cassert>
#include <cstring>
#include <cstdio>
int main() {
    const uint8_t a[6] = {2,0,0,0,0,1}, b[6] = {2,0,0,0,0,2}, c[6] = {2,0,0,0,0,3};
    uint8_t out[6];
    meshtimingq::Queue q;
    assert(!q.due(1000, out));                               // empty
    assert(q.push(a) && q.push(b) && q.push(c));
    assert(q.push(b) && q.size() == 3);                      // duplicate not queued twice
    assert(q.due(1000, out) && !memcmp(out, a, 6));          // first goes at once
    assert(!q.due(1000, out));                               // never two in the same tick
    assert(!q.due(1000 + meshtimingq::GAP_MS - 1, out));     // not before the gap
    assert(q.due(1000 + meshtimingq::GAP_MS, out) && !memcmp(out, b, 6));
    assert(q.due(1000 + 2 * meshtimingq::GAP_MS, out) && !memcmp(out, c, 6));
    assert(!q.due(5000, out) && q.size() == 0);
    // Signed arithmetic: spacing holds across the 32-bit wrap and for a clock that
    // reads slightly behind the last send.
    meshtimingq::Queue w;
    w.push(a); w.push(b);
    assert(w.due(0xFFFFFFF0u, out));
    assert(!w.due(0xFFFFFFF0u - 1, out));                    // clock behind: not due
    assert(!w.due(0xFFFFFFF0u + 5, out));                    // 5 ms later: not yet
    assert(w.due(4, out) && !memcmp(out, b, 6));             // 20 ms later across the wrap
    // Bounded: a full queue refuses, it does not overwrite or grow.
    meshtimingq::Queue f;
    for (int i = 0; i < meshtimingq::CAP; i++) { uint8_t m[6] = {2,0,0,0,(uint8_t)(i >> 8),(uint8_t)(i + 1)}; assert(f.push(m)); }
    const uint8_t extra[6] = {9,9,9,9,9,9};
    assert(!f.push(extra) && f.size() == meshtimingq::CAP);
    puts("mesh timing queue: dedupe, spacing, wrap-safe, bounded PASS");
}
