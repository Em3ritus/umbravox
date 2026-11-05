// Host test: the live sweep styles on the engine (wide panel). Every CYD style except the seal-eye GAZE draws on the
// Waveshare radar, stays inside the radar disc, animates, is independent of strip/column windows, and is covered by the
// dirty region the engine repaints (the panel mirror equals a full render after every frame, for every style).
// Build: make -C sim test_sweep_styles
#include "../ui/scan_render.h"
#include "../ui/palette.h"
#include "../ui/frame_present.h"
#include "../ui/scan_present.h"
#include "../ui/model.h"
#include "scan_fixture.h"
#include <cassert>
#include <cstdio>
#include <vector>
#include <set>
using namespace scanr;

static int fails = 0;
static void expect(bool c, const char* m) { if (!c) { ++fails; printf("  FAIL %s\n", m); } else printf("  ok   %s\n", m); }

static const uint8_t kStyles[] = { ui::SW_PULSE, ui::SW_STAR, ui::SW_CANDLE, ui::SW_GALAXY, ui::SW_ORBIT,
                                   ui::SW_STARFALL, ui::SW_PULSAR, ui::SW_COMET, ui::SW_AURORA };
static const char* name(uint8_t s) {
    switch (s) { case ui::SW_PULSE: return "PULSE"; case ui::SW_STAR: return "STAR"; case ui::SW_CANDLE: return "CANDLE";
        case ui::SW_GALAXY: return "GALAXY"; case ui::SW_ORBIT: return "ORBIT"; case ui::SW_STARFALL: return "STARFALL";
        case ui::SW_PULSAR: return "PULSAR"; case ui::SW_COMET: return "COMET"; case ui::SW_AURORA: return "AURORA"; }
    return "?";
}

static std::vector<uint16_t> full(int w, int h, const ScanScene& sc) {
    std::vector<uint16_t> px(w * h);
    for (int y = 0; y < h; y += 16) { S s{px.data() + y * w, w, y, std::min(16, h - y)}; composeStrip(s, w, h, sc); }
    return px;
}
static void fill(scanpres::SceneStore& st, int theme, uint8_t style, uint32_t now) {
    st.begin(theme, now); st.animate(true); st.sweep(style);
    st.gps({true, 3, 12, 500}); st.status(3, true, 78, Rec::Off); st.counts(1665, 418, 2903);
    st.channel("CH 06", "2.4 GHz");
    for (int i = 0; i < 21; i++) st.setChannel(i, scanfix::CHANNELS[i]);
    for (const auto& b : scanfix::BLIPS) st.addBlip(b.bearing, b.dist, b.band, b.lastSeen);
}

// Drive the real Engine + Frame like device/engine_adapter.cpp and compare the panel mirror to a full render.
static bool pipeline(int theme, uint8_t style, int w, int h, int& maxRegArea) {
    pal::setTheme(theme);
    std::vector<uint16_t> front(w * h), back(w * h), panel(w * h, 0);
    framepresent::Frame f; f.begin(front.data(), back.data(), w, h);
    scanpres::Engine e; e.reset(w, h); e.enter();
    std::vector<uint16_t> buf(w * 16);
    for (int k = 0; k < 60; k++) {
        fill(e.back(), theme, style, 1000 + k * 97 + (k % 7 == 3 ? 500 : 0));
        e.commit();
        uieng::Rect rg[8]; const int nr = e.regions(rg, 8);
        int area = 0;
        while (e.pending()) {
            const auto sl = e.nextSlice(4);
            for (int sy = sl.y0; sy < sl.y1; sy += 16) {
                const int sh = std::min(16, sl.y1 - sy);
                if (nr > 0) for (int i = 0; i < nr; i++) {
                    if (rg[i].y >= sy + sh || rg[i].y + rg[i].h <= sy) continue;
                    S s{buf.data(), w, sy, sh, rg[i].x, rg[i].x + rg[i].w};
                    composeStrip(s, w, h, e.scene());
                    f.stageRegion(sy, buf.data(), sh, rg[i].x, rg[i].w); area += rg[i].w * sh;
                } else { S s{buf.data(), w, sy, sh}; composeStrip(s, w, h, e.scene()); f.stage(sy, buf.data(), sh); }
            }
            if (sl.done) f.present(false, [&](uieng::Rect r, const uint16_t* px, int stride) {
                for (int y = r.y; y < r.y + r.h; y++) for (int x = r.x; x < r.x + r.w; x++) panel[y * w + x] = px[y * stride + x];
            }, 0, h);
        }
        if (k > 0 && area > maxRegArea) maxRegArea = area;
        if (panel != full(w, h, e.scene())) { fprintf(stderr, "    %s theme %d frame %d: panel != full render\n", name(style), theme, k); return false; }
    }
    return true;
}

