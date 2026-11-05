// One frame of the Waveshare 3.5" (480x320) engine UI, for the theme designer's live preview and its host test.
//
// This is the same path the firmware takes: ui::State -> uiproj::<screen> (project.h) -> the screen's Store -> composeStrip
// in 16-row strips. Nothing here draws a pixel itself. The WebAssembly bridge (web/designer/wasm/bridge.cpp) and the
// host reference (sim/ws_preview_png.cpp) both call render(), so web/designer/test/wasm-parity.test.mjs can hold the
// browser to the host byte for byte.
#pragma once
#include "demo.h"
#include "palette.h"
#include "project.h"
#include "scan_present.h"
#include "menu_present.h"
#include "mesh_present.h"
#include "aspect_present.h"
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

namespace prevws {

constexpr int W = 480, H = 320;
enum Screen { SCAN = 0, MESH = 1, ALERT = 2, MENU = 3, ASPECT = 4 };

// theme: palette id (built-in or a loaded pack). sweep < 0: the radar. alertStyle < 0: the theme's own rite (style 0).
// alertConf: ui::CONF_LOW / MED / HIGH. LOW and MED take the hero area, HIGH the whole screen.
// frame * 20 ms is the animation clock and the alert's age, as in the CYD preview.
inline void render(int theme, int screen, int frame, int sweep, int alertStyle, int alertConf, uint16_t* out) {
    const uint8_t themeId = (uint8_t)theme;
    pal::setTheme(themeId);
    const uint32_t f = (uint32_t)frame;
    ui::State s = ui::demoState(f);
    s.set.sweep = sweep >= 0 ? (uint8_t)sweep : (uint8_t)ui::SW_RADAR;
    s.set.theme = themeId;
    s.set.labels = 0;
    s.netId = 0;
    const bool alertOn = screen == ALERT || alertStyle >= 0;
    if (alertOn) {
        s.set.alertStyle = (uint8_t)(alertStyle >= 0 ? alertStyle : 0);
        s.alertActive = true;
        s.alertThreat = ui::TH_DRONE;
        s.alertConf = (uint8_t)alertConf;
        std::snprintf(s.alertWhy, sizeof s.alertWhy, "%s", "DRONEID");
        std::snprintf(s.alertName, sizeof s.alertName, "%s", "60:60:1F:A2:4C:19");
        s.alertAge = (uint16_t)(f * 20);
    }
    ui::clampSettings(s.set);
    s.filter = ui::F_WIFI;

    uiproj::Chrome c;
    c.gps = scanr::GnssFix{true, 3, 12, 500};
    c.rec = scanr::Rec::Off;
    c.hasCell = true;
    c.batt = 78;
    c.now = 1000 + f * 20;
    c.pmuOk = true;

    uint16_t band[W * 16];
    auto strips = [&](auto&& compose) {
        for (int y = 0; y < H; y += 16) {
            scanr::S st{band, W, y, std::min(16, H - y)};
            compose(st);
            std::memcpy(out + y * W, band, sizeof(uint16_t) * W * st.h);
        }
    };
    switch (screen) {
    case MESH: { static mshpres::MeshStore st; uiproj::mesh(st, s, c); strips([&](scanr::S& b) { mshr::composeStrip(b, W, H, st.scene()); }); break; }
    case MENU: { static menpres::MenuStore st; uiproj::menu(st, s, c); strips([&](scanr::S& b) { menr::composeStrip(b, W, H, st.scene()); }); break; }
    case ASPECT: { static asppres::AspectStore st; uiproj::aspect(st, s, c); strips([&](scanr::S& b) { aspr::composeStrip(b, W, H, st.scene()); }); break; }
    default: {
        static scanpres::SceneStore st; uiproj::scan(st, s, c, true); st.animate(true); st.sweep(s.set.sweep);
        strips([&](scanr::S& b) { scanr::composeStrip(b, W, H, st.scene()); }); break;
    }
    }
}

} // namespace prevws
