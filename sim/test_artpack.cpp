// test_artpack: the .art parser (ui/artpack.h) on good and hostile input, and the rect / nine-slice blits.
#include "../ui/scan_render.h"
#include "../ui/artpack.h"
#include "artfix.h"
#include <stdio.h>
#include <stdlib.h>
#include <vector>

static int fails = 0;
static void check(const char* what, bool ok) {
    if (!ok) { ++fails; printf("FAIL %s\n", what); }
}
static void refused(const char* what, const std::vector<uint8_t>& d) {
    artpack::Art a; const char* why = "";
    const bool ok = artpack::parse(d.data(), d.size(), a, &why);
    check(what, !ok && why && why[0]);
}

static const int FW = 64, FH = 48;
static const uint16_t BG = 0x1111;

template <class Fn>
static std::vector<uint16_t> render(int sh, int lo, int hi, Fn draw) {
    std::vector<uint16_t> frame(FW * FH, BG);
    const int xhi = hi < 0 ? FW : hi;
    for (int y = 0; y < FH; y += sh) {
        const int h = (y + sh > FH) ? FH - y : sh;
        std::vector<uint16_t> buf((size_t)h * (xhi - lo), BG);
        scanr::S s(buf.data(), FW, y, h, lo, hi);
        draw(s);
        for (int r = 0; r < h; ++r)
            for (int x = lo; x < xhi; ++x) frame[(y + r) * FW + x] = buf[(size_t)r * (xhi - lo) + (x - lo)];
    }
    return frame;
}

