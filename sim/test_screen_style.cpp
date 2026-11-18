// test_screen_style: a layout theme's style block on the engine screens other than Scan (plan 4): the title font,
// the nine-slice panel picture and the highlight colours, on both boards. Packs are written to a temp folder and
// loaded through sim/packload.h.
#include "packload.h"
#include "../ui/codex_render.h"
#include "../ui/domain_render.h"
#include "../ui/menu_render.h"
#include "../ui/mesh_render.h"
#include "../ui/omens_render.h"
#include "../ui/echoes_render.h"
#include "../ui/attune_render.h"
#include "../ui/aspect_render.h"
#include "../ui/detail_render.h"
#include "../ui/files_render.h"
#include "codex_fixture.h"
#include "domain_fixture.h"
#include "menu_fixture.h"
#include "mesh_fixture.h"
#include "omens_fixture.h"
#include "echoes_fixture.h"
#include "attune_fixture.h"
#include "aspect_fixture.h"
#include "detail_fixture.h"
#include "files_fixture.h"
#include "scan_fixture.h"
#include "../ui/scan_render.h"
#include "fontfix.h"
#include "artfix.h"
#include <stdio.h>
#include <string>
#include <vector>
#include <sys/stat.h>

static int fails = 0, checks = 0;
static void check(const char* what, bool ok) { ++checks; if (!ok) { ++fails; printf("  FAIL %s\n", what); } }

struct FbSink : scanr::Sink {
    int W, H; std::vector<uint16_t> fb;
    FbSink(int w, int h) : W(w), H(h), fb((size_t)w * h, 0) {}
    void strip(int x, int y, int w, int h, const uint16_t* px) override {
        for (int ry = 0; ry < h; ry++) for (int rx = 0; rx < w; rx++) {
            const int gx = x + rx, gy = y + ry;
            if (gx >= 0 && gx < W && gy >= 0 && gy < H) fb[(size_t)gy * W + gx] = px[ry * w + rx];
        }
    }
};
enum { CODEX = 1, DOMAIN, MENU, MESH, OMENS, ECHOES, ATTUNE, ASPECT, DETAIL, FILES, ASPECT_TABS };
static const char* NAME[] = {"scan", "codex", "domain", "menu", "mesh", "omens", "echoes", "attune", "aspect", "detail", "files", "aspect controls"};
static std::vector<uint16_t> render(int screen, uint8_t theme, int W, int H) {
    pal::setTheme(theme);
    FbSink fb(W, H);
    switch (screen) {
    case CODEX:  { static codr::Compositor c; c.renderFull(W, H, codfix::demo(theme), fb); break; }
    case DOMAIN: { static domr::Compositor c; c.renderFull(W, H, domfix::demo(theme, W >= 400), fb); break; }
    case MENU:   { static menr::Compositor c; c.renderFull(W, H, menfix::demo(theme), fb); break; }
    case MESH:   { static mshr::Compositor c; c.renderFull(W, H, mshfix::demo(theme), fb); break; }
    case OMENS:  { static omnr::Compositor c; c.renderFull(W, H, omnfix::demo(theme), fb); break; }
    case ECHOES: { static echr::Compositor c; c.renderFull(W, H, echfix::demo(theme), fb); break; }
    case ATTUNE: { static atnr::Compositor c; c.renderFull(W, H, atnfix::demo(theme, W, H), fb); break; }
    case ASPECT: { static aspr::Compositor c; c.renderFull(W, H, aspfix::demo(theme), fb); break; }
    case DETAIL: { static detr::Compositor c; c.renderFull(W, H, detfix::demo(theme), fb); break; }
    case ASPECT_TABS: { static aspr::Compositor c; auto sc = aspfix::demo(theme); sc.controls = true; c.renderFull(W, H, sc, fb); break; }
    default:     { static flsr::Compositor c; c.renderFull(W, H, flsfix::demo(theme), fb); break; }
    }
    return fb.fb;
}

