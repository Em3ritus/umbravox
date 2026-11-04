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
static double g_area = 0, g_full = 0;   // composed region area vs the radar square, summed over sweep-only frames
static int pipeline(int theme, int w, int h) {
    pal::setTheme(theme);
    std::vector<uint16_t> front(w * h), back(w * h), panel(w * h, 0);
    framepresent::Frame f; f.begin(front.data(), back.data(), w, h);
    scanpres::Engine e; e.reset(w, h); e.enter();
    std::vector<uint16_t> buf(w * 16);
    int sweepOnly = 0;
    for (int k = 0; k < 40; k++) {
        fill(e.back(), theme, 1000 + k * 41 + (k % 7 == 3 ? 500 : 0) + (k > 30 ? 700 : 0), k < 25 ? 1665 : 1666);   // some big clock jumps; a count changes at k=25
        e.commit();
        uieng::Rect rg[8]; const int nr = e.regions(rg, 8);
        const auto rb = radarBounds(w, h);
        if (k == 0) assert(nr == 0);                                       // entry: a plain full-width frame
        else if (k == 25) {                                                // WIFI tile + the sweep
            assert(nr == 2 && rg[0].x == uieng::scan(w, h).tiles[0].x && rg[0].w == uieng::scan(w, h).tiles[0].w);
        } else {                                                           // sweep only: ONE narrow region inside the radar square
            if (nr != 1) { fprintf(stderr, "theme %d %dx%d frame %d: %d regions:", theme, w, h, k, nr); for (int i = 0; i < nr; i++) fprintf(stderr, " {%d,%d,%d,%d}", rg[i].x, rg[i].y, rg[i].w, rg[i].h); fprintf(stderr, "\n"); }
            assert(nr == 1 && rg[0].x >= rb.x && rg[0].y >= rb.y && rg[0].x + rg[0].w <= rb.x + rb.w && rg[0].y + rg[0].h <= rb.y + rb.h);
            sweepOnly++;
        }
        double area = 0;
        while (e.pending()) {
            const auto sl = e.nextSlice(4);
            for (int sy = sl.y0; sy < sl.y1; sy += 16) {
                const int sh = std::min(16, sl.y1 - sy);
                if (nr > 0) for (int i = 0; i < nr; i++) {
                    if (rg[i].y >= sy + sh || rg[i].y + rg[i].h <= sy) continue;
                    S s{buf.data(), w, sy, sh, rg[i].x, rg[i].x + rg[i].w};
                    composeStrip(s, w, h, e.scene());
                    f.stageRegion(sy, buf.data(), sh, rg[i].x, rg[i].w); area += double(rg[i].w) * sh;
                } else { S s{buf.data(), w, sy, sh}; composeStrip(s, w, h, e.scene()); f.stage(sy, buf.data(), sh); }
            }
            if (sl.done) f.present(false, [&](uieng::Rect r, const uint16_t* px, int stride) {
                for (int y = r.y; y < r.y + r.h; y++) for (int x = r.x; x < r.x + r.w; x++) panel[y * w + x] = px[y * stride + x];
            }, 0, h);
        }
        if (nr == 1) { g_area += area; g_full += double(((rb.y + rb.h + 15) / 16 * 16 - rb.y / 16 * 16)) * rb.w; }
        if (panel != full(w, h, e.scene())) { fprintf(stderr, "theme %d %dx%d frame %d: panel != full render\n", theme, w, h, k); assert(false); }
    }
    return sweepOnly;
}

