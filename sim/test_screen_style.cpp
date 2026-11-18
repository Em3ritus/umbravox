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
enum { CODEX = 1, DOMAIN, MENU, MESH, OMENS, ECHOES, ATTUNE, ASPECT, DETAIL, FILES };
static const char* NAME[] = {"scan", "codex", "domain", "menu", "mesh", "omens", "echoes", "attune", "aspect", "detail", "files"};
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
    for (int i = 0; i < 20; i++) j += std::string(i ? "," : "") + "\"" + themepack::ROLE_KEYS[i] + "\":\"" + (i == 0 ? "#000000" : "#C0C0C0") + "\"";
    writeText(d + "/theme.json", j + "}}");
    writeText(d + "/layout.json", "{\"format\":1,\"waveshare\":{\"style\":" + style + "},\"cyd\":{\"style\":" + style + "}}");
    writeFile(d + "/f0.fnt", fontfix::make());
    writeFile(d + "/a4.art", artfix::make(9, 9, 1, false));
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
    check("packs load", titled >= 0 && panels >= 0);
    if (titled < 0 || panels < 0) { printf("FAILED\n"); return 1; }

    const int BW[2] = {480, 320}, BH[2] = {320, 240};
    const int PANEL_SCREENS[] = {CODEX, OMENS, ASPECT, DETAIL, FILES};
    const int TITLE_SCREENS[] = {OMENS, DETAIL};
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
            snprintf(label, sizeof label, "%dx%d %s: the title font is used", W, H, NAME[sc]); check(label, n > 0);
            snprintf(label, sizeof label, "%dx%d %s: title text only inside the content area", W, H, NAME[sc]); check(label, out == 0);
        }
    }
    pal::setTheme(0);
    printf(fails ? "FAILED %d/%d\n" : "PASSED %d checks\n", fails ? fails : checks, checks);
    return fails ? 1 : 0;
}