static std::string root;
static void writeFile(const std::string& path, const std::vector<uint8_t>& b) {
    FILE* f = fopen(path.c_str(), "wb"); fwrite(b.data(), 1, b.size(), f); fclose(f);
}
static void writeText(const std::string& path, const std::string& s) { writeFile(path, std::vector<uint8_t>(s.begin(), s.end())); }
static int loadPack(const char* folder, const char* name, const std::string& style) {
    const std::string d = root + "/" + folder;
    mkdir(d.c_str(), 0755);
    std::string j = std::string("{\"format\":1,\"name\":\"") + name + "\",\"dial\":\"compact\",\"centrepiece\":\"heptagram\","
                    "\"sweeps\":[\"radar\"],\"alert\":\"ward\",\"colors\":{";
    for (int i = 0; i < 20; i++) j += std::string(i ? "," : "") + "\"" + themepack::ROLE_KEYS[i] + "\":\"" + (i == 0 ? "#000000" : i == themepack::R_BRIGHT ? "#FFFFFF" : "#C0C0C0") + "\"";
    writeText(d + "/theme.json", j + "}}");
    writeText(d + "/layout.json", "{\"format\":1,\"waveshare\":{\"style\":" + style + "},\"cyd\":{\"style\":" + style + "}}");
    writeFile(d + "/f0.fnt", fontfix::make());
    writeFile(d + "/a4.art", artfix::make(9, 9, 1, false));
    writeFile(d + "/a5.art", artfix::make(20, 20, 1, false));    // a menu icon that fits a Waveshare tile
    writeFile(d + "/a6.art", artfix::make(20, 200, 1, false));   // a menu icon no tile has room for
    char why[160];
    const int id = packload::load(d.c_str(), why, sizeof why);
    if (id < 0 || why[0]) printf("  load %s: %d %s\n", folder, id, why);
    return id;
}

static const uint16_t MAG = 0xF81F, CYAN = 0x07FF;
static bool in(uieng::Rect r, int x, int y) { return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h; }

// Pixels that differ between two frames: how many, and how many lie outside `area`.
static void diff(const std::vector<uint16_t>& a, const std::vector<uint16_t>& b, int W, int H, uieng::Rect area, int& n, int& out) {
    n = out = 0;
    for (int y = 0; y < H; y++) for (int x = 0; x < W; x++)
        if (a[(size_t)y * W + x] != b[(size_t)y * W + x]) { n++; if (!in(area, x, y)) out++; }
}
static int count(const std::vector<uint16_t>& a, uint16_t c) { int n = 0; for (uint16_t v : a) n += v == c; return n; }

