// Host test for the DOMAIN map projection (device/plotproj.h): the integer
// bounding-box cull, agreement with the old per-node projection, and the
// refresh cache rule. Also times the old and new loops over a 20,000-entry
// synthetic table -- host numbers, not device numbers (the C5 has no FPU and
// reads the table from PSRAM).
#include "../device/plotproj.h"
#include <chrono>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <vector>

static int fails = 0;
static void check(const char* what, bool ok) {
    printf(ok ? "  ok   %s\n" : "  FAIL %s\n", what);
    if (!ok) fails++;
}

constexpr int R = 42;          // ui::MAP_R

// The loop fillPlots ran before this change, per node.
static bool oldProject(int rangeM, int32_t myLat7, int32_t myLon7, int32_t lat7, int32_t lon7,
                       int& px, int& py) {
    const float latDeg = (float)myLat7 / 1e7f;
    const float mPerDegLat = 111320.0f;
    const float mPerDegLon = 111320.0f * cosf(latDeg * 3.14159265f / 180.0f);
    const float pxPerM = (float)R / (float)rangeM;
    const float dE = ((float)(lon7 - myLon7) / 1e7f) * mPerDegLon;
    const float dN = ((float)(lat7 - myLat7) / 1e7f) * mPerDegLat;
    px = (int)lrintf(dE * pxPerM);
    py = (int)lrintf(-dN * pxPerM);
    return px * px + py * py <= R * R;
}

struct Fake { uint8_t pad[80]; int32_t lat7, lon7; bool located; };   // ~92 B, like Node

int main() {
    using namespace plotproj;
    const int32_t LAT = 421017660, LON = -710195598;      // the field drive

    printf("agreement with the old projection (random points, every range):\n");
    {
        srand(7);
        long n = 0, inOld = 0, decisionFlips = 0, offByMore = 0;
        for (int range : {100, 250, 500, 1000, 2500}) {
            const Proj p = make(range, LAT, LON, R);
            for (int k = 0; k < 200000; k++) {
                // Points within ~3x the range, so both sides of the edge occur.
                const int32_t span = (int32_t)(range * 3 * 1e7 / 111320.0);
                const int32_t la = LAT + (rand() % (2 * span + 1)) - span;
                const int32_t lo = LON + (int32_t)((rand() % (2 * span + 1)) - span) * 4 / 3;
                int ox, oy, nx, ny;
                const bool o = oldProject(range, LAT, LON, la, lo, ox, oy);
                const bool w = project(p, la, lo, nx, ny);
                n++;
                if (o) inOld++;
                if (o != w) decisionFlips++;
                else if (o && (abs(ox - nx) > 1 || abs(oy - ny) > 1)) offByMore++;
            }
        }
        printf("     (%ld points, %ld inside, %ld edge flips)\n", n, inOld, decisionFlips);
        check("inside points land within 1 px of the old position", offByMore == 0);
        check("inside/beyond agrees except on the rounding edge (< 0.1%)",
              decisionFlips * 1000 < n);
    }

    printf("the bounding box never culls a point the circle would keep:\n");
    {
        srand(11);
        bool safe = true;
        for (int range : {100, 250, 500, 1000, 2500}) {
            const Proj p = make(range, LAT, LON, R);
            for (int k = 0; k < 200000; k++) {
                const int32_t la = LAT + (rand() % 400001) - 200000;
                const int32_t lo = LON + (rand() % 400001) - 200000;
                int px, py;
                const bool kept = project(p, la, lo, px, py);
                // Exact float check with no cull, the same formula.
                const float fx = (float)(lo - LON) * p.kE, fy = -(float)(la - LAT) * p.kN;
                const int ex = (int)lrintf(fx), ey = (int)lrintf(fy);
                if (!kept && ex * ex + ey * ey <= R * R) safe = false;
            }
        }
        check("no false culls", safe);
        const Proj p = make(250, LAT, LON, R);
        int px, py;
        check("far away (other side of the planet) is beyond, no overflow",
              !project(p, -LAT, LON + 1800000000, px, py));
        check("the centre is inside at (0,0)", project(p, LAT, LON, px, py) && px == 0 && py == 0);
        check("north is up (-y)", project(p, LAT + 10000, LON, px, py) && py < 0 && px == 0);
    }

    printf("cache refresh rule:\n");
    {
        Cache c;
        check("empty cache is stale", stale(c, 0, 250, true, 0));
        remember(c, 1000, 250, true, 3);
        check("fresh within REFRESH_MS", !stale(c, 1000 + REFRESH_MS - 1, 250, true, 3));
        check("stale after REFRESH_MS (we moved)", stale(c, 1000 + REFRESH_MS, 250, true, 3));
        check("REFRESH_MS is 500", REFRESH_MS == 500);
        check("zoom invalidates at once", stale(c, 1001, 500, true, 3));
        check("losing the fix invalidates", stale(c, 1001, 250, false, 3));
        check("a table clear (rollover / reset) invalidates", stale(c, 1001, 250, true, 4));
        check("millis() wrap", [] { Cache w; remember(w, 0xFFFFFF00u, 250, true, 0);
                                    return !stale(w, 0xFFFFFF00u + 100, 250, true, 0) &&
                                           stale(w, 0xFFFFFF00u + REFRESH_MS, 250, true, 0); }());
    }

    printf("host timing, 20000 located APs (host, NOT device numbers):\n");
    {
        std::vector<Fake> t(20000);
        srand(3);
        for (auto& f : t) {
            f.located = true;
            f.lat7 = LAT + (rand() % 2000001) - 1000000;   // ~ +-11 km
            f.lon7 = LON + (rand() % 2000001) - 1000000;
        }
        volatile int sink = 0;
        const int REPS = 50;
        auto t0 = std::chrono::steady_clock::now();
        for (int r = 0; r < REPS; r++) {
            int inside = 0;
            for (const auto& f : t) {
                if (!f.located) continue;
                int px, py;
                if (oldProject(250, LAT, LON, f.lat7, f.lon7, px, py)) inside++;
            }
            sink = sink + inside;
        }
        auto t1 = std::chrono::steady_clock::now();
        for (int r = 0; r < REPS; r++) {
            int inside = 0;
            const Proj p = make(250, LAT, LON, R);
            for (const auto& f : t) {
                if (!f.located) continue;
                int px, py;
                if (project(p, f.lat7, f.lon7, px, py)) inside++;
            }
            sink = sink + inside;
        }
        auto t2 = std::chrono::steady_clock::now();
        const double oldMs = std::chrono::duration<double, std::milli>(t1 - t0).count() / REPS;
        const double newMs = std::chrono::duration<double, std::milli>(t2 - t1).count() / REPS;
        printf("     old %.3f ms/frame, new %.3f ms/pass (and a pass runs at most every %u ms)\n",
               oldMs, newMs, (unsigned)REFRESH_MS);
        check("the new loop is not slower", newMs <= oldMs * 1.1);
        (void)sink;
    }

    printf(fails ? "\n%d FAILED\n" : "\nall passed\n", fails);
    return fails ? 1 : 0;
}
