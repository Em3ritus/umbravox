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

// The history the board's engine would have built by `now`, replayed from a made-up but deterministic feed: the
// demo's counts grow a little every tick and its channel loads jitter around the demo's values, so the preview's
// sparklines and peak caps are alive and identical in the browser and in the host reference. The last tick uses the
// demo's own loads, so the bars are exactly the ones the rest of the screen draws.
inline void fxHistory(const ui::State& s, uint32_t now, scanr::Fx& out) {
    auto mix = [](uint32_t a, uint32_t b) { uint32_t h = a * 2654435761u ^ (b + 0x9E3779B9u) * 40503u; h ^= h >> 15; h *= 2246822519u; h ^= h >> 13; return h; };
    const uint32_t last = (now + 30000u) / scanr::kFxMs, span = 110, first = last - span;   // +30 s: a board that has been up a while, so the traces are full from frame 0
    long total[3] = { s.nets24, s.nets5, s.ble };
    long before[3];                                  // the counts at tick `first`: walk back from the demo's own totals
    for (int k = 0; k < 3; k++) { before[k] = total[k]; for (uint32_t t = last; t > first; --t) before[k] -= (long)(mix(t, k) % 5); if (before[k] < 0) before[k] = 0; }
    scanpres::FxTracker tr;
    long cnt[3] = { before[0], before[1], before[2] };
    int load[scanr::kChannels];
    for (uint32_t t = first; t <= last; ++t) {
        if (t > first) for (int k = 0; k < 3; k++) cnt[k] += (long)(mix(t, k) % 5);
        for (int i = 0; i < scanr::kChannels; i++) {
            const int base = i < ui::CH24 ? s.load24[i] : s.load5[i - ui::CH24];
            const int j = int(mix(t, 100 + i) % 31u) - 15;
            load[i] = t == last ? base : (base + j < 0 ? 0 : (base + j > 100 ? 100 : base + j));
        }
        tr.advance(t * scanr::kFxMs, cnt, load, out);
    }
}

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
        st.fx(true); fxHistory(s, c.now, st.fxState());
        strips([&](scanr::S& b) { scanr::composeStrip(b, W, H, st.scene()); }); break;
    }
    }
}

} // namespace prevws