int main() {
    const int w = 480, h = 320;
    // 1. Each style animates, stays in the disc, and differs from the others.
    std::set<uint64_t> seen;
    const auto rb = radarBounds(w, h); const int fr = (rb.w - 5) / 2, cx = rb.x + fr + 2, cy = rb.y + fr + 2;
    for (uint8_t st : kStyles) {
        pal::setTheme(pal::TH_UMBRAVOX);
        std::vector<uint16_t> a, b;
        for (int pass = 0; pass < 2; pass++) {
            ScanScene sc = scanfix::demo(pal::TH_UMBRAVOX); sc.animateSweep = true; sc.sweep = st; sc.nBlips = 0; sc.now = pass ? 1300 : 400;
            (pass ? b : a) = full(w, h, sc);
        }
        ScanScene off = scanfix::demo(pal::TH_UMBRAVOX); off.animateSweep = true; off.sweep = ui::SW_RADAR; off.nBlips = 0; off.now = 400;
        const auto radar0 = full(w, h, off);
        int moved = 0, outside = 0, vsRadar = 0;
        for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) {
            const int i = y * w + x; moved += a[i] != b[i]; vsRadar += a[i] != radar0[i];
            const int dx = x - cx, dy = y - cy;
            if (dx * dx + dy * dy > fr * fr && a[i] != radar0[i]) outside++;                // differs from the plain radar frame, outside the disc
        }
        uint64_t hsh = 1469598103934665603ull; for (uint16_t p : a) { hsh ^= p; hsh *= 1099511628211ull; }
        char m[96];
        snprintf(m, sizeof m, "%s animates (%d px move between two times)", name(st), moved); expect(moved > 30, m);
        snprintf(m, sizeof m, "%s stays inside the radar disc (%d px outside)", name(st), outside); expect(outside == 0, m);
        snprintf(m, sizeof m, "%s is not the plain radar sweep", name(st)); expect(vsRadar > 100, m);
        snprintf(m, sizeof m, "%s looks different from the other styles", name(st)); expect(seen.insert(hsh).second, m);
    }
    // 2. GAZE needs the seal's eyes: it falls back to the radar sweep rather than drawing nothing.
    {
        pal::setTheme(pal::TH_UMBRAVOX);
        ScanScene g = scanfix::demo(pal::TH_UMBRAVOX), r = g; g.animateSweep = r.animateSweep = true; g.now = r.now = 900;
        g.sweep = ui::SW_GAZE; r.sweep = ui::SW_RADAR;
        expect(full(w, h, g) == full(w, h, r), "GAZE falls back to the radar sweep");
    }
    // 3. The engine repaints exactly what changed: panel mirror == full render, every style, three themes.
    for (uint8_t st : kStyles) {
        bool ok = true; int area = 0;
        for (int th : {int(pal::TH_UMBRAVOX), int(pal::TH_BAPH0MAU5), int(pal::TH_COSMICMUFFIN)}) ok = pipeline(th, st, w, h, area) && ok;
        char m[96]; snprintf(m, sizeof m, "%s: dirty regions cover every changed pixel (largest repaint %d px)", name(st), area);
        expect(ok, m);
    }
    printf(fails ? "FAILED\n" : "PASSED\n"); return fails ? 1 : 0;
}
