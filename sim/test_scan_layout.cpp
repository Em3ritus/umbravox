// Layout A for the 480x320 panel: slim count column on the left, a big radar, and a right-hand
// column for the channel readout and the spectrum. The 320x240 panels keep their layout exactly.
#include "../ui/scan_render.h"
#include <cassert>
#include <cstdio>
#include <cstring>
using namespace scanr;
using uieng::Rect;

static bool hit(const Rect& a, const Rect& b) { return a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h; }
static bool inside(const Rect& r, const Rect& o) { return r.x >= o.x && r.y >= o.y && r.x + r.w <= o.x + o.w && r.y + r.h <= o.y + o.h; }
static void eq(const Rect& r, int x, int y, int w, int h, const char* what) {
    if (r.x != x || r.y != y || r.w != w || r.h != h) { fprintf(stderr, "%s: {%d,%d,%d,%d} want {%d,%d,%d,%d}\n", what, r.x, r.y, r.w, r.h, x, y, w, h); assert(false); }
}

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
    puts("test_scan_layout: ok");
    return 0;
}