int main() {
    char tmpl[] = "/tmp/uvscreenXXXXXX";
    root = mkdtemp(tmpl);
    pal::clearPacks();
    const int titled = loadPack("titled", "TITLED", "{\"title\":\"f0\"}");
    const int panels = loadPack("panels", "PANELS", "{\"panel\":\"a4\",\"panelSlice\":[3,3,3,3],\"highlight\":{\"fill\":\"#ff00ff\",\"edge\":\"#00ffff\"}}");
    const int icons = loadPack("icons", "ICONS", "{\"icons\":[\"a5\",null,\"a6\"]}");
    check("packs load", titled >= 0 && panels >= 0 && icons >= 0);
    if (titled < 0 || panels < 0 || icons < 0) { printf("FAILED\n"); return 1; }

    const int BW[2] = {480, 320}, BH[2] = {320, 240};
    const int PANEL_SCREENS[] = {CODEX, OMENS, ASPECT, DETAIL, FILES, DOMAIN, MESH, MENU, ECHOES, ATTUNE, ASPECT_TABS};
    const int TITLE_SCREENS[] = {OMENS, DETAIL, DOMAIN, MESH, ECHOES};
    for (int b = 0; b < 2; b++) {
        const int W = BW[b], H = BH[b];
        char label[128];
        pal::setTheme((uint8_t)panels);
        const uieng::Rect body = uieng::chrome(W, H).body;
        for (int sc : PANEL_SCREENS) {
            assets::clear((uint8_t)panels);
            const auto bare = render(sc, (uint8_t)panels, W, H);
            packload::useMedia(panels);
            const auto styled = render(sc, (uint8_t)panels, W, H);
            int n, out; diff(bare, styled, W, H, body, n, out);
            snprintf(label, sizeof label, "%dx%d %s: the panel picture is drawn (%d px)", W, H, NAME[sc], n); check(label, n > 100);
            snprintf(label, sizeof label, "%dx%d %s: only inside the content area (%d outside)", W, H, NAME[sc], out); check(label, out == 0);
            if (sc == ASPECT_TABS || sc == MESH) {   // the active tab's underline takes the highlight edge
                snprintf(label, sizeof label, "%dx%d %s: the highlight edge on the active tab", W, H, NAME[sc]);
                check(label, count(styled, CYAN) > 0);
            }
            if (sc == CODEX || sc == FILES) {
                snprintf(label, sizeof label, "%dx%d %s: the highlight colours", W, H, NAME[sc]);
                check(label, count(styled, MAG) > 0 && count(styled, CYAN) > 0);
            }
        }
        for (int sc : TITLE_SCREENS) {
            assets::clear((uint8_t)titled);
            const auto bare = render(sc, (uint8_t)titled, W, H);
            packload::useMedia(titled);
            const auto styled = render(sc, (uint8_t)titled, W, H);
            int n, out; diff(bare, styled, W, H, body, n, out);
            const uieng::Rect wm = uieng::chrome(W, H).wordmark;   // the wordmark is title text too
            int outBoth = 0;
            for (int y = 0; y < H; y++) for (int x = 0; x < W; x++)
                if (bare[(size_t)y * W + x] != styled[(size_t)y * W + x] && !in(body, x, y) && !in(wm, x, y)) outBoth++;
            out = outBoth;
            snprintf(label, sizeof label, "%dx%d %s: the title font is used (%d px, font %p)", W, H, NAME[sc], n, (const void*)assets::font(0)); check(label, n > 0);
            snprintf(label, sizeof label, "%dx%d %s: title text only inside the content area and the wordmark", W, H, NAME[sc]); check(label, out == 0);
        }
    }
    // Menu icons: drawn in a tile with room, never in a tile without, nothing for a null slot.
    for (int b = 0; b < 2; b++) {
        const int W = BW[b], H = BH[b];
        char label[128];
        pal::setTheme((uint8_t)icons);
        const uieng::MenuLayout ML = uieng::menu(W, H);
        assets::clear((uint8_t)icons);
        const auto bare = render(MENU, (uint8_t)icons, W, H);
        packload::useMedia(icons);
        const auto styled = render(MENU, (uint8_t)icons, W, H);
        int n0, out0, n1, out1, n2, out2;
        diff(bare, styled, W, H, ML.cells[0], n0, out0);
        diff(bare, styled, W, H, ML.cells[1], n1, out1);
        diff(bare, styled, W, H, ML.cells[2], n2, out2);
        const int in0 = n0 - out0, in1 = n1 - out1, in2 = n2 - out2;   // diff() counts the whole frame; minus outside = inside
        snprintf(label, sizeof label, "%dx%d menu: icons change nothing outside tile 0", W, H); check(label, out0 == 0);
        if (W >= 400) { snprintf(label, sizeof label, "480x320 menu: the icon is drawn in tile 0 (%d px)", in0); check(label, in0 > 100); }
        snprintf(label, sizeof label, "%dx%d menu: a null icon slot draws nothing", W, H); check(label, in1 == 0);
        snprintf(label, sizeof label, "%dx%d menu: a tile without room draws no icon", W, H); check(label, in2 == 0);
    }
    // A HIGH alert after a styled screen draws exactly as after a fresh start, in the built-in 5x7 (no stale role font,
    // no clip left over from the other screen's content area, no font pointer kept across frames).
    {
        const int bodied = loadPack("bodied", "BODIED", "{\"body\":\"f0\",\"title\":\"f0\"}");
        check("body-font pack loads", bodied >= 0);
        if (bodied >= 0) {
            pal::setTheme((uint8_t)bodied); packload::useMedia(bodied);
            scanr::ScanScene al = scanfix::demo((uint8_t)bodied);
            al.alert.active = true; al.alert.conf = ui::CONF_HIGH; al.alert.age = 4000;
            snprintf(al.alert.label, sizeof al.alert.label, "DRONE"); snprintf(al.alert.name, sizeof al.alert.name, "Aa Aa");
            snprintf(al.alert.why, sizeof al.alert.why, "Aa");
            auto takeover = [&]() { FbSink fb(480, 320); static scanr::Compositor c; c.renderFull(480, 320, al, fb); return fb.fb; };
            assets::clear((uint8_t)bodied);
            const auto plain = takeover();                 // no pack font anywhere: the built-in 5x7
            packload::useMedia(bodied);
            (void)render(MENU, (uint8_t)bodied, 480, 320);   // a styled screen leaves its fonts and clip behind
            const auto afterMenu = takeover();
            check("HIGH alert after a styled screen equals the alert with no pack font", afterMenu == plain);
            (void)render(OMENS, (uint8_t)bodied, 480, 320);
            assets::clear((uint8_t)bodied);                 // the device clears the registry when the theme's media goes
            const auto afterClear = takeover();
            check("HIGH alert after the registry is cleared draws the same", afterClear == plain);
        }
    }
    pal::setTheme(0);
    printf(fails ? "FAILED %d/%d\n" : "PASSED %d checks\n", fails ? fails : checks, checks);
    return fails ? 1 : 0;
}
