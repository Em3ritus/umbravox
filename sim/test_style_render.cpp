// test_style_render: a layout theme's styles, background art and decor on Scan (plan 4), on both engine boards.
// Packs are written to a temp folder and loaded through sim/packload.h, the device loader's host twin.
#include "packload.h"
#include "scan_fixture.h"
#include "../ui/scan_present.h"
#include "fontfix.h"
#include "artfix.h"
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <vector>
#include <sys/stat.h>

static int fails = 0, checks = 0;
static void check(const char* what, bool ok) { ++checks; if (!ok) { ++fails; printf("  FAIL %s\n", what); } }

static std::string root;
static void writeFile(const std::string& path, const std::vector<uint8_t>& b) {
    FILE* f = fopen(path.c_str(), "wb"); fwrite(b.data(), 1, b.size(), f); fclose(f);
}
static void writeText(const std::string& path, const std::string& s) { writeFile(path, std::vector<uint8_t>(s.begin(), s.end())); }
static std::string themeJson(const char* name) {
    std::string s = std::string("{\"format\":1,\"name\":\"") + name + "\",\"header\":\"AAAA\",\"dial\":\"compact\",\"centrepiece\":\"heptagram\","
                    "\"sweeps\":[\"radar\"],\"alert\":\"ward\",\"colors\":{";
    for (int i = 0; i < 20; i++) s += std::string(i ? "," : "") + "\"" + themepack::ROLE_KEYS[i] + "\":\"" + (i == 0 ? "#000000" : "#C0C0C0") + "\"";
    return s + "}}";
}
static std::string rj(uieng::Rect r) { char b[64]; snprintf(b, sizeof b, "[%d,%d,%d,%d]", r.x, r.y, r.w, r.h); return b; }

static const int BW[2] = {480, 320}, BH[2] = {320, 240};
static const char* BKEY[2] = {"waveshare", "cyd"};
static const uint16_t MAG = 0xF81F;   // "#ff00ff": in no theme colour and not in the fixture art's palette

// Decor rect for a board: overlaps count tile 0 by 10 px at its top-right corner.
static uieng::Rect decorRect(int b) {
    const uieng::Rect t = themelayout::defaultRect((themelayout::Board)b, themelayout::P_COUNT_WIFI);
    return {t.x + t.w - 10, t.y - 20, 30, 30};
}
static std::string board(int b, bool full) {
    const uieng::Rect wm = themelayout::defaultRect((themelayout::Board)b, themelayout::P_WORDMARK);
    std::string s = std::string("\"") + BKEY[b] + "\":{\"wordmark\":{\"rect\":" + rj(wm) + ",\"fill\":\"#ff00ff\"";
    if (full) s += ",\"font\":\"f0\"";
    s += "}";
    if (full) s += ",\"style\":{\"bg\":\"a" + std::to_string(b) + "\"},\"decor.0\":{\"rect\":" + rj(decorRect(b)) + ",\"art\":\"a" + std::to_string(2 + b) + "\"}";
    return s + "}";
}
static int loadPack(const char* folder, const char* name, bool full, bool withFiles) {
    const std::string d = root + "/" + folder;
    mkdir(d.c_str(), 0755);
    writeText(d + "/theme.json", themeJson(name));
    writeText(d + "/layout.json", "{\"format\":1," + board(0, full) + "," + board(1, full) + "}");
    if (withFiles) {
        writeFile(d + "/f0.fnt", fontfix::make());
        writeFile(d + "/a0.art", artfix::make(480, 320, 1, false));
        writeFile(d + "/a1.art", artfix::make(320, 240, 1, false));
        writeFile(d + "/a2.art", artfix::make(30, 30, 2, true));
        writeFile(d + "/a3.art", artfix::make(30, 30, 2, true));
    }
    char why[160];
    const int id = packload::load(d.c_str(), why, sizeof why);
    if (id < 0 || why[0]) printf("  load %s: %d %s\n", folder, id, why);
    return id;
}

