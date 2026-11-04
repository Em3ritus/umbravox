// Layout A for the 480x320 panel: slim count column on the left, a big radar, and a right-hand
// column for the channel readout and the spectrum. The 320x240 panels keep their layout exactly.
#include "../ui/scan_render.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>
#include "scan_fixture.h"
#include "../ui/palette.h"
using namespace scanr;
using uieng::Rect;

static bool hit(const Rect& a, const Rect& b) { return a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h; }
static bool inside(const Rect& r, const Rect& o) { return r.x >= o.x && r.y >= o.y && r.x + r.w <= o.x + o.w && r.y + r.h <= o.y + o.h; }
static void eq(const Rect& r, int x, int y, int w, int h, const char* what) {
    if (r.x != x || r.y != y || r.w != w || r.h != h) { fprintf(stderr, "%s: {%d,%d,%d,%d} want {%d,%d,%d,%d}\n", what, r.x, r.y, r.w, r.h, x, y, w, h); assert(false); }
}

struct Fb : Sink {
    int w, h; std::vector<uint16_t> p;
    Fb(int W, int H) : w(W), h(H), p(W * H) {}
    void strip(int x, int y, int W, int H, const uint16_t* b) override {
        for (int j = 0; j < H; j++) for (int i = 0; i < W; i++) p[(y + j) * w + x + i] = b[j * W + i];
    }
};

int main() {
    // Small panels are untouched.
    { const auto L = uieng::scan(320, 240); const auto r = radarBounds(320, 240); const auto hs = heroSub(L.hero);
      eq(L.tiles[0], 8, 52, 88, 45, "tile0 320"); eq(L.tiles[1], 8, 97, 88, 45, "tile1 320"); eq(L.tiles[2], 8, 142, 88, 46, "tile2 320");
      eq(L.hero, 104, 52, 208, 136, "hero 320"); eq(r, 151, 52, 115, 115, "radar 320"); eq(hs.spectrum, 138, 162, 140, 26, "spectrum 320");
      assert(hs.barBase == 177 && hs.barMax == 8); }

    // Wide panel: layout A.
    const auto L = uieng::scan(480, 320); const auto r = radarBounds(480, 320); const auto hs = heroSub(L.hero);
    eq(L.tiles[0], 16, 52, 100, 68, "tile0"); eq(L.tiles[1], 16, 124, 100, 68, "tile1"); eq(L.tiles[2], 16, 196, 100, 68, "tile2");
    eq(L.hero, 124, 52, 340, 216, "hero");
    assert(r.w >= 205 && r.w == r.h);                       // a 102 px radius (it was 85)
    assert(inside(r, L.hero) && r.y >= 52 && r.y + r.h <= 268);
    for (int i = 0; i < 3; i++) assert(!hit(r, L.tiles[i]));
    assert(inside(hs.spectrum, L.hero) && !hit(hs.spectrum, r));   // the spectrum sits in the right column, clear of the radar
    assert(hs.spectrum.w >= 60 && hs.side.w >= 64 && !hit(hs.side, r) && inside(hs.side, L.hero));
    assert(hs.barMax >= 40);                                // a real equaliser, not a strip

    // Counts: full digits up to 99,999, then K / M.
    struct { long v; const char* s; } cases[] = {
        {0, "0"}, {7, "7"}, {9999, "9999"}, {10000, "10000"}, {99999, "99999"}, {100000, "100K"}, {106400, "106K"},
        {999999, "999K"}, {1000000, "1.0M"}, {1234567, "1.2M"}, {-5, "0"} };
    for (auto& c : cases) { char b[16]; fmtCount(c.v, b, sizeof b); if (strcmp(b, c.s)) { fprintf(stderr, "fmtCount(%ld) = %s want %s\n", c.v, b, c.s); assert(false); } }
    char tiny[4]; fmtCount(1234567, tiny, sizeof tiny); assert(strlen(tiny) < 4);   // never overruns
    // And every formatted count fits the tile at some scale.
    for (long v : {0L, 9999L, 10000L, 99999L, 100000L, 999999L, 1000000L, 12345678L}) {
        char b[16]; fmtCount(v, b, sizeof b);
        const int sc = fitScale(b, L.tiles[0].w - 16, 3);
        assert(Font::width(b, sc) <= L.tiles[0].w - 16);
    }

    // Runic ring (wide panel): 16 glyphs between the middle and outer rings. Dim until the sweep has just
    // passed them, then they light up with it.
    {
        const int R = runeRingRadius(102);
        assert(R > 68 + 6 && R < 102 - 6);                      // clear of both rings (2 px thick, 5 px glyph radius)
        int seen = 0;
        for (int k = 0; k < kRunes; k++) {
            int x, y; runePos(k, R, x, y);
            assert(x * x + y * y >= (R - 1) * (R - 1) && x * x + y * y <= (R + 1) * (R + 1));   // on the circle
            if (k == 0) assert(x == 0 && y == -R);              // starts straight up, clockwise
            if (k == kRunes / 4) assert(x == R && y == 0);
            seen++;
        }
        assert(seen == 16);
        for (int theme = 0; theme < pal::TH_COUNT; theme++) {
            pal::setTheme(theme);
            auto sc = scanfix::demo(theme, 0); sc.animateSweep = true; sc.nBlips = 0;       // head straight up: the tail covers up-left
            Fb fb(480, 320); Compositor comp; comp.renderFull(480, 320, sc, fb);
            const auto rb = radarBounds(480, 320); const int fr = (rb.w - 5) / 2, cx = rb.x + fr + 2, cy = rb.y + fr + 2;
            auto count = [&](int k, uint16_t col) {             // pixels of exactly `col` in the glyph's 12x12 box
                int x, y; runePos(k, R, x, y); int n = 0;
                for (int j = -6; j <= 5; j++) for (int i = -6; i <= 5; i++) n += fb.p[(cy + y + j) * 480 + cx + x + i] == col;
                return n;
            };
            for (int k : {4, 6, 8})                              // right / lower right / bottom: far from the sweep, unlit
                if (count(k, pal::EDGE_LIT) < 15) { fprintf(stderr, "theme %d: rune %d not drawn (%d px)\n", theme, k, count(k, pal::EDGE_LIT)); assert(false); }
            for (int k : {14, 15})                               // just behind the head: lit, so no longer the dim colour
                if (count(k, pal::EDGE_LIT) != 0) { fprintf(stderr, "theme %d: rune %d not lit by the sweep\n", theme, k); assert(false); }
        }
    }
    puts("test_scan_layout: ok");
    return 0;
}