int main() {
    const auto good = artfix::make(7, 5, 3, true);
    artpack::Art a; const char* why = "";
    check("valid art parses", artpack::parse(good.data(), good.size(), a, &why));
    check("header", a.w == 7 && a.h == 5 && a.frames == 3 && a.ncol == 4 && a.transparent && a.cycleMs == 600);
    check("animated", artpack::animated(a));
    const uint32_t at[] = {0, 99, 100, 299, 300, 599, 600};
    const uint16_t want[] = {0, 0, 1, 1, 2, 2, 0};
    for (int i = 0; i < 7; ++i) check("frameAt", artpack::frameAt(a, at[i]) == want[i]);
    {
        const auto one = artfix::make(4, 4, 1, false);
        artpack::Art b;
        check("one frame parses", artpack::parse(one.data(), one.size(), b, &why));
        check("one frame: frameAt 0", artpack::frameAt(b, 12345) == 0 && !artpack::animated(b));
    }

    // Refusals. Header: magic 0-3, ver 4, flags 5, w 6-7, h 8-9, frames 10-11, ncol 12.
    { auto d = good; d[1] = 'X'; refused("bad magic", d); }
    { auto d = good; d[4] = 2; refused("version 2", d); }
    { auto d = good; d[6] = 0; d[7] = 0; refused("w 0", d); }
    { auto d = artfix::make(481, 1, 1, false); refused("w 481", d); }
    { auto d = artfix::make(1, 321, 1, false); refused("h 321", d); }
    { auto d = good; d[12] = 1; refused("ncol 1", d); }
    { auto d = good; d[12] = 17; refused("ncol 17", d); }
    { auto d = good; d[10] = 0; refused("frames 0", d); }
    { auto d = artfix::make(2, 2, 65, false); refused("frames 65", d); }
    { auto d = good; d[16 + 8] = 0; d[16 + 9] = 0; refused("delay 0", d); }
    { auto d = good; d.pop_back(); refused("length -1", d); }
    { auto d = good; d.push_back(0); refused("length +1", d); }
    { auto d = artfix::make(480, 320, 3, false); refused("over MAX_ART", d); }
    { auto d = good; d[d.size() - 1] = 0x9F; refused("index past ncol", d); }
    { std::vector<uint8_t> d(good.begin(), good.begin() + 10); refused("truncated header", d); }

    srand(77);
    int parsed = 0;
    for (int i = 0; i < 2000; ++i) {
        auto d = good;
        const int n = 1 + rand() % 4;
        for (int k = 0; k < n; ++k) d[rand() % d.size()] = (uint8_t)rand();
        if (rand() % 5 == 0) d.resize(rand() % (d.size() + 1));
        artpack::Art m; const char* w = "";
        if (artpack::parse(d.data(), d.size(), m, &w)) {
            ++parsed;
            for (uint32_t t = 0; t < 2000; t += 37) (void)artpack::frameAt(m, t);
            auto fr = render(FH, 0, -1, [&](scanr::S& s) { scanr::artInto(s, m, 3, 3, artpack::frameAt(m, 450)); });
            (void)fr;
        } else check("mutant refusal has a reason", w && w[0]);
    }
    printf("mutants parsed: %d of 2000\n", parsed);

    // artInto: transparent index 0 untouched, the rest drawn from the palette, frame selects pixels.
    {
        auto fr = render(FH, 0, -1, [&](scanr::S& s) { scanr::artInto(s, a, 10, 20, 1); });
        bool ok = true;
        for (int y = 0; y < 5; ++y) for (int x = 0; x < 7; ++x) {
            const uint8_t ix = artfix::indexAt(x, y, 1);
            const uint16_t want = ix == 0 ? BG : artfix::PAL[ix];
            if (fr[(20 + y) * FW + 10 + x] != want) ok = false;
        }
        int ink = 0; for (auto p : fr) if (p != BG) ++ink;
        check("artInto draws exactly its pixels", ok && ink == 7 * 5 - 5);
    }
    {
        const auto op = artfix::make(7, 5, 1, false);
        artpack::Art b; artpack::parse(op.data(), op.size(), b, &why);
        auto fr = render(FH, 0, -1, [&](scanr::S& s) { scanr::artInto(s, b, 0, 0, 0); });
        check("opaque art draws index 0", fr[0] == artfix::PAL[0]);
    }
    {
        auto fr = render(FH, 0, -1, [&](scanr::S& s) { scanr::artInto(s, a, -3, -2, 0); });
        check("negative x/y: visible part only", fr[0] == artfix::PAL[artfix::indexAt(3, 2, 0)]);
        auto fr2 = render(FH, 0, -1, [&](scanr::S& s) { scanr::artInto(s, a, FW - 2, FH - 1, 0); });
        int ink = 0; for (auto p : fr2) if (p != BG) ++ink;
        check("off the right/bottom edge: visible part only", ink == 1);   // x=1 of row 0 (x=0 transparent)
    }
    {
        auto full = render(FH, 0, -1, [&](scanr::S& s) { scanr::artInto(s, a, 4, 9, 2); });
        auto strips = render(3, 0, -1, [&](scanr::S& s) { scanr::artInto(s, a, 4, 9, 2); });
        auto win = render(FH, 5, 40, [&](scanr::S& s) { scanr::artInto(s, a, 4, 9, 2); });
        bool same = true;
        for (int y = 0; y < FH; ++y) for (int x = 5; x < 40; ++x) if (win[y * FW + x] != full[y * FW + x]) same = false;
        check("artInto: strips and window equal one strip", full == strips && same);
    }

    // nineInto: corners copied, nothing outside the rect.
    {
        const auto nd = artfix::make(9, 9, 1, false);
        artpack::Art n; check("9x9 parses", artpack::parse(nd.data(), nd.size(), n, &why));
        const uint8_t sl[4] = {3, 3, 3, 3};
        const uieng::Rect r{10, 10, 40, 20};
        auto fr = render(FH, 0, -1, [&](scanr::S& s) { scanr::nineInto(s, n, r, sl); });
        bool corners = true, outside = true;
        for (int y = 0; y < 3; ++y) for (int x = 0; x < 3; ++x) {
            if (fr[(10 + y) * FW + 10 + x] != artfix::PAL[artfix::indexAt(x, y, 0)]) corners = false;                  // top-left
            if (fr[(10 + y) * FW + 47 + x] != artfix::PAL[artfix::indexAt(6 + x, y, 0)]) corners = false;              // top-right
            if (fr[(27 + y) * FW + 10 + x] != artfix::PAL[artfix::indexAt(x, 6 + y, 0)]) corners = false;              // bottom-left
            if (fr[(27 + y) * FW + 47 + x] != artfix::PAL[artfix::indexAt(6 + x, 6 + y, 0)]) corners = false;          // bottom-right
        }
        for (int y = 0; y < FH; ++y) for (int x = 0; x < FW; ++x) {
            const bool in = x >= 10 && x < 50 && y >= 10 && y < 30;
            if (!in && fr[y * FW + x] != BG) outside = false;
        }
        check("nineInto corners", corners);
        check("nineInto stays in its rect", outside);
        // The top edge repeats source columns 3..5.
        check("nineInto edge repeats", fr[10 * FW + 13] == fr[10 * FW + 16] && fr[10 * FW + 14] == fr[10 * FW + 17]);
        auto strips = render(5, 0, -1, [&](scanr::S& s) { scanr::nineInto(s, n, r, sl); });
        check("nineInto strip invariant", strips == fr);
    }

    printf(fails ? "test_artpack: %d FAILED\n" : "test_artpack: all passed\n", fails);
    return fails ? 1 : 0;
}
