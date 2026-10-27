// The DOMAIN map's projection of located APs, made cheap enough for a 20,000-
// entry table on an FPU-less C5. Pure; host-tested in sim/test_plotproj.cpp.
//
// Three savings over the old per-node loop:
//  - the scale constants are worked out once per pass, so no divide remains
//    per node (each soft-float divide is ~100+ cycles on the C5);
//  - an integer bounding box on lat7/lon7 culls almost every node before any
//    float work (at the 250 m range nearly the whole table is "beyond");
//  - the pass runs at most every REFRESH_MS, and the frames between reuse it.
#pragma once
#include <math.h>
#include <stdint.h>

namespace plotproj {

struct Proj {
    int32_t lat0 = 0, lon0 = 0;   // the map centre (us)
    float   kE = 0, kN = 0;       // px per lat7/lon7 unit, east and north
    int64_t dLatMax = 0, dLonMax = 0;   // bounding box, in lat7/lon7 units
    int     r = 0;                // map radius, px
};

inline Proj make(int rangeM, int32_t lat7, int32_t lon7, int mapR) {
    Proj p;
    p.lat0 = lat7; p.lon0 = lon7; p.r = mapR;
    const float latDeg = (float)lat7 / 1e7f;
    const float mPerDegLat = 111320.0f;
    const float mPerDegLon = 111320.0f * cosf(latDeg * 3.14159265f / 180.0f);
    const float pxPerM = (float)mapR / (float)rangeM;
    p.kE = mPerDegLon * pxPerM / 1e7f;
    p.kN = mPerDegLat * pxPerM / 1e7f;
    // A point whose rounded pixel is inside the circle has |px|, |py| <= r,
    // so |d| * k < r + 0.5. One pixel of margin keeps rounding on the safe
    // side; near a pole kE -> 0 and the box simply opens up.
    const float lim = 4.0e9f;
    const float bN = (float)(mapR + 1) / p.kN, bE = p.kE > 0 ? (float)(mapR + 1) / p.kE : lim;
    p.dLatMax = (int64_t)(bN < lim ? bN : lim);
    p.dLonMax = (int64_t)(bE < lim ? bE : lim);
    return p;
}

// Pixel offset of a point from the centre (+y south). False if it falls
// outside the map circle (the caller counts it as beyond).
inline bool project(const Proj& p, int32_t lat7, int32_t lon7, int& px, int& py) {
    const int64_t dLat = (int64_t)lat7 - p.lat0, dLon = (int64_t)lon7 - p.lon0;
    if (dLat > p.dLatMax || -dLat > p.dLatMax || dLon > p.dLonMax || -dLon > p.dLonMax)
        return false;
    px = (int)lrintf((float)dLon * p.kE);
    py = (int)lrintf(-(float)dLat * p.kN);
    return px * px + py * py <= p.r * p.r;
}

// When to redo the pass. Moving is covered by the refresh period; a zoom, a
// change of fix, or a cleared table (rollover / RESET bump `gen`) is at once.
constexpr uint32_t REFRESH_MS = 500;

struct Cache {
    bool     valid = false;
    uint32_t atMs = 0;
    int      rangeM = 0;
    bool     fix = false;
    uint32_t gen = 0;
};

inline bool stale(const Cache& c, uint32_t nowMs, int rangeM, bool fix, uint32_t gen) {
    return !c.valid || c.rangeM != rangeM || c.fix != fix || c.gen != gen ||
           nowMs - c.atMs >= REFRESH_MS;
}

inline void remember(Cache& c, uint32_t nowMs, int rangeM, bool fix, uint32_t gen) {
    c.valid = true; c.atMs = nowMs; c.rangeM = rangeM; c.fix = fix; c.gen = gen;
}

} // namespace plotproj
