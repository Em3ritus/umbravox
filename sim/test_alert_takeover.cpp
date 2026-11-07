// Host test: the alert takeover on the wide engine panel (480x320).
//   LOW/MED  -> the hero area (radar + spectrum column) is taken over; everything else is untouched.
//   HIGH     -> the whole screen, header and nav included.
//   A tap anywhere on the alert dismisses it (one hit over the takeover rectangle).
//   Every rite (ward, CVLT summoning, the four muffin rites) draws, animates, and resolves the threat name.
//   The engine repaints exactly what changed (panel mirror == full render) across alert on/off/age/level changes.
//   320x240 panels ignore alerts (pinned).
// Build: make -C sim test_alert_takeover
#include "../ui/scan_render.h"
#include "../ui/palette.h"
#include "../ui/frame_present.h"
#include "../ui/scan_present.h"
#include "../ui/layout_engine.h"
#include "../ui/model.h"
#include "scan_fixture.h"
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
static AlertScene mk(uint8_t conf, int age, uint8_t style = 0) {
    AlertScene a; a.active = true; a.threat = 2; a.conf = conf; a.style = style; a.age = (uint16_t)age;
    std::snprintf(a.label, sizeof a.label, "%s", "DRONE"); std::snprintf(a.name, sizeof a.name, "%s", "AA:BB:CC:DD"); std::snprintf(a.why, sizeof a.why, "%s", "DRONEID");
    return a;
}
struct RiteCase { const char* name; int theme; uint8_t style; };
static const RiteCase kRites[] = { {"ward", pal::TH_UMBRAVOX, 0}, {"summoning", pal::TH_BAPH0MAU5, 0}, {"supernova", pal::TH_COSMICMUFFIN, ui::AL_SUPERNOVA},
    {"vortex", pal::TH_COSMICMUFFIN, ui::AL_VORTEX}, {"comet strike", pal::TH_COSMICMUFFIN, ui::AL_COMET}, {"lock-on", pal::TH_COSMICMUFFIN, ui::AL_LOCKON} };

static void fill(scanpres::SceneStore& st, int theme, uint32_t now, const AlertScene& a) {
    st.begin(theme, now); st.animate(true);
    st.gps({true, 3, 12, 500}); st.status(3, true, 78, Rec::Off); st.counts(1665, 418, 2903);
    st.channel("CH 06", "2.4 GHz");
    for (int i = 0; i < 21; i++) st.setChannel(i, scanfix::CHANNELS[i]);
    for (const auto& b : scanfix::BLIPS) st.addBlip(b.bearing, b.dist, b.band, b.lastSeen);
    st.alert(a);
}
static bool pipeline(const RiteCase& r, int w, int h) {
    pal::setTheme(r.theme);
    std::vector<uint16_t> front(w * h), back(w * h), panel(w * h, 0);
    framepresent::Frame f; f.begin(front.data(), back.data(), w, h);
    scanpres::Engine e; e.reset(w, h); e.enter();
    std::vector<uint16_t> buf(w * 16);
    for (int k = 0; k < 96; k++) {
        // off, LOW opening then settled (long ages), MED, HIGH opening then settled, back to LOW, a different alert, off
        AlertScene a; const int phaseK = k / 12;
        if (phaseK == 1) a = mk(ui::CONF_LOW, (k % 12) * 160, r.style);                 // 0..1760 ms: opens, resolves, settles
        else if (phaseK == 2) a = mk(ui::CONF_MED, 1760 + (k % 12) * 130, r.style);
        else if (phaseK == 3) a = mk(ui::CONF_HIGH, (k % 12) * 230, r.style);           // 0..2530 ms
        else if (phaseK == 4) a = mk(ui::CONF_HIGH, 2530 + (k % 12) * 170, r.style);
        else if (phaseK == 5) a = mk(ui::CONF_LOW, 2000 + (k % 12) * 110, r.style);
        else if (phaseK == 6) { a = mk(ui::CONF_LOW, 2000 + (k % 12) * 110, r.style); std::snprintf(a.name, sizeof a.name, "%s", "11:22:33:44"); if (k % 12 > 5) std::snprintf(a.label, sizeof a.label, "%s", "CAMERA"); }
        fill(e.back(), r.theme, 1000 + k * 97, a);
        e.commit();
        uieng::Rect rg[8]; const int nr = e.regions(rg, 8);
        while (e.pending()) {
            const auto sl = e.nextSlice(4);
            for (int sy = sl.y0; sy < sl.y1; sy += 16) {
                const int sh = std::min(16, sl.y1 - sy);
                if (nr > 0) for (int i = 0; i < nr; i++) {
                    if (rg[i].y >= sy + sh || rg[i].y + rg[i].h <= sy) continue;
                    S s{buf.data(), w, sy, sh, rg[i].x, rg[i].x + rg[i].w};
                    composeStrip(s, w, h, e.scene());
                    f.stageRegion(sy, buf.data(), sh, rg[i].x, rg[i].w, i);
                } else { S s{buf.data(), w, sy, sh}; composeStrip(s, w, h, e.scene()); f.stage(sy, buf.data(), sh); }
            }
            if (sl.done) f.present(false, [&](uieng::Rect rc, const uint16_t* px, int stride) {
                for (int y = rc.y; y < rc.y + rc.h; y++) for (int x = rc.x; x < rc.x + rc.w; x++) panel[y * w + x] = px[y * stride + x];
            }, 0, h);
        }
        if (panel != full(w, h, e.scene())) { fprintf(stderr, "    %s frame %d: panel != full render\n", r.name, k); return false; }
    }
    return true;
}

