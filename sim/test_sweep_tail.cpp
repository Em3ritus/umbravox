// Smooth sweep: a continuous head angle and a 90-degree tail that fades with
// angular distance behind the head. Pure geometry, no framebuffer.
#include "../ui/scan_render.h"
#include "../ui/palette.h"
#include "scan_fixture.h"
#include <vector>
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

struct Fb : Sink {
    int w, h; std::vector<uint16_t> p;
    Fb(int W, int H) : w(W), h(H), p(W * H) {}
    void strip(int x, int y, int W, int H, const uint16_t* b) override {
        for (int j = 0; j < H; j++) for (int i = 0; i < W; i++) p[(y + j) * w + x + i] = b[j * W + i];
    }
};

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

    // 4. The needle is drawn LAST: it shows over a centre emblem / seal / art on every theme.
    for (int theme = 0; theme < pal::TH_COUNT; theme++) for (auto wh : {std::pair<int,int>{480,320},{320,240}}) {
        const int w = wh.first, h = wh.second;
        pal::setTheme(theme);
        Compositor comp; auto sc = scanfix::demo(theme);
        sc.animateSweep = true; sc.now = 0; sc.nBlips = 0;   // head straight up, no contact in the way
        Fb fb(w, h); comp.renderFull(w, h, sc, fb);
        const auto r = radarBounds(w, h);
        const int fr = (r.w - 5) / 2, fcx = r.x + fr + 2, fcy = r.y + fr + 2;
        for (int dy : {8, fr / 3, fr / 2}) {
            if (fb.p[(fcy - dy) * w + fcx] != pal::SWEEP_HI)
                { fprintf(stderr,"theme %d %dx%d: needle hidden at %d px above centre\n", theme, w, h, dy); assert(false); }
        }
    }

    // 5. Contacts: bigger when live, and they flare when the sweep passes them.
    {
        const Marker m = placeMarker(0, 60, 0, 1000, 85);            // bearing 0 = right of centre
        assert(blipLook(m, sweepDir(0), 480).size >= m.sizePx + 2);
        assert(blipLook(m, sweepDir(0), 320).size >= m.sizePx + 1);
        const int near = blipLook(m, sweepDir(276), 480).flare;       // head 7 degrees past the contact
        const int mid  = blipLook(m, sweepDir(256 + 128), 480).flare; // head 45 degrees past it
        assert(near > mid && mid > 0);
        assert(blipLook(m, sweepDir(768), 480).flare == 0);           // head on the far side: not lit
        assert(blipLook(m, sweepDir(200), 480).flare == 0);           // head has not reached it yet
        // On screen: the same contact is brighter and has a halo while the sweep is on it.
        auto lum = [](uint16_t p) { return ((p >> 11) & 31) * 2 + ((p >> 5) & 63) + (p & 31) * 2; };
        for (int theme = 0; theme < pal::TH_COUNT; theme++) {
            pal::setTheme(theme);
            const FieldBlip bl[1] = {{0, 60, 0, 0}};
            auto draw = [&](uint32_t now, bool withBlip) {
                auto sc = scanfix::demo(theme, now); sc.animateSweep = true; sc.blips = bl; sc.nBlips = withBlip ? 1 : 0;
                Compositor comp; Fb fb(480, 320); comp.renderFull(480, 320, sc, fb); return fb;
            };
            const auto rb = radarBounds(480, 320); const int fr = (rb.w - 5) / 2, cx = rb.x + fr + 2 + 51, cy = rb.y + fr + 2;
            const auto lit = draw(809, true), dark = draw(2250, true), bare = draw(809, false);   // 809 ms: head just past it; 2250 ms: far side
            if (lum(lit.p[cy * 480 + cx]) <= lum(dark.p[cy * 480 + cx])) { fprintf(stderr, "theme %d: swept contact not brighter\n", theme); assert(false); }
            const int halo = cx + 4;                                  // just outside the 6 px core, inside the glow arm
            if (lit.p[cy * 480 + halo] == bare.p[cy * 480 + halo]) { fprintf(stderr, "theme %d: no halo\n", theme); assert(false); }
        }
    }
    puts("test_sweep_tail: ok");
    return 0;
}
