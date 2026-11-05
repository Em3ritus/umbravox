// Host test: the texture and motion of the SCAN side panels on the wide (480x320) panel.
//   - the three count cards get a dot texture, a scanning glint and a sparkline of new sightings per second;
//   - the 2.4/5 GHz spectrum gets a grid, a peak-hold cap with an afterglow, and a pulse under the baseline.
// All of it is opt-in (ScanScene::fx.on) and wide-only, so every other picture stays byte-identical.
// Build: make -C sim test_scan_fx
#include "../ui/scan_render.h"
#include "../ui/palette.h"
#include "../ui/frame_present.h"
#include "../ui/scan_present.h"
#include "../ui/model.h"
#include "scan_fixture.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>
using namespace scanr;

static int fails = 0;
static void expect(bool c, const char* m) { if (!c) { ++fails; printf("  FAIL %s\n", m); } else printf("  ok   %s\n", m); }

static std::vector<uint16_t> full(int w, int h, const ScanScene& sc) {
    std::vector<uint16_t> px(w * h);
    for (int y = 0; y < h; y += 16) { S s{px.data() + y * w, w, y, std::min(16, h - y)}; composeStrip(s, w, h, sc); }
    return px;
}
static void fill(scanpres::SceneStore& st, uint32_t now, bool fx, int w = 480) {
    (void)w;
    st.begin(2, now); st.animate(true); st.sweep(ui::SW_RADAR); st.fx(fx);
    st.gps({true, 3, 12, 500}); st.status(3, true, 78, Rec::Off); st.counts(1665, 418, 2903);
    st.channel("CH 06", "2.4 GHz");
    for (int i = 0; i < 21; i++) st.setChannel(i, scanfix::CHANNELS[i]);
    for (const auto& b : scanfix::BLIPS) st.addBlip(b.bearing, b.dist, b.band, b.lastSeen);
}
// Count the pixels that differ inside r.
static int diffIn(const std::vector<uint16_t>& a, const std::vector<uint16_t>& b, int w, const uieng::Rect& r) {
    int n = 0;
    for (int y = r.y; y < r.y + r.h; y++) for (int x = r.x; x < r.x + r.w; x++) n += a[y * w + x] != b[y * w + x];
    return n;
}
static int diffAll(const std::vector<uint16_t>& a, const std::vector<uint16_t>& b) {
    int n = 0; for (size_t i = 0; i < a.size(); i++) n += a[i] != b[i]; return n;
}
static bool outside(const std::vector<uint16_t>& a, const std::vector<uint16_t>& b, int w, int h, const uieng::Rect* keep, int nk) {
    for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) {
        bool in = false; for (int k = 0; k < nk; k++) in = in || (x >= keep[k].x && x < keep[k].x + keep[k].w && y >= keep[k].y && y < keep[k].y + keep[k].h);
        if (!in && a[y * w + x] != b[y * w + x]) return false;
    }
    return true;
}

