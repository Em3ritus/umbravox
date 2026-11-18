// test_art_draw: the row-run picture paths (artpack::blitRun, scanr::artInto, scanr::nineInto) draw exactly what the
// per-pixel code they replaced drew, kept here as refArtInto / refNineInto; and scanr::firstLayer skips only layers an
// opaque decor or background really covers.
#include "packload.h"
#include "scan_fixture.h"
#include "artfix.h"
#include <string>
#include <sys/stat.h>
#include "../ui/artpack.h"
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <vector>

static int fails = 0, checks = 0;
static void check(const char* what, bool ok) { ++checks; if (!ok) { ++fails; printf("  FAIL %s\n", what); } }

// A valid .art (artpack.h v1): 16 colours, seeded random indices, so every nibble position carries a different value.
struct Pic { std::vector<uint8_t> bytes; artpack::Art a; };
static Pic makePic(int w, int h, int frames, bool transparent, uint32_t seed) {
    Pic p;
    auto& b = p.bytes;
    const uint8_t hdr[16] = {'U', 'V', 'A', 'R', 1, (uint8_t)(transparent ? 1 : 0), (uint8_t)w, (uint8_t)(w >> 8),
                             (uint8_t)h, (uint8_t)(h >> 8), (uint8_t)frames, (uint8_t)(frames >> 8), 16, 0, 0, 0};
    b.assign(hdr, hdr + 16);
    for (int i = 0; i < 16; i++) { const uint16_t c = (uint16_t)(0x1111 * i + 7); b.push_back((uint8_t)c); b.push_back((uint8_t)(c >> 8)); }
    for (int f = 0; f < frames; f++) { b.push_back(50); b.push_back(0); }
    const size_t fb = ((size_t)w * h + 1) / 2;
    for (int f = 0; f < frames; f++)
        for (size_t i = 0; i < fb; i++) {
            seed = seed * 1103515245u + 12345u;
            uint8_t v = (uint8_t)(seed >> 16);
            if ((w * h) & 1 && i == fb - 1) v &= 0xF0;   // the unused low nibble of an odd-sized frame
            b.push_back(v);
        }
    const char* why = nullptr;
    if (!artpack::parse(b.data(), b.size(), p.a, &why)) printf("  makePic %dx%d: %s\n", w, h, why ? why : "?");
    return p;
}

// The per-pixel code as it was before the row-run change.
static void refArtInto(scanr::S& s, const artpack::Art& a, int x, int y, uint16_t frame) {
    if (!a.px || a.w == 0) return;
    if (frame >= a.frames) frame = 0;
    const int y0 = std::max(y, s.y0), y1 = std::min(y + (int)a.h, s.y0 + s.h);
    const int x0 = std::max(x, s.lo), x1 = std::min(x + (int)a.w, s.hi);
    for (int yy = y0; yy < y1; ++yy) {
        uint16_t* row = s.px + (yy - s.y0) * s.stride;
        for (int xx = x0; xx < x1; ++xx) {
            const uint8_t ix = artpack::index(a, frame, xx - x, yy - y);
            if (ix == 0 && a.transparent) continue;
            row[xx - s.lo] = a.pal[ix];
        }
    }
}
static void refNineInto(scanr::S& s, const artpack::Art& a, uieng::Rect r, const uint8_t slice[4]) {
    if (!a.px || a.w == 0 || r.w <= 0 || r.h <= 0) return;
    const int y0 = std::max(r.y, s.y0), y1 = std::min(r.y + r.h, s.y0 + s.h);
    const int x0 = std::max(r.x, s.lo), x1 = std::min(r.x + r.w, s.hi);
    for (int yy = y0; yy < y1; ++yy) {
        const int sy = scanr::nineSrc(yy - r.y, r.h, a.h, slice[1], slice[3]);
        uint16_t* row = s.px + (yy - s.y0) * s.stride;
        for (int xx = x0; xx < x1; ++xx) {
            const int sx = scanr::nineSrc(xx - r.x, r.w, a.w, slice[0], slice[2]);
            const uint8_t ix = artpack::index(a, 0, sx, sy);
            if (ix == 0 && a.transparent) continue;
            row[xx - s.lo] = a.pal[ix];
        }
    }
}

