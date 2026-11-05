// Host test: a looping video in place of the radar picture (wide 480x320 panel only).
//   The clip is a small 4-bit grey frame set (two pixels per byte, high nibble first), drawn 2x inside the radar disc
//   under the blips and the sweep. It advances by the scene clock, and the engine repaints the whole radar when the
//   frame changes (a wedge is not enough), so the panel mirror must equal a full render after every frame.
// Build: make -C sim test_radar_video
#include "../ui/scan_render.h"
#include "../ui/palette.h"
#include "../ui/frame_present.h"
#include "../ui/scan_present.h"
#include "../ui/model.h"
#include "scan_fixture.h"
#include <cstdio>
#include <cstring>
#include <vector>
using namespace scanr;

static int fails = 0;
static void expect(bool c, const char* m) { if (!c) { ++fails; printf("  FAIL %s\n", m); } else printf("  ok   %s\n", m); }

static constexpr int VW = 102, VH = 102, VF = 6, VFPS = 12;
static std::vector<uint8_t> g_px;
static RadarVideo makeVideo() {                     // frame k: grey level (3k+1)%16 everywhere, one marker column at x = 10k+20
    const int stride = (VW * VH + 1) / 2;
    g_px.assign(stride * VF, 0);
    for (int f = 0; f < VF; f++) for (int y = 0; y < VH; y++) for (int x = 0; x < VW; x++) {
        const int g = (x == 10 * f + 20 && y > 20 && y < 80) ? 15 : (3 * f + 1) % 16;
        const int i = y * VW + x; uint8_t& b = g_px[f * stride + i / 2];
        b = (i & 1) ? uint8_t((b & 0xF0) | g) : uint8_t((b & 0x0F) | (g << 4));
    }
    return RadarVideo{ VW, VH, VF, VFPS, g_px.data() };
}
static std::vector<uint16_t> full(int w, int h, const ScanScene& sc) {
    std::vector<uint16_t> px(w * h);
    for (int y = 0; y < h; y += 16) { S s{px.data() + y * w, w, y, std::min(16, h - y)}; composeStrip(s, w, h, sc); }
    return px;
}
static void fill(scanpres::SceneStore& st, uint32_t now, const RadarVideo* v, bool animate) {
    st.begin(pal::TH_BAPH0MAU5, now); st.animate(animate); st.sweep(ui::SW_RADAR); st.video(v);
    st.gps({true, 3, 12, 500}); st.status(3, true, 78, Rec::Off); st.counts(1665, 418, 2903);
    st.channel("CH 06", "2.4 GHz");
    for (int i = 0; i < 21; i++) st.setChannel(i, scanfix::CHANNELS[i]);
}
static int diffAll(const std::vector<uint16_t>& a, const std::vector<uint16_t>& b) { int n = 0; for (size_t i = 0; i < a.size(); i++) n += a[i] != b[i]; return n; }