int main() {
    const int W = 480, H = 320;
    pal::setTheme(2);
    const auto L = uieng::scan(W, H);
    const auto spec = specRect(W, H);
    const uieng::Rect owned[4] = { L.tiles[0], L.tiles[1], L.tiles[2], spec };

    scanpres::SceneStore off, on;
    fill(off, 5000, false); fill(on, 5000, true);
    const auto pOff = full(W, H, off.scene()), pOn = full(W, H, on.scene());

    puts("== texture is drawn, and only on the side panels");
    expect(diffIn(pOff, pOn, W, L.tiles[0]) > 40 && diffIn(pOff, pOn, W, L.tiles[1]) > 40 && diffIn(pOff, pOn, W, L.tiles[2]) > 40,
           "each count card gains texture");
    expect(diffIn(pOff, pOn, W, spec) > 40, "the spectrum gains grid and baseline");
    expect(outside(pOff, pOn, W, H, owned, 4), "nothing outside the cards and the spectrum changes (radar, header, nav intact)");
    {   // the card text is still on top: the number's pixels are the same bright ink
        const auto& t = L.tiles[0];
        int bright = 0; for (int y = t.y; y < t.y + t.h; y++) for (int x = t.x; x < t.x + t.w; x++) bright += pOn[y * W + x] == pal::BRIGHT;
        expect(bright > 30, "the count digits still draw over the texture");
    }

    puts("== opt-in and wide only");
    {
        scanpres::SceneStore a, b; fill(a, 5000, false); fill(b, 5000, true);
        expect(diffAll(full(W, H, a.scene()), pOff) == 0, "fx off equals the previous picture");
        // 320x240: the flag changes nothing
        const int w2 = 320, h2 = 240;
        expect(diffAll(full(w2, h2, a.scene()), full(w2, h2, b.scene())) == 0, "a 320x240 panel ignores fx");
    }

    puts("== glint and baseline pulse move on the tick, not between ticks");
    {
        scanpres::SceneStore t0, t1, t2; fill(t0, 5000, true); fill(t1, 5000 + 100, true); fill(t2, 5000 + kFxMs, true);
        const auto p0 = full(W, H, t0.scene()), p1 = full(W, H, t1.scene()), p2 = full(W, H, t2.scene());
        const uieng::Rect keep[4] = { L.tiles[0], L.tiles[1], L.tiles[2], spec };
        // t1 differs from t0 only by the radar sweep and markers, never by the side panels
        expect(diffIn(p0, p1, W, L.tiles[0]) + diffIn(p0, p1, W, L.tiles[1]) + diffIn(p0, p1, W, L.tiles[2]) + diffIn(p0, p1, W, spec) == 0,
               "within one tick the side panels do not change");
        expect(diffIn(p0, p2, W, L.tiles[0]) > 0 && diffIn(p0, p2, W, L.tiles[1]) > 0 && diffIn(p0, p2, W, L.tiles[2]) > 0, "next tick: every card's glint moves");
        expect(diffIn(p0, p2, W, spec) > 0, "next tick: the baseline pulse moves");
        (void)keep;
    }

    puts("== sparkline");
    {
        scanpres::SceneStore z, sp; fill(z, 5000, true); fill(sp, 5000, true);
        sp.fxState().spark[0][kSpark - 1] = 40;                          // a burst of new sightings, newest sample
        const auto pz = full(W, H, z.scene()), ps = full(W, H, sp.scene());
        expect(diffIn(pz, ps, W, L.tiles[0]) > 4, "a spike changes the WIFI card");
        expect(diffIn(pz, ps, W, L.tiles[1]) == 0 && diffIn(pz, ps, W, L.tiles[2]) == 0, "...and no other card");
        // the number area (upper part of the card) is untouched: the trace lives under it
        const auto& t = L.tiles[0];
        expect(diffIn(pz, ps, W, {t.x, t.y, t.w, t.h - 20}) == 0, "the trace stays in the bottom of the card");
        // taller sample => taller bar
        scanpres::SceneStore s2; fill(s2, 5000, true); s2.fxState().spark[0][kSpark - 1] = 20; s2.fxState().spark[0][0] = 40;
        const auto p2 = full(W, H, s2.scene());
        int hi = 0, lo = 0;
        for (int y = t.y; y < t.y + t.h; y++) {
            if (p2[y * W + (t.x + 12 + (kSpark - 1) * 3)] != pz[y * W + (t.x + 12 + (kSpark - 1) * 3)]) lo++;
            if (p2[y * W + (t.x + 12)] != pz[y * W + (t.x + 12)]) hi++;
        }
        expect(hi > lo && lo > 0, "scale is relative to the window: the larger sample draws the taller bar");
    }

    puts("== peak hold and afterglow");
    {
        const auto hs = heroSub(uieng::scan(W, H).hero);
        scanpres::SceneStore p, q; fill(p, 5000, true); fill(q, 5000, true);
        const int ch = 5, barH = scanfix::CHANNELS[ch] * hs.barMax / 100;
        q.fxState().peak[ch] = (uint8_t)std::min(100, scanfix::CHANNELS[ch] + 40);
        const auto pp = full(W, H, p.scene()), pq = full(W, H, q.scene());
        const int pitch = (hs.spectrum.w / kChannels >= 2) ? hs.spectrum.w / kChannels : 2;
        const int x = hs.spectrum.x + ch * pitch;
        const int topPeak = hs.barBase - q.fxState().peak[ch] * hs.barMax / 100;
        int above = 0; for (int y = topPeak; y < hs.barBase - barH; y++) above += pp[y * W + x] != pq[y * W + x];
        expect(above > 0, "a peak above the bar draws a cap and an afterglow in the gap");
        expect(pq[topPeak * W + x] != pp[topPeak * W + x], "the cap sits at the peak height");
        int other = 0; for (int y = hs.barBase - hs.barMax - 3; y < hs.barBase; y++) other += pp[y * W + x + 3 * pitch * 4] != pq[y * W + x + 3 * pitch * 4];
        expect(other == 0, "other channels are untouched");
    }

    puts("== tracker: peak decay and per-second sample");
    {
        scanpres::FxTracker tr; Fx out; long c[3] = { 100, 50, 10 };
        int ch[21]; for (int i = 0; i < 21; i++) ch[i] = 10;
        ch[3] = 80;
        tr.advance(10000, c, ch, out);
        expect(out.on && out.peak[3] == 80 && out.peak[4] == 10, "first sample: peaks equal the bars");
        ch[3] = 10;
        tr.advance(10000 + kFxMs, c, ch, out);
        expect(out.peak[3] < 80 && out.peak[3] > 10, "the peak falls slowly after the bar drops");
        tr.advance(10000 + kFxMs * 40, c, ch, out);
        expect(out.peak[3] == 10, "and settles on the bar");
        // a sample lands every kSparkTicks ticks and carries the new count delta
        scanpres::FxTracker t2; Fx o2; long cc[3] = { 100, 50, 10 };
        t2.advance(20000, cc, ch, o2);
        cc[0] = 107;
        t2.advance(20000 + kFxMs * kSparkTicks, cc, ch, o2);
        expect(o2.spark[0][kSpark - 1] == 7 && o2.spark[1][kSpark - 1] == 0, "newest sample = sightings since the last one");
        cc[0] = 90;                                                        // a reset must never underflow
        t2.advance(20000 + kFxMs * kSparkTicks * 2, cc, ch, o2);
        expect(o2.spark[0][kSpark - 1] == 0 && o2.spark[0][kSpark - 2] == 7, "history shifts left; a falling count samples as 0");
        t2.advance(20000 + 3600000, cc, ch, o2);                          // a long sleep restarts the history
        bool zero = true; for (int k = 0; k < kSpark; k++) zero = zero && o2.spark[0][k] == 0;
        expect(zero, "a long clock jump clears the history");
    }

    puts("== engine: a tick repaints the cards and the spectrum, a frame inside a tick does not");
    {
        pal::setTheme(2);
        scanpres::Engine e; e.reset(W, H); e.enter();
        auto go = [&](uint32_t now) { fill(e.back(), now, true); e.commit(); };
        go(1000); while (e.pending()) e.nextSlice(4);
        go(1000 + kFxMs); while (e.pending()) e.nextSlice(4);
        go(1000 + kFxMs + 80);                                             // same tick, only the sweep moved
        uieng::Rect rg[scanpres::Engine::kMaxRegions]; int n = e.regions(rg, scanpres::Engine::kMaxRegions);
        bool touchesCard = false, touchesSpec = false;
        for (int i = 0; i < n; i++) for (int k = 0; k < 3; k++) touchesCard = touchesCard || (rg[i].x == L.tiles[k].x && rg[i].y == L.tiles[k].y);
        for (int i = 0; i < n; i++) touchesSpec = touchesSpec || (rg[i].x == spec.x && rg[i].y == spec.y);
        expect(n >= 1 && !touchesCard && !touchesSpec, "inside a tick only the sweep repaints");
        while (e.pending()) e.nextSlice(4);
        go(1000 + 2 * kFxMs + 10);                                         // a new tick
        n = e.regions(rg, scanpres::Engine::kMaxRegions);
        int cards = 0; touchesSpec = false;
        for (int i = 0; i < n; i++) { for (int k = 0; k < 3; k++) cards += rg[i].x == L.tiles[k].x && rg[i].y == L.tiles[k].y && rg[i].w == L.tiles[k].w && rg[i].h == L.tiles[k].h;
                                       touchesSpec = touchesSpec || (rg[i].x == spec.x && rg[i].y == spec.y && rg[i].w == spec.w && rg[i].h == spec.h); }
        expect(cards == 3 && touchesSpec, "a new tick repaints the three cards and the spectrum, each as its own rectangle");
        expect(n <= scanpres::Engine::kMaxRegions, "within the region budget");

        // The panel mirror stays equal to a full render across many ticks with the real engine + frame.
        std::vector<uint16_t> front(W * H), back(W * H), panel(W * H, 0);
        framepresent::Frame f; f.begin(front.data(), back.data(), W, H);
        scanpres::Engine e2; e2.reset(W, H); e2.enter();
        std::vector<uint16_t> buf(W * 16);
        bool same = true;
        for (int k = 0; k < 90 && same; k++) {
            const uint32_t now = 2000 + k * 67;
            fill(e2.back(), now, true);
            if (k == 40) e2.back().counts(1700, 430, 2950);
            e2.commit();
            uieng::Rect r2[scanpres::Engine::kMaxRegions]; const int nr = e2.regions(r2, scanpres::Engine::kMaxRegions);
            while (e2.pending()) {
                const auto sl = e2.nextSlice(4);
                for (int sy = sl.y0; sy < sl.y1; sy += 16) {
                    const int sh = std::min(16, sl.y1 - sy);
                    if (nr > 0) { for (int i = 0; i < nr; i++) {
                        if (r2[i].y >= sy + sh || r2[i].y + r2[i].h <= sy) continue;
                        S s{buf.data(), W, sy, sh, r2[i].x, r2[i].x + r2[i].w};
                        composeStrip(s, W, H, e2.scene());
                        f.stageRegion(sy, buf.data(), sh, r2[i].x, r2[i].w, i);
                    } }
                    else { S s{buf.data(), W, sy, sh}; composeStrip(s, W, H, e2.scene()); f.stage(sy, buf.data(), sh); }
                }
                if (sl.done) f.present(false, [&](uieng::Rect rr, const uint16_t* px, int stride) {
                    for (int y = rr.y; y < rr.y + rr.h; y++) for (int x = rr.x; x < rr.x + rr.w; x++) panel[y * W + x] = px[y * stride + x];
                }, 0, H);
            }
            same = diffAll(panel, full(W, H, e2.scene())) == 0;
        }
        expect(same, "the panel mirror equals a full render after every frame");
    }

    printf("%s\n", fails ? "FAILED" : "all passed");
    return fails ? 1 : 0;
}
