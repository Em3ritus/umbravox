// Host tests for emblem and motif drawing: one emblem blitter serves the baked
// muffin and any pack's picture, and drawMotif lets every alert rite draw "the
// theme's picture" whatever the centrepiece is.
#include "../ui/canvas.h"
#include "../ui/palette.h"
#include "../ui/screens.h"
#include "../ui/themepack.h"
#include "../ui/signatures.h"
#include "../ui/motif.h"
#include "../ui/muffinseal.h"
#include "../ui/layout.h"
#include "../ui/sweep.h"
#include <initializer_list>
#include <cstdio>
#include <cstring>

static int fails = 0;
static void check(const char* w, bool ok) {
    printf(ok ? "  ok   %s\n" : "  FAIL %s\n", w);
    if (!ok) fails++;
}

using namespace ui;

static uint32_t sum(const Canvas& c) {            // FNV-1a over the pixel array
    uint32_t h = 2166136261u;
    for (int i = 0; i < W * H; i++) {
        h = (h ^ (c.px[i] & 0xFF)) * 16777619u;
        h = (h ^ (c.px[i] >> 8)) * 16777619u;
    }
    return h;
}

// --- 1. regression guard: the built-in themes must render as they always did.
// Checksums were generated from the code BEFORE the refactor (`./test_motif
// --print` on the unmodified tree) and are pinned here.
static uint32_t renderScan(uint8_t theme, uint32_t frame) {
    pal::setTheme(theme);
    State st; st.set.theme = theme; st.frame = frame;
    Canvas c; c.clear(0);
    drawScan(c, st, nullptr);
    return sum(c);
}
static uint32_t renderAlert(int style, int age) {
    pal::setTheme(pal::TH_COSMICMUFFIN);
    State st; st.set.theme = pal::TH_COSMICMUFFIN; st.set.alertStyle = (uint8_t)style;
    st.alertActive = true; st.alertThreat = TH_DRONE; st.alertConf = CONF_HIGH;
    st.alertAge = (uint16_t)age;
    Canvas c; c.clear(0);
    drawAlert(c, st);
    return sum(c);
}

static const int AGES[3] = {100, 400, 900};

static void computeGuard(uint32_t* out) {
    int n = 0;
    const uint8_t th[2] = {pal::TH_COSMICMUFFIN, pal::TH_BAPH0MAU5};
    for (uint8_t t : th) for (uint32_t f : {0u, 24u}) out[n++] = renderScan(t, f);
    for (int s = 0; s < 4; s++) for (int a : AGES) out[n++] = renderAlert(s, a);
}

// --- helpers over a sentinel-filled canvas ----------------------------------
constexpr uint16_t SENT = 0x1234;

static bool changedInside(const Canvas& c, int cx, int cy, int r) {
    for (int y = 0; y < H; y++) for (int x = 0; x < W; x++)
        if ((x - cx) * (x - cx) + (y - cy) * (y - cy) <= r * r && c.px[y * W + x] != SENT) return true;
    return false;
}
static bool untouchedOutside(const Canvas& c, int cx, int cy, int r) {
    for (int y = 0; y < H; y++) for (int x = 0; x < W; x++)
        if ((x - cx) * (x - cx) + (y - cy) * (y - cy) > r * r && c.px[y * W + x] != SENT) return false;
    return true;
}
static bool same(const Canvas& a, const Canvas& b) { return !memcmp(a.px, b.px, sizeof a.px); }

// Emblem fixtures. Colours avoid the sentinel and black.
static uint8_t PX1[1] = {0};
static uint8_t PX96[96 * 96];
static uint8_t PX2[8 * 8];
static themepack::Emblem mkEmblem(int w, int h, int ncol, const uint8_t* px) {
    themepack::Emblem e; memset(&e, 0, sizeof e);
    e.w = (uint8_t)w; e.h = (uint8_t)h; e.ncol = (uint8_t)ncol; e.px = px;
    for (int i = 0; i < ncol; i++) e.pal[i] = (uint16_t)(0xF81F - 0x0841 * i);
    return e;
}

static themepack::Pack mkPack(const char* name, themepack::Centre cen, themepack::Rite rite) {
    themepack::Pack p; memset(&p, 0, sizeof p);
    snprintf(p.name, sizeof p.name, "%s", name);
    snprintf(p.header, sizeof p.header, "%s", name);
    p.dial = themepack::DIAL_COMPACT; p.centre = cen; p.rite = rite;
    p.sweeps = 0x07FF; p.sweep = SW_RADAR;
    for (int i = 0; i < themepack::N_COLORS; i++) p.color[i] = (uint16_t)(0x2104 * (i + 1));
    p.color[themepack::R_VOID] = 0x0000;
    p.color[themepack::R_AMBER] = 0xFFE0;
    p.color[themepack::R_RED] = 0xF800;
    return p;
}

