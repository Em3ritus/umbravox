// bench_art PACK_DIR: host us/frame for a layout theme's art on the Waveshare (480x320) SCAN screen: a full frame
// and the regions the presenter repaints each frame (radar, the animated decor, a count tile), each with an FNV-1a
// hash of its pixels so a speed-up that changes a pixel shows. Build: make -C sim bench_art; pack: tools/stress-pack.mjs.
#include "packload.h"
#include "scan_fixture.h"
#include <chrono>
#include <cstdio>
#include <vector>

static constexpr int W = 480, H = 320, RUNS = 20;

// The region r composed as the presenter does: 16-row strips over its rows, column window [r.x, r.x + r.w).
static uint64_t region(const scanr::ScanScene& sc, uieng::Rect r, std::vector<uint16_t>& buf) {
    uint64_t hash = 14695981039346656037ull;
    for (int y = r.y; y < r.y + r.h; y += 16) {
        const int h = std::min(16, r.y + r.h - y);
        scanr::S s(buf.data(), W, y, h, r.x, r.x + r.w);
        scanr::composeStrip(s, W, H, sc);
        for (int i = 0; i < h * r.w; i++) { hash ^= buf[i]; hash *= 1099511628211ull; }
    }
    return hash;
}

int main(int argc, char** argv) {
    if (argc != 2) { std::fprintf(stderr, "usage: bench_art PACK_DIR\n"); return 2; }
    char why[160] = "";
    const int id = packload::load(argv[1], why, sizeof why);
    if (id < 0 || why[0]) { std::fprintf(stderr, "load %s: %d %s\n", argv[1], id, why); return 1; }
    pal::setTheme((uint8_t)id);
    if (!packload::useMedia(id)) { std::fprintf(stderr, "no media for theme %d\n", id); return 1; }
    const scanr::ScanScene sc = scanfix::demo((uint8_t)id);
    uieng::Rect dr{0, 0, 0, 0};
    if (!scanr::animatedDecor(W, &dr)) { std::fprintf(stderr, "the pack has no animated decor\n"); return 1; }
    const struct { const char* name; uieng::Rect r; } parts[] = {
        {"full", {0, 0, W, H}}, {"radar", scanr::radarBounds(W, H)}, {"decor", dr}, {"tile", uieng::scan(W, H).tiles[0]}};
    std::vector<uint16_t> buf((size_t)W * 16);
    for (const auto& p : parts) {
        uint64_t hash = 0;
        const auto t0 = std::chrono::steady_clock::now();
        for (int k = 0; k < RUNS; k++) hash = region(sc, p.r, buf);
        const auto us = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - t0).count() / RUNS;
        std::printf("%-5s us=%lld pixels=%016llx\n", p.name, (long long)us, (unsigned long long)hash);
    }
    return 0;
}