// One strip [lo,hi) x [y0,y0+h) of a 480-wide screen, pre-filled with a pattern so a skipped pixel shows.
struct Strip { std::vector<uint16_t> px; scanr::S s; };
static Strip strip(int y0, int h, int lo, int hi) {
    Strip t{std::vector<uint16_t>((size_t)(hi - lo) * h), scanr::S(nullptr, 480, y0, h, lo, hi)};
    for (size_t i = 0; i < t.px.size(); i++) t.px[i] = (uint16_t)(0xA5A5 ^ i);
    t.s.px = t.px.data();
    return t;
}
static bool sameArt(const artpack::Art& a, int x, int y, uint16_t frame, int y0, int h, int lo, int hi) {
    Strip p = strip(y0, h, lo, hi), q = strip(y0, h, lo, hi);
    refArtInto(p.s, a, x, y, frame);
    scanr::artInto(q.s, a, x, y, frame);
    return p.px == q.px;
}
static bool sameNine(const artpack::Art& a, uieng::Rect r, const uint8_t sl[4], int y0, int h, int lo, int hi) {
    Strip p = strip(y0, h, lo, hi), q = strip(y0, h, lo, hi);
    refNineInto(p.s, a, r, sl);
    scanr::nineInto(q.s, a, r, sl);
    return p.px == q.px;
}

// --- firstLayer: packs written to a temp folder, loaded through sim/packload.h ----------------------------------
static std::string root;
static void writeFile(const std::string& path, const std::vector<uint8_t>& b) {
    FILE* f = fopen(path.c_str(), "wb"); fwrite(b.data(), 1, b.size(), f); fclose(f);
}
static void writeText(const std::string& path, const std::string& t) { writeFile(path, std::vector<uint8_t>(t.begin(), t.end())); }
static std::string themeJson(const char* name) {
    std::string t = std::string("{\"format\":1,\"name\":\"") + name + "\",\"header\":\"AAAA\",\"dial\":\"compact\",\"centrepiece\":\"heptagram\","
                    "\"sweeps\":[\"radar\"],\"alert\":\"ward\",\"colors\":{";
    for (int i = 0; i < 20; i++) t += std::string(i ? "," : "") + "\"" + themepack::ROLE_KEYS[i] + "\":\"" + (i == 0 ? "#000000" : "#C0C0C0") + "\"";
    return t + "}}";
}
// A Waveshare look: optional full-screen bg (a0, opaque), decor.0 240x240 at (100,60) (a1, opaque or transparent),
// decor.1 40x40 at (400,200) (a2, opaque). Returns the theme id, media installed.
static int pack(const char* folder, const char* name, bool bg, bool d0Transparent) {
    const std::string d = root + "/" + folder;
    mkdir(d.c_str(), 0755);
    writeText(d + "/theme.json", themeJson(name));
    std::string ws = "\"waveshare\":{";
    if (bg) ws += "\"style\":{\"bg\":\"a0\"},";
    ws += "\"decor.0\":{\"rect\":[100,60,240,240],\"art\":\"a1\"},\"decor.1\":{\"rect\":[400,200,40,40],\"art\":\"a2\"}}";
    writeText(d + "/layout.json", "{\"format\":1," + ws + "}");
    if (bg) writeFile(d + "/a0.art", artfix::make(480, 320, 1, false));
    writeFile(d + "/a1.art", artfix::make(240, 240, 1, d0Transparent));
    writeFile(d + "/a2.art", artfix::make(40, 40, 1, false));
    char why[160] = "";
    const int id = packload::load(d.c_str(), why, sizeof why);
    if (id < 0 || why[0]) printf("  load %s: %d %s\n", folder, id, why);
    pal::setTheme((uint8_t)id);
    packload::useMedia(id);
    return id;
}
// The region r composed as the presenter does it (16-row strips, column window), as one buffer.
static std::vector<uint16_t> compose(const scanr::ScanScene& sc, uieng::Rect r) {
    std::vector<uint16_t> out, buf((size_t)r.w * 16);
    for (int y = r.y; y < r.y + r.h; y += 16) {
        const int h = std::min(16, r.y + r.h - y);
        scanr::S st(buf.data(), 480, y, h, r.x, r.x + r.w);
        scanr::composeStrip(st, 480, 320, sc);
        out.insert(out.end(), buf.begin(), buf.begin() + (size_t)h * r.w);
    }
    return out;
}
static uint32_t blitsFor(const scanr::ScanScene& sc, uieng::Rect r) {
    scanr::artBlits() = 0; compose(sc, r); return scanr::artBlits();
}
// Composed with skipping and with every layer drawn, the region is the same.
static bool identical(const scanr::ScanScene& sc, uieng::Rect r) {
    scanr::coverEnabled() = true;  const auto a = compose(sc, r);
    scanr::coverEnabled() = false; const auto b = compose(sc, r);
    scanr::coverEnabled() = true;
    return a == b;
}
static void layers() {
    char tmpl[] = "/tmp/test_art_draw_XXXXXX";
    root = mkdtemp(tmpl);
    const uieng::Rect in0{110, 64, 220, 16}, in1{404, 208, 32, 16}, wide0{99, 64, 220, 16}, full{0, 0, 480, 320};
    auto win = [](uieng::Rect r) { return scanr::S(nullptr, 480, r.y, r.h, r.x, r.x + r.w); };
    {
        const int id = pack("cov", "COV", true, false);
        const auto sc = scanfix::demo((uint8_t)id);
        check("cover_skips decor.0 covers", scanr::firstLayer(480, 320, win(in0), true) == 2);
        check("cover_skips decor.1 covers", scanr::firstLayer(480, 320, win(in1), true) == 3);
        check("partial_not_cover: bg is the top cover", scanr::firstLayer(480, 320, win(wide0), true) == 1);
        check("not_scan: decor ignored", scanr::firstLayer(480, 320, win(in0), false) == 1);
        scanr::coverEnabled() = false; const uint32_t all0 = blitsFor(sc, in0), all1 = blitsFor(sc, in1);
        scanr::coverEnabled() = true;  const uint32_t cut0 = blitsFor(sc, in0), cut1 = blitsFor(sc, in1);
        check("cover_skips: the background is not drawn under decor.0", all0 - cut0 == 1);
        check("cover_skips: bg and decor.0 are not drawn under decor.1", all1 - cut1 == 2);
        check("frame_identical cov full", identical(sc, full));
        check("frame_identical cov radar", identical(sc, scanr::radarBounds(480, 320)));
        check("frame_identical cov windows", identical(sc, in0) && identical(sc, in1) && identical(sc, wide0));
        assets::clear((uint8_t)id);
        check("unloaded_not_cover", scanr::firstLayer(480, 320, win(in0), true) == 0);
        check("frame_identical cov unloaded", identical(sc, full));
    }
    {
        const int id = pack("trn", "TRN", true, true);
        const auto sc = scanfix::demo((uint8_t)id);
        check("transparent_not_cover", scanr::firstLayer(480, 320, win(in0), true) == 1);
        check("frame_identical trn", identical(sc, full) && identical(sc, in0));
    }
    {
        const int id = pack("nobg", "NOBG", false, false);
        const auto sc = scanfix::demo((uint8_t)id);
        check("nobg: decor.0 covers the VOID fill", scanr::firstLayer(480, 320, win(in0), true) == 2);
        check("nobg: nothing covers outside the decor", scanr::firstLayer(480, 320, win(wide0), true) == 0);
        check("frame_identical nobg", identical(sc, full) && identical(sc, in0) && identical(sc, wide0));
    }
}

