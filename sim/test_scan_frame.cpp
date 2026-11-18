// Plan 5c: the scan slice loop is shared code (ui/scan_frame.h) and counts pixels drawn and pushed.
//
// The board (device/engine_adapter.cpp) and the designer's meter (ui/meter.h) both run scanframe::slice; this test
// drives it the way the board does, into a framepresent::Frame, and checks the counts and that what reaches the
// panel is exactly a full render of the scene.
#include "../ui/scan_render.h"
#include "../ui/palette.h"
#include "../ui/frame_present.h"
#include "../ui/scan_present.h"
#include "../ui/scan_frame.h"
#include "../ui/layout_engine.h"
#include "scan_fixture.h"
#include <cstdio>
#include <cstring>
#include <vector>
#include <algorithm>

using namespace scanr;

static int fails = 0;
static void expect(bool c, const char* m) { if (!c) { ++fails; printf("  FAIL %s\n", m); } else printf("  ok   %s\n", m); }

static std::vector<uint16_t> full(int w, int h, const ScanScene& sc) {
    std::vector<uint16_t> px(w * h);
    for (int y = 0; y < h; y += 16) { S s{px.data() + y * w, w, y, std::min(16, h - y)}; composeStrip(s, w, h, sc); }
    return px;
}
static void fill(scanpres::SceneStore& st, uint32_t now) {
    st.begin(pal::TH_UMBRAVOX, now); st.animate(true);
    st.gps({true, 3, 12, 500}); st.status(3, true, 78, Rec::Off); st.counts(1665, 418, 2903);
    st.channel("CH 06", "2.4 GHz");
    for (int i = 0; i < 21; i++) st.setChannel(i, scanfix::CHANNELS[i]);
    for (const auto& b : scanfix::BLIPS) st.addBlip(b.bearing, b.dist, b.band, b.lastSeen);
}

// The board's buffered stage: the three Frame calls, as device/engine_adapter.cpp maps them.
struct FrameStage {
    framepresent::Frame& f;
    void full(int sy, const uint16_t* b, int sh) { f.stage(sy, b, sh); }
    void window(int sy, const uint16_t* b, int sh, int lo, int w) { f.stageWindow(sy, b, sh, lo, w); }
    void region(int sy, const uint16_t* b, int sh, int lo, int w, int id) { f.stageRegion(sy, b, sh, lo, w, id); }
};

static void run(int W, int H) {
    printf("%dx%d\n", W, H);
    pal::setTheme(pal::TH_UMBRAVOX);
    std::vector<uint16_t> front(W * H), back(W * H), panel(W * H, 0), buf(W * 16);
    framepresent::Frame f; f.begin(front.data(), back.data(), W, H);
    scanpres::Engine e; e.reset(W, H); e.enter();
    FrameStage stage{f};
    bool samePanel = true; uint32_t drawn[10]{}, pushed[10]{}, sent = 0;
    for (int k = 0; k < 10; k++) {
        fill(e.back(), 1000 + k * 20);
        e.commit();
        uimeter::Count n;
        while (e.pending()) {
            const auto sl = e.nextSlice(4);
            if (sl.y1 > sl.y0) scanframe::slice(e, e.scene(), W, H, sl.y0, sl.y1, buf.data(), f.valid(), stage, n);
            if (sl.done) sent = f.present(false, [&](uieng::Rect rc, const uint16_t* px, int stride) {
                for (int y = rc.y; y < rc.y + rc.h; y++) for (int x = rc.x; x < rc.x + rc.w; x++) panel[y * W + x] = px[y * stride + x];
            }, 0, H);
        }
        drawn[k] = n.drawn; pushed[k] = sent;
        if (panel != full(W, H, e.scene())) samePanel = false;
    }
    expect(drawn[0] == (uint32_t)(W * H), "frame 0 draws the whole screen");
    expect(pushed[0] == (uint32_t)(W * H), "frame 0 pushes the whole screen");
    bool small = true; for (int k = 2; k < 10; k++) if (drawn[k] >= (uint32_t)(W * H / 4)) small = false;
    expect(small, "sweep-only frames draw under a quarter of the screen");
    bool fewer = true; for (int k = 2; k < 10; k++) if (pushed[k] > drawn[k]) fewer = false;
    expect(fewer, "a frame never pushes more than it drew");
    expect(samePanel, "the panel equals a full render on every frame");
    for (int k = 0; k < 10; k++) printf("    frame %d drawn %u pushed %u\n", k, drawn[k], pushed[k]);

    // Presenting a frame identical to the panel pushes nothing.
    for (int y = 0; y < H; y += 16) { const int sh = std::min(16, H - y); f.stage(y, front.data() + y * W, sh); }
    sent = f.present(false, [](uieng::Rect, const uint16_t*, int) {}, 0, H);
    expect(sent == 0, "an unchanged frame pushes 0 pixels");
}

int main() {
    {   // the FRAMELOG line tools/meterprobe.py parses
        char b[64];
        scanframe::formatFrm(b, sizeof b, 153600, 4800, 31250, 41, -1);
        expect(!strcmp(b, "[frm] d 153600 p 4800 us 31250 gap 41 a -1"), "formatFrm scan line");
        scanframe::formatFrm(b, sizeof b, 1, 2, 3, 4, 2);
        expect(!strcmp(b, "[frm] d 1 p 2 us 3 gap 4 a 2"), "formatFrm alert line");
    }
    run(480, 320);
    run(320, 240);
    printf(fails ? "%d FAILED\n" : "all passed\n", fails);
    return fails ? 1 : 0;
}
