// Host test: an installed theme layout (uieng::activeLayout) moves the shared
// chrome and Scan's count tiles, and every screen's hits follow it. With no
// layout installed everything is the built-in geometry (the oracle).
#include "../ui/themelayout.h"
#include "oracle_hits.inc"
#include <cstdio>
#include <cstring>
#include <string>

using namespace uieng;
using themelayout::Board;

static int fails = 0, checks = 0;
static void check(const char* what, bool ok) {
    ++checks;
    if (!ok) { ++fails; printf("  FAIL %s\n", what); }
}

static const int BW[2] = {480, 320}, BH[2] = {320, 240};
static const char* BKEY[2] = {"waveshare", "cyd"};

static std::string rj(Rect r) { char b[64]; snprintf(b, sizeof b, "[%d,%d,%d,%d]", r.x, r.y, r.w, r.h); return b; }

// The test layout: footer moved up under the header (body shifts down 44 px),
// count.5g hidden, everything resolved through the real parser and resolve().
static ChromeOverride ov[2];
static LayoutOverride lo;
static void build(bool both = true) {
    for (int b = 0; b < 2; b++) {
        std::string pieces;
        const themelayout::Piece nav[5] = {themelayout::P_NAV_SCAN, themelayout::P_NAV_DOMAIN, themelayout::P_NAV_CODEX,
                                           themelayout::P_NAV_POI, themelayout::P_STOP};
        for (int i = 0; i < 5; i++) {
            Rect r = themelayout::defaultRect((Board)b, nav[i]); r.y = 44;
            pieces += std::string(i ? "," : "") + "\"" + themelayout::PIECE_NAME[nav[i]] + "\":" + rj(r);
        }
        pieces += ",\"count.5g\":\"hidden\"";
        std::string s = std::string("{\"format\":1,\"") + BKEY[b] + "\":{" + pieces + "}}";
        themelayout::Layout L; char why[96];
        bool ok = themelayout::parse(s.data(), s.size(), L, why, sizeof why) && themelayout::resolve(L, (Board)b, ov[b], why, sizeof why);
        if (!ok) printf("  build %s: %s\n", BKEY[b], why);
        check("test layout resolves", ok);
    }
    lo.board[0] = &ov[0];
    lo.board[1] = both ? &ov[1] : nullptr;
}

static const Hit* find(const HitList& h, const char* slot) {
    for (int i = 0; i < h.count; i++) if (h.items[i].slot && strcmp(h.items[i].slot, slot) == 0) return &h.items[i];
    return nullptr;
}
static bool inside(Rect in, Rect out) { return in.x >= out.x && in.y >= out.y && in.x + in.w <= out.x + out.w && in.y + in.h <= out.y + out.h; }
static bool isChromeSlot(const char* s) {
    static const char* const c[] = {"gps", "mesh", "power", "bridge", "nav-menu", "nav-scan", "nav-domain", "nav-codex", "mark-poi", "stop"};
    for (const char* x : c) if (strcmp(s, x) == 0) return true;
    return false;
}

static void testNoOverride() {
    activeLayout() = nullptr;
    HitCtx ctx;
    for (int i = 0; i < ORACLE_N; i++) {
        const OCase& oc = ORACLE[i];
        if (strcmp(oc.screen, "scan") != 0 || strcmp(oc.state, "active") != 0) continue;
        const HitList h = scanHits(oc.W, oc.H, ctx);
        bool ok = h.count == oc.n;
        for (int k = 0; k < oc.n; k++) {
            const Hit* f = find(h, oc.h[k].id);
            ok = ok && f && f->rect == Rect{oc.h[k].x, oc.h[k].y, oc.h[k].w, oc.h[k].h};
        }
        char tag[64]; snprintf(tag, sizeof tag, "no override = oracle (scan %dx%d)", oc.W, oc.H);
        check(tag, ok);
    }
    for (int b = 0; b < 2; b++) {
        const Chrome a = chrome(BW[b], BH[b]), e = builtinChrome(BW[b], BH[b]);
        check("no override: chrome is builtin", a.stop == e.stop && a.body == e.body && a.menu == e.menu);
    }
}