int main() {
    // blitRun_basic: every start column and length of every row of a 7x3 picture, against artpack::index.
    for (bool tr : {false, true}) {
        Pic p = makePic(7, 3, 2, tr, 11);
        bool ok = true;
        for (int f = 0; f < 2; f++) for (int sy = 0; sy < 3; sy++) for (int sx = 0; sx < 7; sx++) for (int n = 0; sx + n <= 7; n++) {
            uint16_t got[8], want[8];
            for (int i = 0; i < 8; i++) got[i] = want[i] = (uint16_t)(0xBEEF + i);
            artpack::blitRun(p.a, (uint16_t)f, sx, sy, n, got);
            for (int i = 0; i < n; i++) { const uint8_t ix = artpack::index(p.a, (uint16_t)f, sx + i, sy); if (!(tr && ix == 0)) want[i] = p.a.pal[ix]; }
            ok = ok && memcmp(got, want, sizeof got) == 0;
        }
        check(tr ? "blitRun_basic transparent" : "blitRun_basic opaque", ok);
    }
    {   // run_odd_start: the clipped span starts on a low nibble.
        Pic p = makePic(7, 5, 1, false, 3);
        check("run_odd_start x=-3", sameArt(p.a, -3, 0, 0, 0, 5, 0, 480));
        check("run_odd_start lo=5", sameArt(p.a, 0, 0, 0, 0, 5, 5, 480));
        check("run_odd_start lo=4 x=1", sameArt(p.a, 1, 1, 0, 0, 4, 4, 9));
    }
    {   // widths: 1, 2, 3 and 480 wide; 1 and 320 tall; strips over the middle of the picture.
        bool ok = true;
        for (int w : {1, 2, 3, 480}) for (int h : {1, 320}) {
            if ((size_t)(16 + 32 + 2 + ((size_t)w * h + 1) / 2) > artpack::MAX_ART) continue;
            Pic p = makePic(w, h, 1, w == 3, (uint32_t)(w * 31 + h));
            ok = ok && sameArt(p.a, 0, 0, 0, 0, std::min(h, 16), 0, 480) && sameArt(p.a, 0, 0, 0, h > 16 ? 150 : 0, std::min(h, 16), 0, 480);
        }
        check("widths", ok);
    }
    {   // transparent_and_opaque, over a column window that cuts the picture.
        Pic o = makePic(40, 20, 1, false, 5), t = makePic(40, 20, 1, true, 5);
        check("opaque windowed", sameArt(o.a, 100, 30, 0, 32, 16, 110, 133));
        check("transparent windowed", sameArt(t.a, 100, 30, 0, 32, 16, 110, 133));
    }
    {   // frame_out_of_range: falls back to frame 0.
        Pic p = makePic(9, 9, 3, false, 9);
        check("frame 2", sameArt(p.a, 0, 0, 2, 0, 9, 0, 480));
        check("frame_out_of_range 3", sameArt(p.a, 0, 0, 3, 0, 9, 0, 480));
        check("frame_out_of_range 65535", sameArt(p.a, 0, 0, 65535, 0, 9, 0, 480));
    }
    {   // offscreen: negative and past the screen; nothing outside the window changes.
        Pic p = makePic(30, 30, 1, false, 13);
        check("offscreen left/top", sameArt(p.a, -20, -25, 0, 0, 16, 0, 480));
        check("offscreen right/bottom", sameArt(p.a, 470, 310, 0, 304, 16, 0, 480));
        check("offscreen fully", sameArt(p.a, 500, 0, 0, 0, 16, 0, 480) && sameArt(p.a, -40, 0, 0, 0, 16, 0, 480));
    }
    {   // nine_sizes: rect 1x1 up to 480x320, three slice sets, full and windowed strips.
        Pic p = makePic(12, 10, 1, true, 21), q = makePic(12, 10, 1, false, 22);
        const uint8_t sls[3][4] = {{0, 0, 0, 0}, {4, 4, 4, 4}, {1, 2, 3, 4}};
        const uieng::Rect rs[] = {{5, 5, 1, 1}, {0, 0, 12, 10}, {3, 7, 37, 23}, {0, 0, 480, 320}, {-6, -4, 50, 40}};
        bool ok = true;
        for (const auto& sl : sls) for (const auto& r : rs) for (const Pic* x : {&p, &q})
            for (int y0 : {0, 8, 304}) ok = ok && sameNine(x->a, r, sl, y0, 16, 0, 480) && sameNine(x->a, r, sl, y0, 16, 9, 61);
        check("nine_sizes", ok);
    }
    {   // nine_borders_exceed: borders larger than the picture and the rect.
        Pic p = makePic(20, 20, 1, false, 31);
        const uint8_t sl[4] = {30, 30, 30, 30}, sl2[4] = {15, 0, 15, 0};
        check("nine_borders_exceed", sameNine(p.a, {4, 4, 10, 10}, sl, 0, 16, 0, 480) && sameNine(p.a, {4, 4, 25, 10}, sl2, 0, 16, 0, 480));
        // nine_borders_fill_rect: L + R == the rect width with a middle in the picture (the run path's edge),
        // and one pixel either side of it (L + R == w - 1 runs, w + 1 falls back).
        const uint8_t e[4] = {6, 2, 4, 2};
        bool ok = true;
        for (int rw : {9, 10, 11}) for (int lo : {0, 7}) ok = ok && sameNine(p.a, {3, 1, rw, 12}, e, 0, 16, lo, lo + 40);
        check("nine_borders_fill_rect", ok);
    }
    layers();
    printf("%s  %d/%d checks passed\n", fails ? "FAILED" : "PASSED", checks - fails, checks);
    return fails ? 1 : 0;
}
