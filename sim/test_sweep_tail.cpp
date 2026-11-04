// Smooth sweep: a continuous head angle and a 90-degree tail that fades with
// angular distance behind the head. Pure geometry, no framebuffer.
#include "../ui/scan_render.h"
#include <cassert>
#include <cmath>
#include <cstdio>
using namespace scanr;

// Level of the pixel at `deg` degrees behind the head (counter-clockwise on
// screen) and radius 40, with the head pointing straight up.
static int behind(const SweepDir& d, double deg, int levels) {
    const double a = (-90.0 - deg) * M_PI / 180.0;
    return trailLevel(d, (int)lround(40 * cos(a)), (int)lround(40 * sin(a)), levels);
}

int main() {
    // 1. The head angle is continuous: 10 ms never moves it more than ~1.5 degrees,
    //    it is monotonic within a turn, and one turn is the documented period.
    assert(kSweepPeriodMs >= 2000 && kSweepPeriodMs <= 4000);
    uint16_t prev = sweepAngle(0);
    int wraps = 0;
    for (uint32_t t = 10; t <= 2u * kSweepPeriodMs; t += 10) {
        const uint16_t a = sweepAngle(t);
        const int step = (int)a - (int)prev;
        if (step < 0) { wraps++; assert(step + 1024 <= 6); } else assert(step <= 6);   // 6/1024 turn = 2.1 deg
        prev = a;
    }
    assert(wraps == 2);
    assert(sweepAngle(0) == 0 && sweepAngle(kSweepPeriodMs / 2) == 512);

    // 2. Tail: brightest at the head, strictly fading with angular distance,
    //    still visible near the end, nothing beyond 90 degrees or ahead of the head.
    const SweepDir d = sweepDir(0);          // head straight up
    const int L = 32;
    int last = L + 1;
    for (double deg : {3.0, 15.0, 30.0, 50.0, 70.0, 85.0}) {
        const int v = behind(d, deg, L);
        printf("behind %5.1f deg -> level %d\n", deg, v);
        assert(v > 0 && v < last);
        last = v;
    }
    assert(behind(d, 3.0, L) >= L * 3 / 4);   // a strong head
    assert(behind(d, 100.0, L) == 0);
    assert(behind(d, 170.0, L) == 0);
    assert(behind(d, -10.0, L) == 0);         // ahead of the head is dark
    assert(trailLevel(d, 0, 0, L) == 0);      // centre pixel is not tinted

    // 3. Rotating the head rotates the tail with it.
    const SweepDir r = sweepDir(256);         // head pointing right (a quarter turn)
    assert(trailLevel(r, 40, -4, L) > trailLevel(r, 28, -28, L));   // near head > 45 deg back
    assert(trailLevel(r, 28, 28, L) == 0);                           // ahead of it
    puts("test_sweep_tail: ok");
    return 0;
}
