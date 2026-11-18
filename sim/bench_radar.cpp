// Timing: Scan draw with a generated-ring radar (r=77) against the built-in (r=102, baked table), and the
// one-off ring generation. Host numbers: compare ratios, not absolute times (the board has no FPU).
// Build + run: make -C sim bench_radar && sim/bench_radar
#include "../ui/themelayout.h"
#include "../ui/scan_render.h"
#include "../ui/palette.h"
#include "scan_fixture.h"
#include <chrono>
#include <cstdio>
#include <string>
#include <vector>

struct NullSink : scanr::Sink { uint32_t h = 0; void strip(int, int, int w, int hh, const uint16_t* px) override { for (int i = 0; i < w * hh; i++) h = h * 31 + px[i]; } };

static double drawUs(int reps) {
    static scanr::Compositor c;
    auto sc = scanfix::demo(pal::TH_UMBRAVOX);
    NullSink s;
    const auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < reps; i++) { sc.now = 5000 + i * 40; c.renderFull(480, 320, sc, s); }
    return std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count() / reps;
}

int main() {
    pal::setTheme(pal::TH_UMBRAVOX);
    const int reps = 400;
    drawUs(20);
    const double builtin = drawUs(reps);
    // r = 77: box 159, placed where the built-in radar is.
    const uieng::Rect r0 = uieng::builtinScan(480, 320).radar;
    char json[160]; snprintf(json, sizeof json, "{\"format\":1,\"waveshare\":{\"radar\":[%d,%d,159,159]}}", r0.x, r0.y);
    themelayout::Layout L; uieng::ChromeOverride o; uieng::LayoutOverride lo{}; char why[96];
    if (!themelayout::parse(json, strlen(json), L, why, sizeof why) || !themelayout::resolve(L, themelayout::B_WAVESHARE, o, why, sizeof why)) {
        printf("layout: %s\n", why); return 1;
    }
    lo.board[0] = &o;
    uieng::activeLayout() = &lo;
    drawUs(20);   // warms the ring cache
    const double gen77 = drawUs(reps);
    uieng::activeLayout() = nullptr;
    std::vector<radargeometry::Point> buf(radargeometry::ringCapacity(101));
    const auto t0 = std::chrono::steady_clock::now();
    int n = 0;
    for (int i = 0; i < 50; i++) n = radargeometry::generateRing(101, buf.data(), (int)buf.size());
    const double genUs = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count() / 50;
    printf("scan draw 480x320: built-in r=102 %.0f us, layout r=77 %.0f us (%.0f%%)\n", builtin, gen77, 100.0 * gen77 / builtin);
    printf("generateRing(101): %d points, %.0f us once (host)\n", n, genUs);
    printf("trig ring fallback calls: %d\n", radargeometry::fallbackCalls());
    return 0;
}