int main() {
    const int W = 480, H = 320;
    pal::setTheme(pal::TH_BAPH0MAU5);
    const RadarVideo v = makeVideo();
    const auto rb = radarBounds(W, H); const int fr = (rb.w - 5) / 2, cx = rb.x + fr + 2, cy = rb.y + fr + 2;
    auto grey = [](int g) { return ui::Canvas::mix(pal::VOID, pal::BRIGHT, g, 15); };

    puts("== the frame follows the clock");
    expect(videoFrame(v, 0) == 0 && videoFrame(v, 1000 / VFPS + 1) == 1 && videoFrame(v, 1000 * VF / VFPS) == 0, "frame = now * fps / 1000, looping");
    expect(videoFrame(v, 4000000000u) >= 0 && videoFrame(v, 4000000000u) < VF, "a large clock stays in range");

    puts("== drawn inside the disc, under everything else, wide only");
    {
        scanpres::SceneStore a, b; fill(a, 5000, nullptr, false); fill(b, 5000, &v, false);
        const auto pa = full(W, H, a.scene()), pb = full(W, H, b.scene());
        int in = 0, outside = 0;
        for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) {
            const int dx = x - cx, dy = y - cy;
            if (pa[y * W + x] == pb[y * W + x]) continue;
            if (dx * dx + dy * dy <= fr * fr) in++; else outside++;
        }
        expect(in > 5000, "the disc is filled with the clip");
        expect(outside == 0, "nothing outside the radar disc changes");
        const int f = videoFrame(v, 5000);
        expect(pb[(cy - 40) * W + (cx - 50)] == grey((3 * f + 1) % 16) || pb[(cy - 40) * W + (cx - 50)] != pa[(cy - 40) * W + (cx - 50)],
               "a pixel inside the disc is the clip's grey");
        const int mx = cx - VW + 2 * (10 * f + 20);                       // the marker column of this frame, 2x
        expect(pb[cy * W + mx] == grey(15), "the clip's own pixels land where 2x scaling puts them");
        scanpres::SceneStore c320; fill(c320, 5000, &v, false);
        scanpres::SceneStore c320n; fill(c320n, 5000, nullptr, false);
        expect(diffAll(full(320, 240, c320.scene()), full(320, 240, c320n.scene())) == 0, "a 320x240 panel ignores the clip");
    }

    puts("== it plays");
    {
        scanpres::SceneStore a, b, c; fill(a, 5000, &v, false); fill(b, 5000 + 20, &v, false); fill(c, 5000 + 1000 / VFPS + 5, &v, false);
        const auto pa = full(W, H, a.scene()), pb = full(W, H, b.scene()), pc = full(W, H, c.scene());
        expect(diffAll(pa, pb) == 0, "inside one frame period the picture does not change");
        expect(diffAll(pa, pc) > 5000, "the next frame is a different picture");
    }

    puts("== blips draw over the clip");
    {
        scanpres::SceneStore a; fill(a, 5000, &v, true);
        for (const auto& bl : scanfix::BLIPS) a.addBlip(bl.bearing, bl.dist, bl.band, bl.lastSeen);
        scanpres::SceneStore b; fill(b, 5000, &v, true);
        expect(diffAll(full(W, H, a.scene()), full(W, H, b.scene())) > 20, "contacts are visible over the video");
    }

    puts("== a clip with its own palette keeps its own colours");
    {
        uint8_t palb[32]; uint16_t want[16];
        for (int i = 0; i < 16; i++) { want[i] = (uint16_t)(0x0841 * (i + 1) ^ 0x1234); palb[2 * i] = (uint8_t)(want[i] & 255); palb[2 * i + 1] = (uint8_t)(want[i] >> 8); }
        RadarVideo cv = v; cv.pal = palb;
        scanpres::SceneStore plain, col; fill(plain, 5000, &v, false); fill(col, 5000, &cv, false);
        const auto pp = full(W, H, plain.scene()), pc = full(W, H, col.scene());
        const int f = videoFrame(cv, 5000), lvl = (3 * f + 1) % 16;
        const int px = cx - 50, py = cy - 40;
        expect(pp[py * W + px] == grey(lvl), "without a palette the grey level is painted in the theme's colours");
        expect(pc[py * W + px] == want[lvl], "with one, the pixel is the palette's colour for that index");
        const int mx = cx - VW + 2 * (10 * f + 20);
        expect(pc[cy * W + mx] == want[15], "and an index 15 pixel is palette entry 15");
        int outside = 0;
        for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) { const int dx = x - cx, dy = y - cy; if (dx * dx + dy * dy > fr * fr && pp[y * W + x] != pc[y * W + x]) outside++; }
        expect(outside == 0, "nothing outside the radar disc changes");
    }

    puts("== engine: a new frame repaints the whole radar, and the panel mirror stays exact");
    {
        std::vector<uint16_t> front(W * H), back(W * H), panel(W * H, 0);
        framepresent::Frame f; f.begin(front.data(), back.data(), W, H);
        scanpres::Engine e; e.reset(W, H); e.enter();
        std::vector<uint16_t> buf(W * 16);
        bool same = true, sawFull = false, sawNarrow = false;
        for (int k = 0; k < 120 && same; k++) {
            const uint32_t now = 2000 + k * 41;
            fill(e.back(), now, &v, true);
            for (const auto& bl : scanfix::BLIPS) e.back().addBlip(bl.bearing, bl.dist, bl.band, bl.lastSeen);
            e.commit();
            uieng::Rect rg[scanpres::Engine::kMaxRegions]; const int nr = e.regions(rg, scanpres::Engine::kMaxRegions);
            if (k > 0 && nr >= 1) {
                bool covers = false; for (int i = 0; i < nr; i++) covers = covers || (rg[i].x <= rb.x && rg[i].y <= rb.y && rg[i].x + rg[i].w >= rb.x + rb.w && rg[i].y + rg[i].h >= rb.y + rb.h);
                if (videoFrame(v, now) != videoFrame(v, now - 41)) { sawFull = sawFull || covers; if (!covers) { printf("  frame %d: new clip frame but no full-radar region\n", k); same = false; } }
                else if (!covers) sawNarrow = true;
            }
            while (e.pending()) {
                const auto sl = e.nextSlice(4);
                for (int sy = sl.y0; sy < sl.y1; sy += 16) {
                    const int sh = std::min(16, sl.y1 - sy);
                    if (nr > 0) { for (int i = 0; i < nr; i++) {
                        if (rg[i].y >= sy + sh || rg[i].y + rg[i].h <= sy) continue;
                        S s{buf.data(), W, sy, sh, rg[i].x, rg[i].x + rg[i].w};
                        composeStrip(s, W, H, e.scene());
                        f.stageRegion(sy, buf.data(), sh, rg[i].x, rg[i].w, i);
                    } }
                    else { S s{buf.data(), W, sy, sh}; composeStrip(s, W, H, e.scene()); f.stage(sy, buf.data(), sh); }
                }
                if (sl.done) f.present(false, [&](uieng::Rect rr, const uint16_t* px, int stride) {
                    for (int y = rr.y; y < rr.y + rr.h; y++) for (int x = rr.x; x < rr.x + rr.w; x++) panel[y * W + x] = px[y * stride + x];
                }, 0, H);
            }
            if (same) same = diffAll(panel, full(W, H, e.scene())) == 0;
        }
        expect(same, "the panel mirror equals a full render after every frame");
        expect(sawFull, "a clip frame change repaints the whole radar");
        expect(sawNarrow, "between clip frames only the sweep repaints");
    }

    printf("%s\n", fails ? "FAILED" : "all passed");
    return fails ? 1 : 0;
}
