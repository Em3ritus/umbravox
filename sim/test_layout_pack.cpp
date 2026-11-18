// Host test: a pack's layout.json is read by the loader (sim/packload.h, the same
// themelayout::build the device's themeload.cpp uses), attached to the pack, and
// installed by pal::setTheme. A bad layout skips only the layout, never the theme.
#include "packload.h"
#include "../ui/themelayout.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unistd.h>
#include <sys/stat.h>

static int fails = 0, checks = 0;
static void check(const char* what, bool ok) { ++checks; printf(ok ? "  ok   %s\n" : "  FAIL %s\n", what); if (!ok) ++fails; }

static std::string root;
static void writeFile(const std::string& path, const std::string& body) {
    FILE* f = fopen(path.c_str(), "wb"); fwrite(body.data(), 1, body.size(), f); fclose(f);
}
static std::string themeJson(const char* name) {
    std::string s = std::string("{\"format\":1,\"name\":\"") + name + "\",\"dial\":\"compact\",\"centrepiece\":\"heptagram\","
                    "\"sweeps\":[\"radar\"],\"alert\":\"ward\",\"colors\":{";
    static const char* roles[] = {"void", "panel", "grid", "edge", "edgeLit", "dim", "text", "bright", "witch", "witchHi",
                                  "bone", "sweep", "sweepHi", "live", "liveHi", "magenta", "ice", "green", "amber", "red"};
    for (int i = 0; i < 20; i++) s += std::string(i ? "," : "") + "\"" + roles[i] + "\":\"" + (i == 0 ? "#000000" : "#C0C0C0") + "\"";
    return s + "}}";
}
// A pack folder with theme.json and, unless layout is null, layout.json.
static std::string pack(const char* folder, const char* name, const char* layout) {
    const std::string d = root + "/" + folder;
    mkdir(d.c_str(), 0755);
    writeFile(d + "/theme.json", themeJson(name));
    if (layout) writeFile(d + "/layout.json", layout);
    return d;
}
static std::string rj(uieng::Rect r) { char b[64]; snprintf(b, sizeof b, "[%d,%d,%d,%d]", r.x, r.y, r.w, r.h); return b; }

int main() {
    char tmpl[] = "/tmp/uvlayoutXXXXXX";
    root = mkdtemp(tmpl);
    pal::clearPacks();
    char why[128];

    // STOP and MARK POI swap places on both boards.
    std::string ok = "{\"format\":1";
    for (int b = 0; b < 2; b++) {
        const uieng::Rect st = themelayout::defaultRect((themelayout::Board)b, themelayout::P_STOP);
        const uieng::Rect po = themelayout::defaultRect((themelayout::Board)b, themelayout::P_NAV_POI);
        const uieng::Rect newStop{po.x, po.y, po.w, po.h}, newPoi{po.x + po.w, st.y, st.w, st.h};
        ok += std::string(",\"") + (b ? "cyd" : "waveshare") + "\":{\"stop\":" + rj(newStop) + ",\"nav.poi\":" + rj(newPoi) + "}";
    }
    ok += "}";
    const int id = packload::load(pack("layout-ok", "LAYOUT OK", ok.c_str()).c_str(), why, sizeof why);
    check("pack with a layout loads", id >= 0);
    if (id >= 0) {
        pal::setTheme((uint8_t)id);
        const uieng::Rect po = themelayout::defaultRect(themelayout::B_WAVESHARE, themelayout::P_NAV_POI);
        check("pack with layout installs override", uieng::activeLayout() != nullptr && uieng::chrome(480, 320).stop == po);
        check("the cyd board is installed too",
              uieng::chrome(320, 240).stop == themelayout::defaultRect(themelayout::B_CYD, themelayout::P_NAV_POI));
    }
    pal::setTheme(pal::TH_UMBRAVOX);
    check("back to a built-in clears it", uieng::activeLayout() == nullptr &&
                                          uieng::chrome(480, 320).stop == uieng::builtinChrome(480, 320).stop);

    const int bad = packload::load(pack("layout-bad", "LAYOUT BAD", "{\"format\":1,\"cyd\":{\"menu\":\"hidden\"}}").c_str(), why, sizeof why);
    check("bad layout skips only the layout: the theme loads", bad >= 0);
    check("bad layout reason", strstr(why, "layout.json: menu cannot be hidden") != nullptr);
    if (bad >= 0) { pal::setTheme((uint8_t)bad); check("bad layout installs nothing", uieng::activeLayout() == nullptr); }

    const std::string big = "{\"format\":1,\"note\":\"" + std::string(themelayout::MAX_JSON, 'a') + "\"}";
    const int over = packload::load(pack("layout-big", "LAYOUT BIG", big.c_str()).c_str(), why, sizeof why);
    check("oversize layout.json: the theme loads", over >= 0);
    check("oversize layout.json reason", strstr(why, "layout.json over 16384 bytes") != nullptr);

    const int junk = packload::load(pack("layout-junk", "LAYOUT JUNK", "{\"format\":1,").c_str(), why, sizeof why);
    check("unparsable layout.json: the theme loads, layout skipped", junk >= 0 && strstr(why, "layout.json: ") != nullptr);

    const int none = packload::load(pack("layout-none", "LAYOUT NONE", nullptr).c_str(), why, sizeof why);
    check("no layout.json is fine", none >= 0 && why[0] == 0);
    if (none >= 0) { pal::setTheme((uint8_t)none); check("no layout installs nothing", uieng::activeLayout() == nullptr); }

    // Switching straight from one layout theme to a layout-less pack clears it (Review Focus 5).
    if (id >= 0 && none >= 0) {
        pal::setTheme((uint8_t)id);
        pal::setTheme((uint8_t)none);
        check("layout theme -> plain pack clears the layout", uieng::activeLayout() == nullptr);
    }
    pal::setTheme(pal::TH_UMBRAVOX);
    printf(fails ? "FAILED %d/%d\n" : "PASSED %d checks\n", fails ? fails : checks, checks);
    return fails ? 1 : 0;
}
