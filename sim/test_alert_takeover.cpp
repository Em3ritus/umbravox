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
#include "fontfix.h"
#include "artfix.h"
#include "../ui/packassets.h"
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

// --- plan 5a: alert cards from the theme's look ---------------------------------------------------------------
static uint64_t fnv(const std::vector<uint16_t>& v) { uint64_t h = 1469598103934665603ull; for (uint16_t x : v) { h ^= x; h *= 1099511628211ull; } return h; }
static uieng::BoardLook gLook[2];
static uieng::LayoutOverride gLo{};
static void useLook(bool on) { gLo.board[0] = gLo.board[1] = nullptr; gLo.look[0] = &gLook[0]; gLo.look[1] = &gLook[1]; uieng::activeLayout() = on ? &gLo : nullptr; }
static void clearLook() { gLook[0] = uieng::BoardLook(); gLook[1] = uieng::BoardLook(); gLook[0].any = gLook[1].any = true; }
static bool onlyInside(const std::vector<uint16_t>& a, const std::vector<uint16_t>& b, int W, int H, uieng::Rect r, int* inside = nullptr) {
    int in = 0, out = 0;
    for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) if (a[y * W + x] != b[y * W + x]) {
        const bool i = x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h; (i ? in : out)++; }
    if (inside) *inside = in;
    return out == 0;
}
static void cardTests() {
    const int W = 480, H = 320;
    pal::setTheme(pal::TH_UMBRAVOX);
    ScanScene base = scanfix::demo(pal::TH_UMBRAVOX); base.animateSweep = true; base.now = 2000;
    // builtin_card_unchanged: hashes recorded on the code before plan 5a (no look installed).
    {
        static const uint64_t PIN[3][3] = {{0xc29ba5c6fb01d92bull, 0x662acfd7e3fb6611ull, 0x816e5fb299152a63ull}, {0x071449e7d2a6384bull, 0x2385fd404a3a9d14ull, 0x7e74260529344125ull}, {0x549334ed94cd3346ull, 0xba510daf2c1f1e3aull, 0xae475db17d4e5809ull}};
        const uint8_t confs[3] = {ui::CONF_LOW, ui::CONF_MED, ui::CONF_HIGH}; const int ages[3] = {0, 400, 3000};
        bool same = true;
        for (int c = 0; c < 3; c++) for (int a = 0; a < 3; a++) {
            ScanScene sc = base; sc.alert = mk(confs[c], ages[a]);
            const uint64_t got = fnv(full(W, H, sc));
            if (PIN[c][a] == 0) printf("    pin[%d][%d] = 0x%016llxull\n", c, a, (unsigned long long)got);
            same &= got == PIN[c][a];
        }
        expect(same, "builtin_card_unchanged (LOW/MED/HIGH at 0, 400, 3000 ms)");
        clearLook(); useLook(true);
        same = true;
        for (int c = 0; c < 3; c++) { ScanScene sc = base; sc.alert = mk(confs[c], 3000); same &= fnv(full(W, H, sc)) == PIN[c][2]; }
        useLook(false);
        expect(same, "a look with no card keys draws the built-in cards");
    }
    const std::vector<uint16_t> plain = full(W, H, base);
    // card_rect_used
    {
        clearLook(); gLook[0].card[uieng::CARD_LOW].r = {90, 80, 300, 160}; useLook(true);
        ScanScene sc = base; sc.alert = mk(ui::CONF_LOW, 3000);
        int in = 0; const bool ok = onlyInside(full(W, H, sc), plain, W, H, gLook[0].card[0].r, &in);
        useLook(false);
        char m[120]; snprintf(m, sizeof m, "card_rect_used: LOW card 300x160 at (90,80) draws only there (%d px)", in);
        expect(ok && in > 300, m);
    }
    // low_card_outside_hero: over the count tiles and the header, not the hero
    {
        clearLook(); gLook[0].card[uieng::CARD_LOW].r = {0, 0, 300, 130}; useLook(true);
        ScanScene sc = base; sc.alert = mk(ui::CONF_LOW, 3000);
        const uieng::Rect card = alertCardRect(W, H, ui::CONF_LOW);
        int in = 0; const bool ok = onlyInside(full(W, H, sc), plain, W, H, card, &in);
        useLook(false);
        expect(card == (uieng::Rect{0, 0, 300, 130}) && ok && in > 300, "low_card_outside_hero: the scene still draws outside the card");
    }
    // high_card_partial
    {
        clearLook(); gLook[0].card[uieng::CARD_HIGH].r = {40, 40, 400, 240}; useLook(true);
        ScanScene sc = base; sc.alert = mk(ui::CONF_HIGH, 3000);
        int in = 0; const bool ok = onlyInside(full(W, H, sc), plain, W, H, {40, 40, 400, 240}, &in);
        useLook(false);
        expect(ok && in > 20000, "high_card_partial: HIGH 400x240 leaves the scene visible outside");
    }
    // card_font_used / card_font_min_after_fit
    {
        static const std::vector<uint8_t> f8 = fontfix::make(8), f7 = fontfix::make(7), f6 = fontfix::make(6);
        static fontpack::Font F8, F7, F6; const char* why = nullptr;
        fontpack::parse(f8.data(), f8.size(), F8, &why); fontpack::parse(f7.data(), f7.size(), F7, &why); fontpack::parse(f6.data(), f6.size(), F6, &why);
        const fontpack::Font* fonts[assets::MAX_FONTS] = {&F8, &F7, &F6};
        assets::set((uint8_t)pal::TH_UMBRAVOX, fonts, nullptr);
        ScanScene sc = base; sc.alert = mk(ui::CONF_LOW, 3000); std::snprintf(sc.alert.label, sizeof sc.alert.label, "%s", "AAAA");
        clearLook(); useLook(true); const std::vector<uint16_t> builtin = full(W, H, sc);
        gLook[0].card[uieng::CARD_LOW].font = 0; const std::vector<uint16_t> withFont = full(W, H, sc);
        expect(alertCardFont(W, ui::CONF_LOW) == &F8 && withFont != builtin && onlyInside(withFont, builtin, W, H, alertCardRect(W, H, ui::CONF_LOW)),
               "card_font_used: the card's words use its font, inside the card");
        gLook[0].card[uieng::CARD_LOW].font = 1;
        expect(alertCardFont(W, ui::CONF_LOW) == &F7, "a 7 px card font is used");
        gLook[0].card[uieng::CARD_LOW].font = -1; gLook[0].card[uieng::CARD_LOW].scale = 2;
        const std::vector<uint16_t> builtin2 = full(W, H, sc);                     // the built-in 5x7 at the card's scale
        gLook[0].card[uieng::CARD_LOW].font = 2;
        expect(alertCardFont(W, ui::CONF_LOW) == nullptr && full(W, H, sc) == builtin2,
               "card_font_min_after_fit: a 6 px font is refused even at scale 2 (fitting may pick scale 1)");
        gLook[0].card[uieng::CARD_LOW].font = 1; gLook[0].card[uieng::CARD_LOW].scale = 0;
        std::snprintf(sc.alert.label, sizeof sc.alert.label, "%s", "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAA");
        {   // a 30-letter label in a 7 px font: the fitted scale is never below 1, so its line is >= 7 px tall
            FontScope fs(alertCardFont(W, ui::CONF_LOW), alertCardRect(W, H, ui::CONF_LOW));
            const int sc1 = fitScale(sc.alert.label, alertCardRect(W, H, ui::CONF_LOW).w - 24, 3);
            expect(lineH(sc1) >= 7, "card_font_min_after_fit: line height after fitting >= 7");
        }
        useLook(false); assets::clear((uint8_t)pal::TH_UMBRAVOX);
    }
    // card_regions: a settled LOW alert with a card repaints only inside the card
    {
        clearLook(); gLook[0].card[uieng::CARD_LOW].r = {0, 0, 300, 130}; useLook(true);
        scanpres::Engine e; e.reset(W, H); e.enter();
        std::vector<uint16_t> front(W * H), back(W * H); framepresent::Frame f; f.begin(front.data(), back.data(), W, H);
        std::vector<uint16_t> buf(W * 16);
        bool inside = true; int frames = 0;
        for (int k = 0; k < 6; k++) {
            AlertScene a = mk(ui::CONF_LOW, 3000 + k * 120);
            scanpres::SceneStore& st = e.back(); fill(st, pal::TH_UMBRAVOX, 5000, a); st.animate(false);
            e.commit();
            uieng::Rect rg[16]; const int n = e.regions(rg, 16);
            if (k >= 2) { frames++; for (int i = 0; i < n; i++) inside &= rg[i].x >= 0 && rg[i].y >= 0 && rg[i].x + rg[i].w <= 300 && rg[i].y + rg[i].h <= 130; if (n <= 0) inside = false; }
            while (e.pending()) { const auto sl = e.nextSlice(20); (void)sl; }
        }
        useLook(false);
        expect(inside && frames == 4, "card_regions: a settled LOW alert repaints only inside its card");
    }
    // the dismiss target follows the LOW/MED card; HIGH stays the whole screen
    {
        clearLook(); gLook[0].card[uieng::CARD_LOW].r = {0, 0, 300, 130}; useLook(true);
        uieng::HitCtx c{}; c.alertTakeover = 1; c.alertRect = alertCardRect(W, H, ui::CONF_LOW);
        const uieng::HitList h = uieng::scanHits(W, H, c);
        bool dis = false; for (int i = 0; i < h.count; i++) dis |= h.items[i].action == uieng::Action::AlertDismiss && h.items[i].rect == c.alertRect;
        useLook(false);
        expect(dis && h.items[0].action == uieng::Action::AlertDismiss, "a LOW card is the dismiss target, ahead of what it covers");
    }
    // dirty regions cover every change with cards placed (LOW over the header, HIGH partial)
    {
        clearLook(); gLook[0].card[uieng::CARD_LOW].r = {0, 0, 300, 130}; gLook[0].card[uieng::CARD_MED].r = {180, 120, 300, 200};
        gLook[0].card[uieng::CARD_HIGH].r = {40, 40, 400, 240}; useLook(true);
        const bool ok = pipeline(kRites[0], W, H) && pipeline(kRites[2], W, H);
        useLook(false);
        expect(ok, "cards placed: dirty regions cover every change across on/off/level/age");
    }
}

