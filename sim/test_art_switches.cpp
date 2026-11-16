// Host test: theme.json "heptagram" and "contacts" switches. A pack can hide the drawn heptagram and the contact
// blips, so its own image or video shows clean. Defaults (switches absent) must draw exactly as before.
// Build: make -C sim test_art_switches
#include "../ui/canvas.h"
#include "../ui/palette.h"
#include "../ui/screens.h"
#include "../ui/themepack.h"
#include "../ui/scan_render.h"
#include "../ui/scan_present.h"
#include "scan_fixture.h"
#include <cstdio>
#include <cstring>
#include <vector>
using namespace scanr;
using namespace ui;

static int fails = 0;
static void check(const char* w, bool ok) { printf(ok ? "  ok   %s\n" : "  FAIL %s\n", w); if (!ok) fails++; }

static themepack::Pack mkPack(const char* name, bool hideHep, bool hideCon, themepack::Dial dial) {
    themepack::Pack p; memset(&p, 0, sizeof p);
    snprintf(p.name, sizeof p.name, "%s", name); snprintf(p.header, sizeof p.header, "ART");   // same header in every pack so renders compare
    p.dial = dial; p.centre = themepack::CENTRE_HEPTAGRAM; p.rite = themepack::RITE_WARD; p.sweeps = 0x0001; p.sweep = 0;
    for (int i = 0; i < themepack::N_COLORS; i++) p.color[i] = (uint16_t)(0x2104 + 0x0841 * i);
    p.color[themepack::R_VOID] = 0x0000; p.color[themepack::R_PANEL] = 0x0841;
    p.hideHeptagram = hideHep; p.hideContacts = hideCon;
    return p;
}

// ---- CYD path: ui/screen_scan.cpp drawScan on the 320x240 canvas
static std::vector<uint16_t> cyd(int id, int nContacts) {
    pal::setTheme((uint8_t)id);
    State st; st.set.theme = (uint8_t)id; st.frame = 7; st.nContacts = (uint8_t)nContacts;
    for (int i = 0; i < nContacts; i++) st.contacts[i] = Contact{ (uint16_t)(30 + 47 * i), (uint8_t)(20 + 9 * i), (uint8_t)(i % 3), (uint8_t)(10 * i) };
    Canvas c; c.clear(0);
    drawScan(c, st, nullptr);
    return std::vector<uint16_t>(c.px, c.px + ui::W * ui::H);
}

// ---- wide engine path: ui/scan_render.h composeStrip, optionally with a flat-colour video in the dial
static std::vector<uint8_t> g_vpx;
static RadarVideo flatVideo() {
    const int w = 102, h = 102, f = 2; g_vpx.assign(((w * h + 1) / 2) * f, 0x88);   // grey level 8 everywhere
    return RadarVideo{ w, h, f, 12, g_vpx.data() };
}
static std::vector<uint16_t> wide(int W, int H, int id, int nBlips, const RadarVideo* v) {
    pal::setTheme((uint8_t)id);
    scanpres::SceneStore st; st.begin((uint8_t)id, 5000); st.animate(false); st.sweep(ui::SW_RADAR); st.video(v);
    st.gps({true, 3, 12, 500}); st.status(3, true, 78, Rec::Off); st.counts(1665, 418, 2903); st.channel("CH 06", "2.4 GHz");
    for (int i = 0; i < 21; i++) st.setChannel(i, scanfix::CHANNELS[i]);
    for (int i = 0; i < nBlips; i++) st.addBlip(scanfix::BLIPS[i].bearing, scanfix::BLIPS[i].dist, scanfix::BLIPS[i].band, scanfix::BLIPS[i].lastSeen);
    std::vector<uint16_t> px(W * H);
    for (int y = 0; y < H; y += 16) { S s{px.data() + y * W, W, y, std::min(16, H - y)}; composeStrip(s, W, H, st.scene()); }
    return px;
}
static long countColour(const std::vector<uint16_t>& a, uint16_t col) { long n = 0; for (uint16_t p : a) n += p == col; return n; }

int main() {
    pal::clearPacks();
    const int idDef = pal::addPack(mkPack("ART DEF", false, false, themepack::DIAL_COMPACT), nullptr);
    const int idHep = pal::addPack(mkPack("ART NOHEP", true, false, themepack::DIAL_COMPACT), nullptr);
    const int idCon = pal::addPack(mkPack("ART NOCON", false, true, themepack::DIAL_COMPACT), nullptr);
    const int idBoth = pal::addPack(mkPack("ART NONE", true, true, themepack::DIAL_COMPACT), nullptr);
    check("four packs registered", idDef >= 0 && idHep >= 0 && idCon >= 0 && idBoth >= 0);

    puts("== CYD scan (screen_scan.cpp)");
    {
        // Same palette in every pack, so pixels compare across them.
        const auto def = cyd(idDef, 5), hep = cyd(idHep, 5), con = cyd(idCon, 5), both = cyd(idBoth, 5);
        const auto defNoC = cyd(idDef, 0), hepNoC = cyd(idHep, 0);
        check("default draws something", def != defNoC);
        check("heptagram off changes the dial", hep != def);
        check("contacts off == the same scene with no contacts", con == defNoC);
        check("both off == heptagram off with no contacts", both == hepNoC);
        check("heptagram off keeps its contacts", hep != hepNoC);
        check("rendering is deterministic", cyd(idDef, 5) == def);
    }

    puts("== wide engine radar with a video (480x320)");
    {
        const RadarVideo v = flatVideo();
        const auto def = wide(480, 320, idDef, 6, &v), hep = wide(480, 320, idHep, 6, &v), con = wide(480, 320, idCon, 6, &v);
        const auto defNoB = wide(480, 320, idDef, 0, &v), hepNoB = wide(480, 320, idHep, 0, &v);
        pal::setTheme((uint8_t)idDef);
        const uint16_t line = pal::EDGE_LIT;
        check("default heptagram is drawn over the clip", countColour(def, line) > countColour(hep, line) + 40);
        check("contacts off == no contacts", con == defNoB);
        check("heptagram off keeps contacts", hep != hepNoB);
        check("heptagram off leaves the clip untouched", hep != def);
    }

    puts("== compact engine radar (320x240) honours the switches");
    {
        const auto def = wide(320, 240, idDef, 6, nullptr), hep = wide(320, 240, idHep, 6, nullptr);
        const auto con = wide(320, 240, idCon, 6, nullptr), defNoB = wide(320, 240, idDef, 0, nullptr);
        check("heptagram off changes the radar", hep != def);
        check("contacts off == no contacts", con == defNoB);
    }

    pal::clearPacks();
    printf(fails ? "\n%d FAILED\n" : "\nall passed\n", fails);
    return fails ? 1 : 0;
}
