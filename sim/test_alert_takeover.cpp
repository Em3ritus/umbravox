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
#include <cstdlib>
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
static uieng::BoardLook gLook[2];
static uieng::LayoutOverride gLo{};
static bool gLookOn = false;   // pal::setTheme resets the layout: pipeline() puts the test look back after it
static const uieng::LayoutOverride* gActive = &gLo;
static void useLook(bool on) { gLookOn = on; gActive = &gLo; gLo.board[0] = gLo.board[1] = nullptr; gLo.look[0] = &gLook[0]; gLo.look[1] = &gLook[1]; uieng::activeLayout() = on ? &gLo : nullptr; }
static void useLayout(const uieng::LayoutOverride* lo) { gLookOn = lo != nullptr; gActive = lo; uieng::activeLayout() = lo; }
static bool pipeline(const RiteCase& r, int w, int h) {
    pal::setTheme(r.theme);
    if (gLookOn) uieng::activeLayout() = gActive;
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
    // review I3: a card scale sizes the label only; the detail lines stay at the detail scale and keep their spacing
    for (int scl : {3, 4}) {
        clearLook(); gLook[0].card[uieng::CARD_LOW].scale = (uint8_t)scl; useLook(true);
        ScanScene a = base; a.alert = mk(ui::CONF_LOW, 3000); ScanScene b = a; b.alert.name[0] = 0;
        const auto ia = full(W, H, a), ib = full(W, H, b);
        int y0 = 9999, y1 = -1; for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) if (ia[y * W + x] != ib[y * W + x]) { y0 = std::min(y0, y); y1 = std::max(y1, y); }
        useLook(false);
        char m[120]; snprintf(m, sizeof m, "card scale %d: the name line is %d rows (detail scale 2: at most 14) (review I3)", scl, y1 - y0 + 1);
        expect(y1 >= y0 && y1 - y0 + 1 <= 14, m);
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

// Plan 5a Task 4: effects during an alert.
static void effectTests() {
    const int W = 480, H = 320;
    // the primitives
    expect(fxPhase(0, uieng::SP_MEDIUM) == 0 && fxPhase(250, uieng::SP_MEDIUM) == 512 && fxPhase(500, uieng::SP_MEDIUM) == 0 &&
           fxPhase(500, uieng::SP_SLOW) == 512 && fxPhase(125, uieng::SP_FAST) == 512, "fxPhase: one cycle is 1000 / 500 / 250 ms");
    {
        std::vector<uint16_t> px(40 * 16, 0x0000); S s{px.data(), 40, 0, 16};
        tintRect(s, {10, 4, 8, 6}, 0xFFFF, 1, 2);
        int in = 0, out = 0; for (int y = 0; y < 16; y++) for (int x = 0; x < 40; x++) { const bool i = x >= 10 && x < 18 && y >= 4 && y < 10; if (px[y * 40 + x]) (i ? in : out)++; }
        expect(in == 48 && out == 0 && px[5 * 40 + 12] == ui::Canvas::mix(0, 0xFFFF, 1, 2), "tintRect blends exactly its rect");
    }
    {
        std::vector<uint16_t> px(40 * 16); for (int y = 0; y < 16; y++) for (int x = 0; x < 40; x++) px[y * 40 + x] = (uint16_t)(x + 100 * y);
        const std::vector<uint16_t> o = px; S s{px.data(), 40, 0, 16};
        shakeRect(s, {10, 2, 10, 5}, 2);
        bool ok = true;
        for (int y = 0; y < 16; y++) for (int x = 0; x < 40; x++) {
            const bool in = x >= 10 && x < 20 && y >= 2 && y < 7;
            const uint16_t want = !in ? o[y * 40 + x] : o[y * 40 + std::max(10, x - 2)];
            ok &= px[y * 40 + x] == want;
        }
        expect(ok, "shakeRect shifts the rect's rows by dx and fills the gap with the edge pixel");
    }
    pal::setTheme(pal::TH_UMBRAVOX);
    ScanScene base = scanfix::demo(pal::TH_UMBRAVOX); base.animateSweep = true; base.now = 2000;
    const uieng::Rect tile = uieng::scan(W, H).tiles[0];
    const uieng::Rect hero = uieng::scan(W, H).hero;
    clearLook(); useLook(true);
    ScanScene low = base; low.alert = mk(ui::CONF_LOW, 3000);              // medium: phase 0 at 3000 ms
    const std::vector<uint16_t> noFx = full(W, H, low);
    // flash_tints / effect_only_in_rect
    gLook[0].piece[themelayout::P_COUNT_WIFI].effect = uieng::FX_FLASH;
    const std::vector<uint16_t> fl = full(W, H, low);
    int in = 0; const bool only = onlyInside(fl, noFx, W, H, tile, &in);
    expect(in > 100, "flash_tints: the piece is tinted in the first half of the cycle");
    expect(only, "effect_only_in_rect: nothing outside the piece changes");
    { ScanScene b = low; b.alert.age = 3300; ScanScene c = b; gLook[0].piece[themelayout::P_COUNT_WIFI].effect = uieng::FX_NONE; const auto ref = full(W, H, c);
      gLook[0].piece[themelayout::P_COUNT_WIFI].effect = uieng::FX_FLASH; expect(full(W, H, b) == ref, "flash: the second half of the cycle is untinted"); }
    // pulse_tints_by_phase
    gLook[0].piece[themelayout::P_COUNT_WIFI].effect = uieng::FX_PULSE;
    { ScanScene a = low; a.alert.age = 3000; ScanScene b = low; b.alert.age = 3125;
      const auto pa = full(W, H, a), pb = full(W, H, b);
      int d = 0; for (int y = tile.y; y < tile.y + tile.h; y++) for (int x = tile.x; x < tile.x + tile.w; x++) d += pa[y * W + x] != pb[y * W + x];
      expect(d > 100 && onlyInside(pa, noFx, W, H, tile), "pulse_tints_by_phase: the tint follows the phase"); }
    // shake_shifts: 3000 + 62 ms -> step 1 of 8 (dx -1)
    gLook[0].piece[themelayout::P_COUNT_WIFI].effect = uieng::FX_SHAKE;
    { ScanScene a = low; a.alert.age = 3000 + 63; ScanScene ref = a; gLook[0].piece[themelayout::P_COUNT_WIFI].effect = uieng::FX_NONE; const auto r0 = full(W, H, ref);
      gLook[0].piece[themelayout::P_COUNT_WIFI].effect = uieng::FX_SHAKE; const auto sh = full(W, H, a);
      bool ok = true; for (int y = tile.y; y < tile.y + tile.h; y++) for (int x = tile.x; x < tile.x + tile.w - 1; x++) ok &= sh[y * W + x] == r0[y * W + x + 1];
      expect(ok && sh != r0, "shake_shifts: the rows move sideways"); }
    // an effect never draws over the card (a piece under a LOW card in the hero: the radar)
    gLook[0].piece[themelayout::P_COUNT_WIFI].effect = uieng::FX_NONE;
    gLook[0].piece[themelayout::P_RADAR].effect = uieng::FX_FLASH;
    expect(full(W, H, low) == noFx, "an effect under the card is hidden by it");
    gLook[0].piece[themelayout::P_RADAR].effect = uieng::FX_NONE;
    // effect_under_high
    gLook[0].piece[themelayout::P_COUNT_WIFI].effect = uieng::FX_FLASH;
    { ScanScene hi = base; hi.alert = mk(ui::CONF_HIGH, 3000); const auto a = full(W, H, hi);
      gLook[0].piece[themelayout::P_COUNT_WIFI].effect = uieng::FX_NONE; const auto b = full(W, H, hi);
      expect(a == b, "effect_under_high: a whole-screen HIGH card hides every effect"); }
    // no_alert_no_effect
    gLook[0].piece[themelayout::P_COUNT_WIFI].effect = uieng::FX_FLASH;
    { const auto a = full(W, H, base); useLook(false); const auto b = full(W, H, base); useLook(true);
      expect(a == b, "no_alert_no_effect"); }
    // effect_clears + effect_regions: the engine repaints the effect pieces every alert frame and once after
    gLook[0].piece[themelayout::P_COUNT_WIFI].effect = uieng::FX_FLASH;
    gLook[0].piece[themelayout::P_COUNT_BLE].effect = uieng::FX_SHAKE; gLook[0].piece[themelayout::P_COUNT_BLE].speed = uieng::SP_FAST;
    gLook[0].piece[themelayout::P_NAV_DOMAIN].effect = uieng::FX_PULSE;   // the nav repaints only on a REC/POI change: the effect must ask
    gLook[0].card[uieng::CARD_HIGH].r = {40, 60, 400, 200};
    { ScanScene a = low; a.alert.age = 3000; ScanScene b = low; b.alert.age = 3125; const auto pa = full(W, H, a), pb = full(W, H, b);
      const uieng::Rect nd = pieceRect(W, H, themelayout::P_NAV_DOMAIN); int d = 0;
      for (int y = nd.y; y < nd.y + nd.h; y++) for (int x = nd.x; x < nd.x + nd.w; x++) d += pa[y * W + x] != pb[y * W + x];
      expect(d > 100, "the nav pulse changes the nav button between frames"); }
    expect(pipeline(kRites[0], W, H) && pipeline(kRites[3], W, H), "effect_clears / effect_regions: dirty regions cover every change");
    (void)hero;
    useLook(false);
    {   // review I1: a region whose rows miss a shaking piece but whose 16-row strip holds some of them
        const char* js = "{\"format\":1,\"waveshare\":{\"count.wifi\":{\"rect\":[16,52,100,68],\"effect\":\"shake\"},"
                         "\"count.5g\":{\"rect\":[20,124,100,68],\"effect\":\"flash\"}}}";
        themelayout::PackLayout* pl = new themelayout::PackLayout(); char why[96];
        const bool built = themelayout::build(js, strlen(js), *pl, why, sizeof why);
        useLayout(&pl->lo);
        const bool ok = built && pipeline(kRites[0], W, H);
        useLayout(nullptr); delete pl;
        expect(ok, "a shake next to another dirty region sharing its strip rows repaints correctly (review I1)");
    }
    // plan 5b (5a M3): an effect piece wholly under a non-whole card is never seen, so it is not repainted
    for (int inside = 1; inside >= 0; inside--) {
        const uieng::Rect ble = uieng::scan(W, H).tiles[2];   // count.ble where the built-in layout puts it
        const int ch = std::max(120, ble.h + 16), cy = std::max(0, std::min(ble.y - 8, H - ch));
        const uieng::Rect card = inside ? uieng::Rect{0, cy, 300, ch} : uieng::Rect{ble.x + 10, cy, 300, ch};
        char js[320];
        snprintf(js, sizeof js, "{\"format\":1,\"waveshare\":{\"alert.card.med\":{\"rect\":[%d,%d,%d,%d]},"
                 "\"count.ble\":{\"rect\":[%d,%d,%d,%d],\"effect\":\"pulse\"}}}", card.x, card.y, card.w, card.h, ble.x, ble.y, ble.w, ble.h);
        themelayout::PackLayout* pl = new themelayout::PackLayout(); char why[96];
        const bool built = themelayout::build(js, strlen(js), *pl, why, sizeof why);
        if (!built) printf("    build: %s\n", why);
        pal::setTheme(pal::TH_UMBRAVOX); useLayout(&pl->lo);
        uieng::Rect fx[uieng::PIECE_SLOTS];
        const int nf = effectRects(W, H, fx, nullptr, &card);
        // the engine: a settled MED alert, two frames apart; the second frame's regions
        scanpres::Engine e; e.reset(W, H); e.enter();
        bool hasBle = false;
        for (int k = 0; k < 2; k++) {
            fill(e.back(), pal::TH_UMBRAVOX, 5000 + k * 97, mk(ui::CONF_MED, 4000 + k * 97));
            e.commit();
            uieng::Rect rg[scanpres::Engine::kMaxRegions]; const int nr = e.regions(rg, scanpres::Engine::kMaxRegions);
            if (k == 1) for (int i = 0; i < nr; i++) hasBle |= rg[i].x == ble.x && rg[i].y == ble.y && rg[i].w == ble.w && rg[i].h == ble.h;
            while (e.pending()) e.nextSlice(64);
        }
        const bool ok = built && pipeline(kRites[0], W, H);
        useLayout(nullptr); delete pl;
        if (inside) expect(built && nf == 0 && !hasBle && ok, "an effect wholly under a MED card is not repainted");
        else expect(built && nf == 1 && hasBle && ok, "an effect partly outside the card still repaints");
    }
}

// plan 5b (5a M4): a commit while a frame is still pending falls back to rows, and those rows cover the new card.
static void pendingTests() {
    const int W = 480, H = 320;
    const char* js = "{\"format\":1,\"waveshare\":{\"alert.card.med\":{\"rect\":[90,80,300,160]}}}";
    themelayout::PackLayout* pl = new themelayout::PackLayout(); char why[96];
    const bool built = themelayout::build(js, strlen(js), *pl, why, sizeof why);
    pal::setTheme(pal::TH_UMBRAVOX); useLayout(&pl->lo);
    scanpres::Engine e; e.reset(W, H); e.enter();
    fill(e.back(), pal::TH_UMBRAVOX, 1000, AlertScene{}); e.commit();
    while (e.pending()) e.nextSlice(64);                       // the first frame, served
    fill(e.back(), pal::TH_UMBRAVOX, 1097, AlertScene{}); e.commit();
    e.nextSlice(1);                                            // part of a scan frame
    const bool wasPending = e.pending();
    fill(e.back(), pal::TH_UMBRAVOX, 1194, mk(ui::CONF_MED, 0)); e.commit();
    uieng::Rect rg[scanpres::Engine::kMaxRegions]; const int nr = e.regions(rg, scanpres::Engine::kMaxRegions);
    bool rows[320] = {};
    while (e.pending()) { const auto sl = e.nextSlice(1); for (int y = sl.y0; y < sl.y1; y++) rows[y] = true; }
    bool cover = true; for (int y = 80; y < 240; y++) cover &= rows[y];
    useLayout(nullptr); delete pl;
    expect(built && wasPending && nr == 0 && cover, "a commit while a frame is pending dirties the card rows");
}

// Plan 5a Task 5: the CYD engine build (320x240) gets the takeover.
static void cydTests() {
    const int W = 320, H = 240;
    const uieng::Rect hero = uieng::scan(W, H).hero;
    for (const RiteCase& r : kRites) {
        pal::setTheme(r.theme);
        ScanScene base = scanfix::demo(r.theme); base.animateSweep = true; base.now = 2000;
        const auto plain = full(W, H, base);
        ScanScene lo = base; lo.alert = mk(ui::CONF_LOW, 3000, r.style);
        ScanScene md = base; md.alert = mk(ui::CONF_MED, 3000, r.style);
        ScanScene hi = base; hi.alert = mk(ui::CONF_HIGH, 3000, r.style);
        int inL = 0, inM = 0, inH = 0;
        const bool okL = onlyInside(full(W, H, lo), plain, W, H, hero, &inL), okM = onlyInside(full(W, H, md), plain, W, H, hero, &inM);
        onlyInside(full(W, H, hi), plain, W, H, {0, 0, W, H}, &inH);
        char m[140]; snprintf(m, sizeof m, "cyd_takeover_low_med_high %s: LOW %d / MED %d px in the hero only, HIGH %d px", r.name, inL, inM, inH);
        expect(okL && okM && inL > 300 && inM > 300 && inH > 10000, m);
    }
    {   // tap to dismiss: as on the Waveshare
        uieng::HitCtx c{}; c.alertTakeover = 2; const uieng::HitList h = uieng::scanHits(W, H, c);
        bool one = h.count == 1 && h.items[0].action == uieng::Action::AlertDismiss && h.items[0].rect == (uieng::Rect{0, 0, W, H});
        c.alertTakeover = 1; const uieng::HitList g = uieng::scanHits(W, H, c);
        bool dis = false; for (int i = 0; i < g.count; i++) dis |= g.items[i].action == uieng::Action::AlertDismiss && g.items[i].rect == hero;
        expect(one && dis, "cyd_tap_dismiss_unchanged: HIGH the whole screen, LOW/MED the hero");
    }
    {   // cyd_settled_rows: a settled LOW alert (nothing else moving) repaints only the rows its regions span
        pal::setTheme(pal::TH_UMBRAVOX);
        scanpres::Engine e; e.reset(W, H); e.enter();
        int lo = 1 << 30, hiY = -1; bool ok = true;
        for (int k = 0; k < 6; k++) {
            scanpres::SceneStore& st = e.back(); fill(st, pal::TH_UMBRAVOX, 5000, mk(ui::CONF_LOW, 3000 + k * 120)); st.animate(false);
            e.commit();
            int y0 = 1 << 30, y1 = -1;
            while (e.pending()) { const auto sl = e.nextSlice(20); if (sl.y1 > sl.y0) { y0 = std::min(y0, sl.y0); y1 = std::max(y1, sl.y1); } }
            if (k >= 2) {
                uieng::Rect rg[8]; const int n = alertRegions(hero, mk(ui::CONF_LOW, 3000 + k * 120), pal::TH_UMBRAVOX, rg, 8, W);
                lo = 1 << 30; hiY = -1; for (int i = 0; i < n; i++) { lo = std::min(lo, rg[i].y); hiY = std::max(hiY, rg[i].y + rg[i].h); }
                ok &= y1 > y0 && y0 >= (lo / 16) * 16 && y1 <= ((hiY + 15) / 16) * 16;
                if (!ok) { printf("    frame %d rows %d..%d, regions %d..%d\n", k, y0, y1, lo, hiY); break; }
            }
        }
        expect(ok, "cyd_settled_rows: a settled alert repaints only its regions' rows");
    }
    {   // review I2: the longest real reason (blescan's follower) stays inside the card, built-in and theme cards
        pal::setTheme(pal::TH_UMBRAVOX);
        ScanScene base = scanfix::demo(pal::TH_UMBRAVOX); base.animateSweep = true; base.now = 2000;
        const auto plain = full(W, H, base);
        ScanScene a = base; a.alert = mk(ui::CONF_LOW, 3000); std::snprintf(a.alert.why, sizeof a.alert.why, "%s", "TRAVELLED WITH YOU");
        int in = 0; const bool okHero = onlyInside(full(W, H, a), plain, W, H, hero, &in);
        clearLook(); gLook[1].card[uieng::CARD_LOW].r = {0, 44, 160, 96}; useLook(true);
        const bool okCard = onlyInside(full(W, H, a), plain, W, H, {0, 44, 160, 96});
        useLook(false);
        expect(okHero && okCard, "a long reason stays inside the card: CYD hero and a 160x96 card (review I2)");
    }
    for (const RiteCase& r : kRites) { char m[96]; snprintf(m, sizeof m, "cyd %s: dirty rows cover every change across on/off/level/age", r.name); expect(pipeline(r, W, H), m); }
    {   // and with a theme's cards and effects on the CYD
        clearLook(); gLook[1].card[uieng::CARD_LOW].r = {0, 0, 200, 100}; gLook[1].card[uieng::CARD_HIGH].r = {20, 20, 280, 180};
        gLook[1].piece[themelayout::P_STOP].effect = uieng::FX_SHAKE; gLook[1].piece[themelayout::P_COUNT_BLE].effect = uieng::FX_FLASH;
        useLook(true);
        expect(pipeline(kRites[0], W, H) && pipeline(kRites[2], W, H), "cyd cards + effects: dirty rows cover every change");
        useLook(false);
    }
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
    // 4. The CYD engine build (320x240) has the takeover too (plan 5a).
    cydTests();
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
    effectTests();
    pendingTests();
    printf(fails ? "FAILED\n" : "PASSED\n"); return fails ? 1 : 0;
}