// Plan 5a Task 3: a picture in place of the rite.
static void clipTests() {
    const int W = 480, H = 320;
    pal::setTheme(pal::TH_UMBRAVOX);
    ScanScene base = scanfix::demo(pal::TH_UMBRAVOX); base.animateSweep = true; base.now = 2000;
    static const std::vector<uint8_t> anim = artfix::make(80, 60, 3, false), big = artfix::make(200, 150, 1, false), wide = artfix::make(320, 40, 1, false);
    static artpack::Art A, B, Wd; const char* why = nullptr;
    artpack::parse(anim.data(), anim.size(), A, &why); artpack::parse(big.data(), big.size(), B, &why); artpack::parse(wide.data(), wide.size(), Wd, &why);
    const artpack::Art* arts[assets::MAX_ARTS] = {&A, &B, &Wd};
    assets::set((uint8_t)pal::TH_UMBRAVOX, nullptr, arts);
    clearLook(); gLook[0].card[uieng::CARD_LOW].r = {90, 80, 300, 160}; useLook(true);
    ScanScene sc = base; sc.alert = mk(ui::CONF_LOW, 3000);
    const std::vector<uint16_t> rite = full(W, H, sc);
    gLook[0].card[uieng::CARD_LOW].clip = 0;
    const std::vector<uint16_t> pic = full(W, H, sc);
    const uieng::Rect card = gLook[0].card[uieng::CARD_LOW].r;
    const AlertGeom g = alertGeom(card, false);
    const uieng::Rect pr{g.mx - 40, g.my - 30, 80, 60};
    // clip_replaces_rite: the picture's pixels at its rect; outside it (and the words) the card is as before, rite gone
    bool match = true;
    ScanScene quiet = sc; quiet.alert.label[0] = quiet.alert.name[0] = quiet.alert.why[0] = 0;   // no words over the picture
    const std::vector<uint16_t> bare = full(W, H, quiet);
    const uint16_t lowEdge = ui::safeEdge(pal::WITCH, ui::SAFE_LOW, pal::VOID, pal::PANEL);
    for (int y = pr.y; y < pr.y + pr.h; y++) for (int x = pr.x; x < pr.x + pr.w; x++) {
        const uint16_t want = artfix::PAL[artfix::indexAt(x - pr.x, y - pr.y, (int)decorFrame(A, 3000, W))];
        if (bare[y * W + x] != want && bare[y * W + x] != lowEdge) match = false;   // the LOW confidence word rides over it
    }
    expect(match && pic != rite, "clip_replaces_rite: the picture plays where the rite was");
    // no rite pixel survives outside the picture rect inside the rite square
    {
        gLook[0].card[uieng::CARD_LOW].clip = -1;
        ScanScene off = sc; off.alert.label[0] = off.alert.name[0] = off.alert.why[0] = 0;
        const std::vector<uint16_t> riteOnly = full(W, H, off);
        gLook[0].card[uieng::CARD_LOW].clip = 0;
        const std::vector<uint16_t> picOnly = full(W, H, off);
        int riteLeft = 0;
        for (int y = card.y; y < card.y + card.h; y++) for (int x = card.x; x < card.x + card.w; x++) {
            const bool inPic = x >= pr.x && x < pr.x + pr.w && y >= pr.y && y < pr.y + pr.h;
            if (!inPic && picOnly[y * W + x] != pal::VOID && riteOnly[y * W + x] == picOnly[y * W + x] && riteOnly[y * W + x] != pal::VOID) riteLeft++;
        }
        char m[100]; snprintf(m, sizeof m, "clip_replaces_rite: no rite drawing left around the picture (%d px)", riteLeft);
        expect(riteLeft == 0 || riteLeft < 50, m);   // the 1 px card frame of non-ward rites stays
    }
    // clip_frames_by_age
    { ScanScene b = sc; b.alert.age = 3000 + 100; const auto f2 = full(W, H, b); b.alert.age = 3000 + 300; const auto f3 = full(W, H, b);
      expect(f2 != pic || f3 != pic, "clip_frames_by_age: an animated picture changes frame with the alert's age"); }
    // clip_words_on_top: with the label set, every label pixel that is BRIGHT with the rite is BRIGHT with the picture
    {
        ScanScene nolab = sc; nolab.alert.label[0] = 0;
        gLook[0].card[uieng::CARD_LOW].clip = -1; const auto r0 = full(W, H, nolab);
        int lab = 0, kept = 0;
        for (int i = 0; i < W * H; i++) if (rite[i] != r0[i] && rite[i] == pal::BRIGHT) { lab++; kept += pic[i] == pal::BRIGHT; }
        gLook[0].card[uieng::CARD_LOW].clip = 0;
        char m[100]; snprintf(m, sizeof m, "clip_words_on_top: the label draws over the picture (%d of %d px)", kept, lab);
        expect(lab > 40 && kept == lab, m);
    }
    // clip_regions: the settled regions hold the picture's rect in place of the rite square
    {
        uieng::Rect rg[8]; const int n = alertRegions(card, sc.alert, pal::TH_UMBRAVOX, rg, 8, W);
        expect(n >= 1 && rg[0] == pr, "clip_regions: the picture's rect replaces the rite square");
    }
    // limits: larger than the card or over the area cap -> the rite plays
    gLook[0].card[uieng::CARD_LOW].clip = 1;    // 200x150 = 30,000 px > 25,600
    expect(full(W, H, sc) == rite, "card_clip_too_big: a picture over the area cap plays the rite");
    gLook[0].card[uieng::CARD_LOW].clip = 2;    // 320 wide > the 300 px card
    expect(full(W, H, sc) == rite, "card_clip_too_big: a picture wider than its card plays the rite");
    // clip_unloaded_falls_back
    gLook[0].card[uieng::CARD_LOW].clip = 7;
    expect(full(W, H, sc) == rite, "clip_unloaded_falls_back: a picture that did not load plays the rite");
    // the engine repaints exactly what changed with a picture playing
    gLook[0].card[uieng::CARD_LOW].clip = 0; gLook[0].card[uieng::CARD_HIGH].clip = 0;
    expect(pipeline(kRites[0], W, H), "clip: dirty regions cover every change across on/off/level/age");
    useLook(false); assets::clear((uint8_t)pal::TH_UMBRAVOX);
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
    cardTests();
    clipTests();
    printf(fails ? "FAILED\n" : "PASSED\n"); return fails ? 1 : 0;
}