int main() {
    const int W = 480, H = 320;
    const uieng::Rect hero = uieng::scan(W, H).hero;
    // 1. Which level takes what.
    expect(alertLevel(mk(ui::CONF_LOW, 0)) == 1 && alertLevel(mk(ui::CONF_MED, 0)) == 1, "LOW and MED take the hero area");
    expect(alertLevel(mk(ui::CONF_HIGH, 0)) == 2, "HIGH takes the whole screen");
    { AlertScene off = mk(ui::CONF_HIGH, 0); off.active = false; expect(alertLevel(off) == 0, "no alert, no takeover"); }
    // 2. One tap target: the takeover rectangle dismisses.
    {
        uieng::HitCtx c{}; c.alertTakeover = 2; const uieng::HitList h = uieng::scanHits(W, H, c);
        bool one = h.count == 1 && h.items[0].action == uieng::Action::AlertDismiss && h.items[0].rect.x == 0 && h.items[0].rect.y == 0 && h.items[0].rect.w == W && h.items[0].rect.h == H;
        expect(one, "HIGH: the whole screen is one dismiss target");
        c.alertTakeover = 1; const uieng::HitList g = uieng::scanHits(W, H, c);
        bool dis = false, nav = false; for (int i = 0; i < g.count; i++) { dis |= g.items[i].action == uieng::Action::AlertDismiss && g.items[i].rect.x == hero.x && g.items[i].rect.w == hero.w; nav |= g.items[i].action == uieng::Action::Nav; }
        expect(dis && nav, "LOW/MED: the hero dismisses, the nav keeps working");
        c.alertTakeover = 0; const uieng::HitList z = uieng::scanHits(W, H, c); bool none = true; for (int i = 0; i < z.count; i++) none &= z.items[i].action != uieng::Action::AlertDismiss;
        expect(none, "no alert: no dismiss target");
    }
    // 3. Every rite at every level.
    for (const RiteCase& r : kRites) {
        pal::setTheme(r.theme);
        ScanScene base = scanfix::demo(r.theme); base.animateSweep = true; base.now = 2000;
        const auto plain = full(W, H, base);
        for (uint8_t conf : { uint8_t(ui::CONF_LOW), uint8_t(ui::CONF_HIGH) }) {
            ScanScene a = base; a.alert = mk(conf, 3000, r.style);
            const auto img = full(W, H, a);
            int inside = 0, outside = 0, bright = 0;
            for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) {
                const bool inHero = x >= hero.x && x < hero.x + hero.w && y >= hero.y && y < hero.y + hero.h;
                if (img[y * W + x] != plain[y * W + x]) (inHero ? inside : outside)++;
                if (img[y * W + x] == pal::BRIGHT) bright++;
            }
            char m[120];
            if (conf == ui::CONF_LOW) {
                snprintf(m, sizeof m, "%s LOW: draws in the hero (%d px) and touches nothing outside it (%d px)", r.name, inside, outside);
                expect(inside > 300 && outside == 0, m);
            } else {
                snprintf(m, sizeof m, "%s HIGH: covers the screen, header included (%d px changed)", r.name, inside + outside);
                expect(outside > 20000, m);
            }
            snprintf(m, sizeof m, "%s %s: the threat name has resolved (%d bright px)", r.name, conf == ui::CONF_LOW ? "LOW" : "HIGH", bright);
            expect(bright > 40, m);
            ScanScene b = a; b.alert.age = 3130;
            snprintf(m, sizeof m, "%s %s animates while it holds", r.name, conf == ui::CONF_LOW ? "LOW" : "HIGH");
            expect(full(W, H, b) != img, m);
            ScanScene early = a; early.alert.age = 40;
            snprintf(m, sizeof m, "%s %s opens differently from how it holds", r.name, conf == ui::CONF_LOW ? "LOW" : "HIGH");
            expect(full(W, H, early) != img, m);
        }
    }
    // 4. Compact panels are pinned: an alert changes nothing there.
    {
        pal::setTheme(pal::TH_UMBRAVOX);
        ScanScene a = scanfix::demo(pal::TH_UMBRAVOX); a.animateSweep = true, a.now = 2000; ScanScene b = a; b.alert = mk(ui::CONF_HIGH, 3000);
        expect(full(320, 240, a) == full(320, 240, b), "320x240 panels ignore alerts");
    }
    // 4b. HIGH layout: the threat text sits above and below the rite, never over it (the rite square stays unobstructed).
    for (const RiteCase& r : kRites) {
        pal::setTheme(r.theme);
        ScanScene a = scanfix::demo(r.theme); a.animateSweep = true, a.now = 2000; a.alert = mk(ui::CONF_HIGH, 3000, r.style);
        ScanScene b = a; b.alert.label[0] = 0; b.alert.name[0] = 0; b.alert.why[0] = 0;
        const std::vector<uint16_t> ia = full(W, H, a), ib = full(W, H, b);
        uieng::Rect rg[8]; const int nr = alertRegions(uieng::Rect{0, 0, W, H}, a.alert, r.theme, rg, 8);
        const uieng::Rect sq = rg[0]; int over = 0, textPx = 0;
        for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) if (ia[y * W + x] != ib[y * W + x]) { textPx++; if (x >= sq.x && x < sq.x + sq.w && y >= sq.y && y < sq.y + sq.h) over++; }
        char m[120]; snprintf(m, sizeof m, "%s HIGH: text clear of the rite square (%d of %d text px inside)", r.name, over, textPx);
        expect(textPx > 500 && over == 0, m);
    }
    // 4c. The ward's heptagram must read as a figure on the board, not a ghost: its brightest pixel is at least half the edge colour.
    {
        pal::setTheme(pal::TH_UMBRAVOX);
        ScanScene a = scanfix::demo(pal::TH_UMBRAVOX); a.animateSweep = true, a.now = 2000; a.alert = mk(ui::CONF_HIGH, 3000, 0);
        const std::vector<uint16_t> ia = full(W, H, a);
        uieng::Rect rg[8]; alertRegions(uieng::Rect{0, 0, W, H}, a.alert, pal::TH_UMBRAVOX, rg, 8); const uieng::Rect sq = rg[0];
        int best = 0; for (int y = sq.y; y < sq.y + sq.h; y++) for (int x = sq.x; x < sq.x + sq.w; x++) best = std::max(best, int(ia[y * W + x] >> 11));
        const int edgeR = int(ui::safeEdge(pal::RED, ui::SAFE_HIGH, pal::VOID, pal::PANEL) >> 11);
        char m[100]; snprintf(m, sizeof m, "ward figure brightness: red %d of edge %d", best, edgeR);
        expect(best * 2 >= edgeR, m);
    }
    // 5. The engine repaints exactly what changed.
    for (const RiteCase& r : kRites) { char m[96]; snprintf(m, sizeof m, "%s: dirty regions cover every change across on/off/level/age", r.name); expect(pipeline(r, W, H), m); }
    printf(fails ? "FAILED\n" : "PASSED\n"); return fails ? 1 : 0;
}
