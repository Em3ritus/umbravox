// Integer radar ring generator: the same antialiased one-pixel circle as the baked
// tables in radar_geometry.h (tools/gen_radar_geometry.py), for ANY radius, so a
// theme layout can size the radar freely.
//
// Exactness: the script's tests are on dyadic rationals (pixel offsets in 1/16 px,
// the band in 1/4 px), so scaling by 16 and 4 turns every comparison into an
// integer one with the same result: for every radius that has a table, the
// generator's points are identical in value and order (sim/test_ring_gen.cpp).
// Cost: about 64 multiply-adds per candidate pixel in a ~3 px band, once per new
// radius (then cached). No floating point: the C5 has no FPU.
#pragma once
#include "radar_geometry.h"
#include <cstdint>

namespace radargeometry {

// An upper bound on generateRing's count: the band |d - r| <= 1.25 holds fewer than
// 2*pi*(2r)*1.25 + slack pixels; 16*(r+2) is comfortably above that for r >= 1.
inline int ringCapacity(int r) { return 16 * (r + 2); }

// Points in row-major order (y, then x), as the baked tables. Returns the count, or
// -1 when cap is too small.
inline int generateRing(int r, Point* out, int cap) {
    if (r < 1 || r > 120) return -1;
    const int32_t inner = (4 * r - 5) * (4 * r - 5), outer = (4 * r + 5) * (4 * r + 5);   // (r -+ 1.25)^2, x16
    const int32_t lo = (16 * r - 8) * (16 * r - 8), hi = (16 * r + 8) * (16 * r + 8);     // (r -+ 0.5)^2, x256
    int n = 0;
    for (int y = -r - 1; y <= r + 1; y++) {
        for (int x = -r - 1; x <= r + 1; x++) {
            const int32_t d16 = 16 * (x * x + y * y);
            if (d16 < inner || d16 > outer) continue;       // |hypot(x,y) - r| > 1.25
            int hits = 0;
            for (int sy = 0; sy < 8; sy++) {
                const int32_t Y = 16 * y + 2 * sy + 1 - 8;
                const int32_t Y2 = Y * Y;
                for (int sx = 0; sx < 8; sx++) {
                    const int32_t X = 16 * x + 2 * sx + 1 - 8;
                    const int32_t d = X * X + Y2;
                    hits += (d >= lo && d <= hi);
                }
            }
            const int alpha = (hits * 16 + 32) / 64;
            if (!alpha) continue;
            if (n >= cap) return -1;
            out[n].x = (int8_t)x; out[n].y = (int8_t)y; out[n].coverage = (uint8_t)alpha;
            n++;
        }
    }
    return n;
}

// The cache for radii without a table: 3 slots, least recently used evicted.
// Storage: the host uses static memory; the device hands in a PSRAM block at boot
// (setRingStorage) so the cache never sits in scarce internal RAM.
namespace detail {
constexpr int kSlots = 3;
constexpr int kSlotCap = 16 * (102 + 2);   // ringCapacity(102): the largest radar a layout allows
struct Cache {
    Point* buf = nullptr; int cap = 0;
    int radius[kSlots] = {0, 0, 0}, count[kSlots] = {0, 0, 0};
    uint32_t used[kSlots] = {0, 0, 0}, tick = 0;
};
inline Cache& cache() {
#if defined(ARDUINO)
    static Cache c;   // the device must call setRingStorage before the first untabled radius
#else
    static Point host[kSlots * kSlotCap];
    static Cache c{host, kSlots * kSlotCap};
#endif
    return c;
}
}  // namespace detail

// How many times a ring was drawn with the slow trig fallback (scan_render.h). Tests assert it stays 0.
inline int& fallbackCalls() { static int n = 0; return n; }

inline void setRingStorage(Point* buf, int cap) {
    detail::Cache& c = detail::cache();
    c.buf = buf; c.cap = cap;
    for (int i = 0; i < detail::kSlots; i++) { c.radius[i] = 0; c.count[i] = 0; c.used[i] = 0; }
}

// The baked table when one exists; else the cached ring (generated on a miss);
// nullptr only when there is no storage or the radius is out of range.
inline const Point* ringFor(int r, int& count) {
    const Point* t = ring(r, count);
    if (t) return t;
    detail::Cache& c = detail::cache();
    const int slotCap = c.cap / detail::kSlots;
    count = 0;
    if (!c.buf || slotCap <= 0 || r < 1 || ringCapacity(r) > slotCap) return nullptr;
    c.tick++;
    int victim = 0;
    for (int i = 0; i < detail::kSlots; i++) {
        if (c.radius[i] == r && c.count[i] > 0) { c.used[i] = c.tick; count = c.count[i]; return c.buf + i * slotCap; }
        if (c.used[i] < c.used[victim]) victim = i;
    }
    Point* dst = c.buf + victim * slotCap;
    const int n = generateRing(r, dst, slotCap);
    if (n <= 0) { c.radius[victim] = 0; c.count[victim] = 0; return nullptr; }
    c.radius[victim] = r; c.count[victim] = n; c.used[victim] = c.tick;
    count = n;
    return dst;
}

}  // namespace radargeometry
