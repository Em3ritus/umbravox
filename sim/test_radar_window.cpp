// Radar-only redraw: composing just the radar's columns must give exactly the same
// pixels as a full compose, on every theme and size, so the sweep can be redrawn
// without touching the rest of the screen.
#include "../ui/scan_render.h"
#include "../ui/palette.h"
#include "../ui/frame_present.h"
#include "../ui/scan_present.h"
#include "scan_fixture.h"
#include <cassert>
#include <cstdio>
#include <vector>
using namespace scanr;

static std::vector<uint16_t> full(int w, int h, const ScanScene& sc) {
    std::vector<uint16_t> px(w * h);
    for (int y = 0; y < h; y += 16) { S s{px.data() + y * w, w, y, std::min(16, h - y)}; composeStrip(s, w, h, sc); }
    return px;
}

// Compose only the columns [lo,hi) of the strip-aligned rows [y0,y1) into a packed buffer.
static void window(int w, int h, const ScanScene& sc, int y0, int y1, int lo, int hi, std::vector<uint16_t>& out) {
    out.assign((hi - lo) * (y1 - y0), 0);
    std::vector<uint16_t> strip((hi - lo) * 16);
    for (int y = y0; y < y1; y += 16) {
        const int n = std::min(16, y1 - y);
        S s{strip.data(), w, y, n, lo, hi};
        composeStrip(s, w, h, sc);
        for (int j = 0; j < n; j++) for (int i = 0; i < hi - lo; i++) out[(y - y0 + j) * (hi - lo) + i] = strip[j * (hi - lo) + i];
    }
}

// Drive the real Engine + Frame the way device/engine_adapter.cpp does and keep a mirror of the "panel".
static void fill(scanpres::SceneStore& st, int theme, uint32_t now, long wifi) {
    st.begin(theme, now); st.animate(true);
    st.gps({true, 3, 12, 500}); st.status(3, true, 78, Rec::Off); st.counts(wifi, 418, 2903);
    st.channel("CH 06", "2.4 GHz");
    for (int i = 0; i < 21; i++) st.setChannel(i, scanfix::CHANNELS[i]);
    for (const auto& b : scanfix::BLIPS) st.addBlip(b.bearing, b.dist, b.band, b.lastSeen);
}
static int pipeline(int theme, int w, int h) {
    pal::setTheme(theme);
    std::vector<uint16_t> front(w * h), back(w * h), panel(w * h, 0);
    framepresent::Frame f; f.begin(front.data(), back.data(), w, h);
    scanpres::Engine e; e.reset(w, h); e.enter();
    std::vector<uint16_t> buf(w * 16);
    int windowed = 0;
    for (int k = 0; k < 40; k++) {
        fill(e.back(), theme, 1000 + k * 41, k < 25 ? 1665 : 1666);        // a count changes at k=25: that frame is full-width
        e.commit();
        int lo = 0, hi = -1; const bool win = e.window(lo, hi) && f.valid();
        if (k >= 1 && k != 25) { assert(win); windowed++; } else assert(!win);   // sweep-only frames use the window, others never do
        while (e.pending()) {
            const auto sl = e.nextSlice(4);
            for (int sy = sl.y0; sy < sl.y1; sy += 16) {
                const int sh = std::min(16, sl.y1 - sy);
                S s = win ? S{buf.data(), w, sy, sh, lo, hi} : S{buf.data(), w, sy, sh};
                composeStrip(s, w, h, e.scene());
                if (win) f.stageWindow(sy, buf.data(), sh, lo, hi - lo); else f.stage(sy, buf.data(), sh);
            }
            if (sl.done) f.present(false, [&](uieng::Rect r, const uint16_t* px, int stride) {
                for (int y = r.y; y < r.y + r.h; y++) for (int x = r.x; x < r.x + r.w; x++) panel[y * w + x] = px[y * stride + x];
            }, sl.y0 < sl.y1 ? 0 : 0, h);
        }
        if (panel != full(w, h, e.scene())) { fprintf(stderr, "theme %d %dx%d frame %d: panel != full render\n", theme, w, h, k); assert(false); }
    }
    return windowed;
}

int main() {
    int checked = 0;
    for (int theme = 0; theme < pal::TH_COUNT; theme++) for (auto wh : {std::pair<int,int>{480,320},{320,240}}) {
        const int w = wh.first, h = wh.second;
        pal::setTheme(theme);
        auto sc = scanfix::demo(theme, 1000); sc.animateSweep = true;
        const auto r = radarBounds(w, h);
        const int y0 = (r.y / 16) * 16, y1 = std::min(h, r.y + r.h), lo = std::max(0, r.x), hi = std::min(w, r.x + r.w);
        auto prev = full(w, h, sc);
        for (int k = 1; k <= 12; k++) {
            auto nx = sc; nx.now = 1000 + k * 137;
            const auto want = full(w, h, nx);
            std::vector<uint16_t> win; window(w, h, nx, y0, y1, lo, hi, win);
            auto got = prev;                                     // previous frame + only the window repainted
            for (int y = y0; y < y1; y++) for (int x = lo; x < hi; x++) got[y * w + x] = win[(y - y0) * (hi - lo) + (x - lo)];
            if (got != want) { fprintf(stderr, "theme %d %dx%d step %d: windowed != full\n", theme, w, h, k); assert(false); }
            prev = want; checked++;
        }
    }
    int windowed = 0;
    for (int theme = 0; theme < pal::TH_COUNT; theme++) for (auto wh : {std::pair<int,int>{480,320},{320,240}}) windowed += pipeline(theme, wh.first, wh.second);
    printf("test_radar_window: %d frames identical; %d pipeline frames used the window and matched the panel\n", checked, windowed);
    return 0;
}