// 320x240 panels keep the single column window (no dirty regions).
static double g_cArea = 0, g_cFull = 0;   // composed window area vs the radar square, summed over sweep-only frames
static int pipelineCompact(int theme, int w, int h) {
    pal::setTheme(theme);
    std::vector<uint16_t> front(w * h), back(w * h), panel(w * h, 0);
    framepresent::Frame f; f.begin(front.data(), back.data(), w, h);
    scanpres::Engine e; e.reset(w, h); e.enter();
    std::vector<uint16_t> buf(w * 16);
    int windowed = 0;
    for (int k = 0; k < 40; k++) {
        fill(e.back(), theme, 1000 + k * 41 + (k % 7 == 3 ? 500 : 0) + (k > 30 ? 700 : 0), k < 25 ? 1665 : 1666);   // some big clock jumps: old and new wedge far apart        // a count changes at k=25: that frame is full-width
        e.commit();
        int lo = 0, hi = -1; const bool win = e.window(lo, hi) && f.valid();
        if (k >= 1 && k != 25) { assert(win); windowed++; g_cFull += double(((radarBounds(w, h).y + radarBounds(w, h).h + 15) / 16 * 16 - radarBounds(w, h).y / 16 * 16)) * (radarBounds(w, h).w); } else assert(!win);   // sweep-only frames use the window, others never do
        const auto rb = radarBounds(w, h);
        while (e.pending()) {
            const auto sl = e.nextSlice(4);
            if (win) { g_cArea += double(sl.y1 - sl.y0) * (hi - lo); }
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

// Live data: counts tick, channel loads move, the channel changes, contacts come and go, the header
// changes -- as on the real board. Each frame the engine hands out dirty REGIONS; the panel mirror must
// equal a full render after every frame, and a frame must cost far less than repainting the whole body.
static void fillLive(scanpres::SceneStore& st, int theme, int k) {
    st.begin(theme, 1000 + k * 43); st.animate(true);
    st.gps({true, 3, 12, 500}); st.status(3 + k / 40, true, 78, Rec::Off);
    st.counts(1665 + k / 3, 418 + k / 5, 2903 + k / 2);
    st.channel(k < 20 ? "CH 06" : k < 40 ? "CH 11" : "CH 165", k < 40 ? "2.4 GHz" : "5 GHz");   // the string grows to 6 glyphs
    for (int i = 0; i < 21; i++) st.setChannel(i, (scanfix::CHANNELS[i] + (k / 2) * (i + 1)) % 100);
    for (const auto& b : scanfix::BLIPS) st.addBlip(b.bearing, b.dist, b.band, b.lastSeen);
    if (k % 17 >= 8) st.addBlip((k * 37) % 360, 20 + k % 60, k % 3, 1000 + k * 43 - 500);   // a contact that comes and goes
}
static double g_composed = 0, g_sent = 0; static int g_frames = 0;
static void live(int theme, int w, int h) {
    pal::setTheme(theme);
    std::vector<uint16_t> front(w * h), back(w * h), panel(w * h, 0), buf(w * 16);
    framepresent::Frame f; f.begin(front.data(), back.data(), w, h);
    scanpres::Engine e; e.reset(w, h); e.enter();
    for (int k = 0; k < 60; k++) {
        fillLive(e.back(), theme, k); e.commit();
        uieng::Rect rg[8]; const int nr = e.regions(rg, 8);
        double composed = 0, sent = 0;
        while (e.pending()) {
            const auto sl = e.nextSlice(4);
            for (int sy = sl.y0; sy < sl.y1; sy += 16) {
                const int sh = std::min(16, sl.y1 - sy);
                if (nr > 0 && f.valid()) {
                    for (int i = 0; i < nr; i++) {
                        if (rg[i].y >= sy + sh || rg[i].y + rg[i].h <= sy) continue;
                        S s{buf.data(), w, sy, sh, rg[i].x, rg[i].x + rg[i].w};
                        composeStrip(s, w, h, e.scene());
                        f.stageRegion(sy, buf.data(), sh, rg[i].x, rg[i].w); composed += double(rg[i].w) * sh;
                    }
                } else { S s{buf.data(), w, sy, sh}; composeStrip(s, w, h, e.scene()); f.stage(sy, buf.data(), sh); composed += double(w) * sh; }
            }
            if (sl.done) f.present(false, [&](uieng::Rect r, const uint16_t* px, int stride) {
                sent += double(r.w) * r.h;
                for (int y = r.y; y < r.y + r.h; y++) for (int x = r.x; x < r.x + r.w; x++) panel[y * w + x] = px[y * stride + x];
            }, 0, h);
        }
        if (panel != full(w, h, e.scene())) { fprintf(stderr, "theme %d %dx%d live frame %d: panel != full render\n", theme, w, h, k); assert(false); }
        if (k >= 2) { g_composed += composed; g_sent += sent; g_frames++; }
    }
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
    for (int theme = 0; theme < pal::TH_COUNT; theme++) live(theme, 480, 320);
    fprintf(stderr, "live data, 480x320: composed %.0f px/frame, sent %.0f px/frame (a full-width body is %d px)\n", g_composed / g_frames, g_sent / g_frames, 480 * 216);
    // The old path repainted the whole full-width body (480x216) on any change besides the sweep, and in this
    // scene something else changes nearly every frame. Regions must cost at most about half of that to draw
    // and under a third to send.
    assert(g_composed / g_frames < 0.55 * 480 * 216 && g_sent / g_frames < 0.30 * 480 * 216);
    int windowed = 0;
    for (int theme = 0; theme < pal::TH_COUNT; theme++) { windowed += pipeline(theme, 480, 320); windowed += pipelineCompact(theme, 320, 240); }
    // The wedge-only window composes well under the whole radar square on a sweep-only frame.
    fprintf(stderr, "sweep-only region area = %.0f%% of the radar square (480x320); compact window = %.0f%%\n", 100 * g_area / g_full, 100 * g_cArea / g_cFull);
    assert(g_area < 0.6 * g_full);       // wide: the sweep region
    assert(g_cArea < 0.7 * g_cFull);     // compact: the small radar makes the fixed 8 px halo margin a bigger share
    printf("test_radar_window: %d frames identical; %d pipeline frames used the window and matched the panel\n", checked, windowed);
    return 0;
}