static void testOverride() {
    build();
    activeLayout() = &lo;
    HitCtx ctx;
    for (int b = 0; b < 2; b++) {
        const int W = BW[b], H = BH[b];
        char tag[96];
        const HitList hs = scanHits(W, H, ctx), hd = domainHits(W, H, ctx);
        snprintf(tag, sizeof tag, "%s: moved stop hit follows (scan, domain)", BKEY[b]);
        check(tag, find(hs, "stop") && find(hs, "stop")->rect == ov[b].c.stop && find(hd, "stop") && find(hd, "stop")->rect == ov[b].c.stop &&
                   ov[b].c.stop.y == 44);
        snprintf(tag, sizeof tag, "%s: wifi tile hit shifted", BKEY[b]);
        check(tag, find(hs, "WIFI") && find(hs, "WIFI")->rect == ov[b].tiles[0]);
        snprintf(tag, sizeof tag, "%s: hidden tile has no hit", BKEY[b]);
        bool zero = false;
        for (int i = 0; i < hs.count; i++) zero = zero || hs.items[i].rect.w <= 0 || hs.items[i].rect.h <= 0;
        check(tag, !find(hs, "5 GHZ") && !zero);
        snprintf(tag, sizeof tag, "%s: scan() hero and body shifted", BKEY[b]);
        const ScanLayout s = scan(W, H);
        check(tag, s.hero == ov[b].hero && s.body == ov[b].scanBody && s.counts == ov[b].counts && s.stop == ov[b].c.stop &&
                   s.navCells[3] == ov[b].c.navCells[3] && s.tiles[1] == Rect{0, 0, 0, 0});

        // Every screen: chrome hits are the override's rects; every other hit lies in the body.
        HitList lists[12]; const char* names[12]; int n = 0;
        names[n] = "scan"; lists[n++] = hs;
        names[n] = "domain"; lists[n++] = hd;
        names[n] = "codex"; lists[n++] = codexHits(W, H, ctx);
        names[n] = "menu"; lists[n++] = menuHits(W, H);
        names[n] = "mesh"; lists[n++] = meshHits(W, H);
        names[n] = "omens"; lists[n++] = omensHits(W, H);
        names[n] = "echoes"; lists[n++] = echoesHits(W, H);
        names[n] = "attune"; lists[n++] = attuneHits(W, H, 0);
        names[n] = "aspect"; lists[n++] = aspectHits(W, H, false);
        names[n] = "detail"; lists[n++] = detailHits(W, H);
        names[n] = "files"; lists[n++] = filesHits(W, H, false, true, 3, 3);
        for (int k = 0; k < n; k++) {
            const Rect body = (k == 0) ? ov[b].scanBody : ov[b].c.body;
            bool ok = true;
            for (int i = 0; i < lists[k].count; i++) {
                const Hit& h = lists[k].items[i];
                const bool chromeHit = h.slot && isChromeSlot(h.slot);
                const bool tile = k == 0 && h.slot && (!strcmp(h.slot, "WIFI") || !strcmp(h.slot, "BLE"));
                if (!chromeHit && !tile && !inside(h.rect, body)) {
                    ok = false;
                    printf("       %s %s: hit %s (%d,%d,%d,%d) outside body (%d,%d,%d,%d)\n", BKEY[b], names[k], h.slot ? h.slot : "?",
                           h.rect.x, h.rect.y, h.rect.w, h.rect.h, body.x, body.y, body.w, body.h);
                }
            }
            snprintf(tag, sizeof tag, "%s: %s hits stay in the body", BKEY[b], names[k]);
            check(tag, ok);
        }
    }
    activeLayout() = nullptr;
}

static void testOneBoard() {
    build(false);   // Review Focus 4: a cyd-only layout leaves the Waveshare built-in
    lo.board[0] = nullptr; lo.board[1] = &ov[1];
    activeLayout() = &lo;
    const Chrome a = chrome(480, 320), e = builtinChrome(480, 320);
    const ScanLayout s = scan(480, 320), es = builtinScan(480, 320);
    check("cyd-only layout leaves waveshare builtin", a.stop == e.stop && a.body == e.body && s.hero == es.hero && s.tiles[1] == es.tiles[1]);
    check("cyd-only layout applies on the cyd", chrome(320, 240).stop == ov[1].c.stop);
    activeLayout() = nullptr;
}