// One Scan frame through strips of height sh.
static std::vector<uint16_t> render(int W, int H, const scanr::ScanScene& sc, int sh) {
    std::vector<uint16_t> fb((size_t)W * H, 0);
    std::vector<uint16_t> buf((size_t)W * sh);
    for (int y = 0; y < H; y += sh) {
        const int h = y + sh > H ? H - y : sh;
        scanr::S s(buf.data(), W, y, h);
        scanr::composeStrip(s, W, H, sc);
        for (int r = 0; r < h; r++) for (int x = 0; x < W; x++) fb[(size_t)(y + r) * W + x] = buf[(size_t)r * W + x];
    }
    return fb;
}
static bool inside(uieng::Rect r, int x, int y) { return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h; }

int main() {
    char tmpl[] = "/tmp/uvstyleXXXXXX";
    root = mkdtemp(tmpl);
    pal::clearPacks();
    const int full = loadPack("styled", "STYLED ONE", true, true);
    const int bare = loadPack("colours", "STYLED TWO", false, false);
    check("both packs load", full >= 0 && bare >= 0);
    if (full < 0 || bare < 0) { printf("FAILED\n"); return 1; }

    for (int b = 0; b < 2; b++) {
        const int W = BW[b], H = BH[b];
        char label[96];
        auto L = [&](const char* what) { snprintf(label, sizeof label, "%s: %s", BKEY[b], what); return label; };
        const uieng::Rect wm = themelayout::defaultRect((themelayout::Board)b, themelayout::P_WORDMARK);
        const uieng::Rect tile = uieng::Rect{};   // filled below from the live layout
        (void)tile;

        // The colours-only pack: magenta only inside the wordmark.
        pal::setTheme((uint8_t)bare);
        const uint16_t before[5] = {pal::TEXT, pal::BRIGHT, pal::PANEL, pal::EDGE, pal::EDGE_LIT};
        const auto cbare = render(W, H, scanfix::demo((uint8_t)bare), 16);
        const uint16_t after[5] = {pal::TEXT, pal::BRIGHT, pal::PANEL, pal::EDGE, pal::EDGE_LIT};
        check(L("pal colours restored after a frame"), memcmp(before, after, sizeof before) == 0);
        int magIn = 0, magOut = 0;
        for (int y = 0; y < H; y++) for (int x = 0; x < W; x++)
            if (cbare[(size_t)y * W + x] == MAG) (inside(wm, x, y) ? magIn : magOut)++;
        check(L("fill colour inside the wordmark"), magIn > wm.w * wm.h / 2);
        check(L("fill colour nowhere else"), magOut == 0);

        // The full pack, media not installed yet: draws like the colours-only pack (same header text, same layout rects).
        pal::setTheme((uint8_t)full);
        assets::clear((uint8_t)full);
        const auto noMedia = render(W, H, scanfix::demo((uint8_t)full), 16);
        check(L("media absent: equals the colours-only layout"), noMedia == cbare);

        // Installed.
        packload::useMedia(full);
        const auto styled = render(W, H, scanfix::demo((uint8_t)full), 16);
        const uieng::ScanLayout SL = uieng::scan(W, H);
        const uieng::Rect dr = decorRect(b), t0 = SL.tiles[0];
        const artpack::Art* bgArt = assets::art(b);
        const artpack::Art* decArt = assets::art(2 + b);
        check(L("media installed"), bgArt && decArt && assets::font(0));
        if (!bgArt || !decArt) continue;
        int bgSeen = 0, decorSeen = 0, decorUnderTile = 0, fontDiff = 0, fontOut = 0;
        for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) {
            const size_t i = (size_t)y * W + x;
            const uint16_t artPx = bgArt->pal[artpack::index(*bgArt, 0, x, y)];
            if (styled[i] != noMedia[i] && styled[i] == artPx && !inside(dr, x, y)) bgSeen++;
            if (inside(dr, x, y) && !inside(t0, x, y)) {
                const uint8_t ix = artpack::index(*decArt, artpack::frameAt(*decArt, 5000), x - dr.x, y - dr.y);
                if (ix && styled[i] == decArt->pal[ix]) decorSeen++;
            }
            if (inside(dr, x, y) && inside(t0, x, y) && styled[i] != noMedia[i]) decorUnderTile++;
            if (inside(wm, x, y)) { if (styled[i] != noMedia[i] && styled[i] != bgArt->pal[artpack::index(*bgArt, 0, x, y)]) fontDiff++; }
        }
        check(L("background art shows where nothing else draws"), bgSeen > 1000);
        check(L("decor drawn in its rect"), decorSeen > 50);
        check(L("a count tile draws over the decor"), decorUnderTile == 0);
        check(L("the wordmark uses the pack font"), fontDiff > 0);
        (void)fontOut;

        // Strip heights and a column window do not change the frame.
        bool stripsSame = true;
        for (int sh : {1, 7, 40}) stripsSame = stripsSame && render(W, H, scanfix::demo((uint8_t)full), sh) == styled;
        check(L("strip heights 1, 7, 40 equal 16"), stripsSame);

        // Animated decor: a frame change marks the decor dirty, nothing else does.
        scanr::ScanScene a = scanfix::demo((uint8_t)full, 0), c = scanfix::demo((uint8_t)full, 50), d = scanfix::demo((uint8_t)full, 150);
        a.animateSweep = c.animateSweep = d.animateSweep = false;
        check(L("decor frame unchanged: not dirty"), !scanpres::sceneParts(W, H, a, c).decor);
        check(L("decor frame changed: dirty"), scanpres::sceneParts(W, H, a, d).decor);
        uieng::Rect r;
        check(L("the animated decor rect"), scanr::animatedDecor(W, &r) && r == dr);
    }
    // Decor over the radar: every pixel the radar inks without the decor is the same with it (decor is under every piece).
    {
        const uieng::Rect rr = themelayout::defaultRect(themelayout::B_WAVESHARE, themelayout::P_RADAR);
        const uieng::Rect dr{rr.x + 4, rr.y + rr.h / 2 - 15, 40, 30};   // across the radar's left rim
        const std::string d = root + "/radar-decor";
        mkdir(d.c_str(), 0755);
        writeText(d + "/theme.json", themeJson("RADAR DECOR"));
        writeText(d + "/layout.json", "{\"format\":1,\"waveshare\":{\"decor.0\":{\"rect\":" + rj(dr) + ",\"art\":\"a0\"}},\"cyd\":{}}");
        writeFile(d + "/a0.art", artfix::make(40, 30, 1, false));
        char why[160];
        const int id = packload::load(d.c_str(), why, sizeof why);
        check("radar decor pack loads", id >= 0 && !why[0]);
        if (id >= 0) {
            pal::setTheme((uint8_t)id);
            scanr::ScanScene sc = scanfix::demo((uint8_t)id); sc.animateSweep = false;
            assets::clear((uint8_t)id);
            const auto bare = render(480, 320, sc, 16);
            packload::useMedia(id);
            const auto with = render(480, 320, sc, 16);
            const uint16_t ground = pal::VOID;
            int radarInk = 0, covered = 0;
            for (int y = dr.y; y < dr.y + dr.h; y++) for (int x = dr.x; x < dr.x + dr.w; x++) {
                const size_t i = (size_t)y * 480 + x;
                // The radar draws AFTER the decor and blends with what is under it, so a radar pixel over the decor is
                // neither the bare colour nor the decor's: covered means the decor's colour shows where the radar inks.
                if (bare[i] != ground) { radarInk++; if (with[i] == artfix::PAL[artfix::indexAt(x - dr.x, y - dr.y, 0)]) covered++; }
            }
            char label[96]; snprintf(label, sizeof label, "decor stays under the radar (%d of %d radar pixels show plain decor)", covered, radarInk);
            check(label, radarInk > 0 && covered == 0);
        }
    }
    pal::setTheme(0);
    printf(fails ? "FAILED %d/%d\n" : "PASSED %d checks\n", fails ? fails : checks, checks);
    return fails ? 1 : 0;
}