int main(int argc, char** argv) {
    if (argc > 1 && !strcmp(argv[1], "--print")) {
        uint32_t g[16]; computeGuard(g);
        for (int i = 0; i < 16; i++) printf("0x%08Xu,\n", g[i]);
        return 0;
    }
    printf("regression guard (built-in COSMIC MUFFIN and BAPH0MAU5):\n");
    {
        static const uint32_t WANT[16] = {
            0x94DF7B48u, 0xB7FE80B7u, 0xC0E2870Eu, 0x19C55742u,
            0x98443D4Fu, 0x3B0FCDF5u, 0xB8989D41u, 0xA7A853D7u,
            0x3462C731u, 0x5ED8CDD4u, 0x10423CCBu, 0xC65AC96Eu,
            0x63009A05u, 0xD478848Eu, 0x5BA0A744u, 0x1FE8CF87u,
        };
        uint32_t g[16]; computeGuard(g);
        for (int i = 0; i < 16; i++) {
            char b[48]; snprintf(b, sizeof b, "render %d unchanged", i);
            check(b, g[i] == WANT[i]);
        }
    }

    for (int i = 0; i < 96 * 96; i++) PX96[i] = (uint8_t)((i / 96 + i % 96) % 5);
    for (int i = 0; i < 64; i++) PX2[i] = (uint8_t)(((i / 8) + (i % 8)) & 1);
    const themepack::Emblem e1 = mkEmblem(1, 1, 1, PX1);
    const themepack::Emblem e96 = mkEmblem(96, 96, 5, PX96);
    const themepack::Emblem e2 = mkEmblem(8, 8, 2, PX2);

    printf("drawEmblem stays inside the circle:\n");
    {
        const themepack::Emblem* es[3] = {&e1, &e96, &e2};
        const char* nm[3] = {"1x1", "96x96", "2-colour"};
        for (int k = 0; k < 3; k++) {
            Canvas c; c.clear(SENT);
            drawEmblem(c, *es[k], 80, 60, 22, 0, 128);
            char b[64];
            snprintf(b, sizeof b, "%s: draws inside", nm[k]);
            check(b, changedInside(c, 80, 60, 22));
            snprintf(b, sizeof b, "%s: nothing outside r", nm[k]);
            check(b, untouchedOutside(c, 80, 60, 22));
        }
        Canvas lo, mid, hi; lo.clear(SENT); mid.clear(SENT); hi.clear(SENT);
        drawEmblem(lo, e96, 80, 60, 22, 0, 40);
        drawEmblem(mid, e96, 80, 60, 22, 0, 128);
        drawEmblem(hi, e96, 80, 60, 22, 0, 220);
        check("lift changes the pixels both ways", !same(lo, mid) && !same(hi, mid));
        Canvas none; none.clear(SENT);
        drawEmblem(none, e96, 80, 60, 22, 0, -1);
        check("-1 draws as 128", same(none, mid));
        Canvas edge; edge.clear(SENT);
        drawEmblem(edge, e96, 3, 3, 22, 0, 128);          // clipped by the canvas, no crash
        check("an emblem hanging off the canvas draws", changedInside(edge, 3, 3, 22));
    }

    printf("the built-in muffin through drawEmblem:\n");
    {
        Canvas a, b; a.clear(SENT); b.clear(SENT);
        pal::setTheme(pal::TH_COSMICMUFFIN);
        drawMuffin(a, 80, 60, 30, 0, 150);
        drawEmblem(b, muffinEmblem(), 80, 60, 30, 0, 150);
        check("drawMuffin == drawEmblem(muffinEmblem())", same(a, b));
    }

    printf("drawMotif:\n");
    {
        pal::setTheme(pal::TH_UMBRAVOX);
        Canvas a, b; a.clear(SENT); b.clear(SENT);
        drawMotif(a, pal::TH_UMBRAVOX, 80, 60, 30, 0, 200, 0xFFFF);
        heptagram(b, 80, 60, 30, 0xFFFF);
        check("a heptagram theme draws the heptagram", same(a, b));
        check("...and it is not blank", changedInside(a, 80, 60, 30));

        Canvas m, e; m.clear(SENT); e.clear(SENT);
        drawMotif(m, pal::TH_COSMICMUFFIN, 80, 60, 30, 0, 170, 0xFFFF);
        drawEmblem(e, muffinEmblem(), 80, 60, 30, 0, 170);
        check("an emblem theme draws the emblem (with the lift)", same(m, e));
        check("...which is not the heptagram", !same(m, b));
    }

    printf("pack rites, emblem centrepiece:\n");
    {
        int ids[5];
        const char* rn[5] = {"ward", "supernova", "vortex", "comet", "lockon"};
        for (int r = 0; r < 5; r++) {
            char nm[16]; snprintf(nm, sizeof nm, "EMB%d", r);
            ids[r] = pal::addPack(mkPack(nm, themepack::CENTRE_EMBLEM, (themepack::Rite)r), &e96);
        }
        static Canvas shot[5];
        const int ages[] = {0, 60, 150, 260, 420, 900, 2500};
        for (int r = 0; r < 5; r++) {
            pal::setTheme((uint8_t)ids[r]);
            State st; st.set.theme = (uint8_t)ids[r];
            st.alertActive = true; st.alertThreat = TH_DRONE; st.alertConf = CONF_HIGH;
            bool inside = true, drew = false;
            for (int age : ages) {
                // The UMBRAVOX ward (the pre-existing rite, left as it was) is
                // not clipped to its card: its first 90 ms strike is an oversized
                // heptagram, and its inscription runes straddle the border by up
                // to 3 px. So the ward is held to the card plus 4 px, and not
                // before the strike has settled; the cosmic rites are strict.
                const int slack = r == 0 ? 4 : 0;
                if (r == 0 && age < 90) continue;
                Canvas c; c.clear(SENT);
                st.alertAge = (uint16_t)age;
                drawAlert(c, st);
                for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) {
                    const bool onCard = x >= COL_R_X && x < COL_R_X + COL_R_W &&
                                        y >= DIAL_Y && y < DIAL_Y + DIAL_H;
                    const bool nearCard = x >= COL_R_X - slack && x < COL_R_X + COL_R_W + slack &&
                                          y >= DIAL_Y - slack && y < DIAL_Y + DIAL_H + slack;
                    if (!nearCard && c.px[y * W + x] != SENT) inside = false;
                    if (onCard && c.px[y * W + x] != SENT) drew = true;
                }
            }
            char b[64];
            snprintf(b, sizeof b, "%s: draws inside the card", rn[r]); check(b, drew);
            snprintf(b, sizeof b, "%s: nothing outside the card", rn[r]); check(b, inside);
            shot[r].clear(0); st.alertAge = 420; drawAlert(shot[r], st);
        }
        bool distinct = true;
        for (int a = 0; a < 5; a++) for (int b = a + 1; b < 5; b++)
            if (same(shot[a], shot[b])) distinct = false;
        check("the five rites draw five different pictures", distinct);
    }

    printf("a CP_EMBLEM pack with no emblem:\n");
    {
        const int id = pal::addPack(mkPack("NOEMB", themepack::CENTRE_EMBLEM, themepack::RITE_WARD), nullptr);
        check("registered as an emblem theme", id >= 0 && pal::centrepiece((uint8_t)id) == pal::CP_EMBLEM);
        check("...with no emblem", pal::emblem((uint8_t)id) == nullptr);
        pal::setTheme((uint8_t)id);
        Canvas a, b; a.clear(SENT); b.clear(SENT);
        drawMotif(a, (uint8_t)id, 80, 60, 30, 0, 200, 0xFFFF);
        heptagram(b, 80, 60, 30, 0xFFFF);
        check("drawMotif falls back to the heptagram", same(a, b));
        State st; st.set.theme = (uint8_t)id;
        Canvas s; s.clear(0);
        drawScan(s, st, nullptr);                         // drawDialAt must not dereference NULL
        check("the scan screen draws", sum(s) != 0);
        st.alertActive = true; st.alertThreat = TH_DRONE; st.alertConf = CONF_HIGH; st.alertAge = 420;
        Canvas al; al.clear(SENT); drawAlert(al, st);
        check("the alert draws", changedInside(al, COL_R_X + COL_R_W / 2, DIAL_Y + DIAL_H / 2, 200));
    }

    printf(fails ? "\n%d FAILED\n" : "\nall passed\n", fails);
    return fails ? 1 : 0;
}