// The row ranges the presenters repaint when the status, body or nav changes.
static bool layoutOk[2];
static bool ok_layout(int b) { return layoutOk[b]; }
static bool rowsIn(Rect r, int y0, int y1) { return r.w <= 0 || (r.y >= y0 && r.y + r.h <= y1); }
static void testBands() {
    activeLayout() = nullptr;
    for (int b = 0; b < 2; b++) {
        const int W = BW[b], H = BH[b];
        const Bands B = bands(W, H);
        check("builtin bands are today's rows", B.statusTop == 0 && B.statusBottom == 44 && B.bodyTop == 44 &&
                                                    B.bodyBottom == H - 44 && B.navTop == H - 44 && B.navBottom == H);
    }
    build();
    // A scattered header: the bridge swaps places with the SCAN nav button, so a header piece sits on the bottom row.
    for (int b = 0; b < 2; b++) {
        const Rect br = themelayout::defaultRect((Board)b, themelayout::P_BRIDGE), ns = themelayout::defaultRect((Board)b, themelayout::P_NAV_SCAN);
        std::string s = std::string("{\"format\":1,\"") + BKEY[b] + "\":{\"bridge\":" + rj(ns) + ",\"nav.scan\":" + rj(br) + "}}";
        themelayout::Layout L; char why[96];
        const bool ok = themelayout::parse(s.data(), s.size(), L, why, sizeof why) && themelayout::resolve(L, (Board)b, ov[b], why, sizeof why);
        if (!ok) printf("  bands layout %s: %s\n", BKEY[b], why);
        layoutOk[b] = ok;
        lo.board[b] = &ov[b];
    }
    activeLayout() = &lo;
    for (int b = 0; b < 2; b++) {
        const int W = BW[b], H = BH[b];
        const Bands B = bands(W, H);
        const Chrome c = chrome(W, H);
        bool ok = true;
        const Rect head[] = {c.menu, c.wordmark, c.statusCells[0], c.statusCells[1], c.statusCells[2], c.bridge, c.sd};
        for (const Rect& r : head) ok = ok && rowsIn(r, B.statusTop, B.statusBottom);
        for (int i = 0; i < 4; i++) ok = ok && rowsIn(c.navCells[i], B.navTop, B.navBottom);
        ok = ok && rowsIn(c.stop, B.navTop, B.navBottom) && rowsIn(c.body, B.bodyTop, B.bodyBottom);
        char tag[96]; snprintf(tag, sizeof tag, "%s: a moved bridge is inside the status repaint rows", BKEY[b]);
        check(tag, ok && ok_layout(b) && B.statusBottom == H && B.navTop == 0);
    }
    activeLayout() = nullptr;
}

// Review I1: a count tile moved outside the body's rows (the CYD row-repaint path) must be inside the body repaint rows.
static void testTileRows() {
    for (int b = 0; b < 2; b++) {
        std::string s = std::string("{\"format\":1,\"") + BKEY[b] + "\":{\"wordmark\":\"hidden\",\"count.wifi\":" +
                        rj(themelayout::defaultRect((Board)b, themelayout::P_WORDMARK)) + "}}";
        themelayout::Layout L; char why[96];
        const bool ok = themelayout::parse(s.data(), s.size(), L, why, sizeof why) && themelayout::resolve(L, (Board)b, ov[b], why, sizeof why);
        if (!ok) printf("  tile layout %s: %s\n", BKEY[b], why);
        lo.board[b] = ok ? &ov[b] : nullptr;
    }
    activeLayout() = &lo;
    for (int b = 0; b < 2; b++) {
        const Bands B = bands(BW[b], BH[b]);
        const Rect t = ov[b].tiles[0];
        char tag[96]; snprintf(tag, sizeof tag, "%s: a tile in the header row is inside the body repaint rows", BKEY[b]);
        check(tag, t.y == 0 && rowsIn(t, B.bodyTop, B.bodyBottom));
    }
    activeLayout() = nullptr;
}

// Plan 2 Task 2: the hero's parts are scan() rects, equal to today's formulas.
static void testHeroParts() {
    activeLayout() = nullptr;
    const ScanLayout w = scan(480, 320), c = scan(320, 240);
    check("waveshare radar {188,56,209,209}", w.radar == Rect{188, 56, 209, 209});
    check("waveshare spectrum {398,194,66,74}", w.spectrum == Rect{398, 194, 66, 74});
    check("waveshare channel {364,52,100,24}", w.channel == Rect{364, 52, 100, 24});
    check("waveshare badge {124,52,62,62}", w.badge == Rect{124, 52, 62, 62});
    check("cyd radar {151,52,115,115}", c.radar == Rect{151, 52, 115, 115});
    check("cyd spectrum {138,162,140,26}", c.spectrum == Rect{138, 162, 140, 26});
    check("cyd channel {252,52,60,16}", c.channel == Rect{252, 52, 60, 16});
    check("cyd badge {104,52,52,52}", c.badge == Rect{104, 52, 52, 52});
    // With a layout that shifts the body, the parts shift with the hero.
    build();
    activeLayout() = &lo;
    for (int b = 0; b < 2; b++) {
        const ScanLayout s = scan(BW[b], BH[b]), e = builtinScan(BW[b], BH[b]);
        const int dx = s.hero.x - e.hero.x, dy = s.hero.y - e.hero.y;
        auto sh = [&](Rect r) { return Rect{r.x + dx, r.y + dy, r.w, r.h}; };
        char tag[64]; snprintf(tag, sizeof tag, "%s: parts shift with the hero", BKEY[b]);
        check(tag, dy == 44 && s.radar == sh(e.radar) && s.spectrum == sh(e.spectrum) && s.channel == sh(e.channel) && s.badge == sh(e.badge));
    }
    activeLayout() = nullptr;
}

int main() {
    printf("== layout override\n");
    testHeroParts();
    testTileRows();
    testNoOverride();
    testOverride();
    testOneBoard();
    testBands();
    printf(fails ? "FAILED %d/%d\n" : "PASSED %d checks\n", fails ? fails : checks, checks);
    return fails ? 1 : 0;
}
