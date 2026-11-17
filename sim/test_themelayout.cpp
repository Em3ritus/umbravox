// Host test for the layout.json parser (ui/themelayout.h). A layout is untrusted
// data, so this is built under the address and UB sanitizers (see the Makefile)
// and ends with a mutation fuzz whose assertion is an invariant, not survival.
#include "../ui/themelayout.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>

using namespace themelayout;

static int fails = 0;
static void check(const char* what, bool ok) {
    printf(ok ? "  ok   %s\n" : "  FAIL %s\n", what);
    if (!ok) fails++;
}

static bool P(const std::string& s, Layout& L, char* why = nullptr, size_t cap = 0) {
    return parse(s.data(), s.size(), L, why, cap);
}
static bool contains(const char* hay, const char* needle) { return strstr(hay, needle) != nullptr; }

static void testParse() {
    Layout L; char why[96];

    check("parses a waveshare menu rect",
          P("{\"format\":1,\"waveshare\":{\"menu\":[0,276,44,44]}}", L) && L.has[B_WAVESHARE] && !L.has[B_CYD] &&
          L.slot[B_WAVESHARE][P_MENU].how == PLACED && L.slot[B_WAVESHARE][P_MENU].x == 0 &&
          L.slot[B_WAVESHARE][P_MENU].y == 276 && L.slot[B_WAVESHARE][P_MENU].w == 44 &&
          L.slot[B_WAVESHARE][P_MENU].h == 44);
    check("hidden wordmark",
          P("{\"format\":1,\"cyd\":{\"wordmark\":\"hidden\"}}", L) && L.has[B_CYD] &&
          L.slot[B_CYD][P_WORDMARK].how == HIDDEN);
    check("absent piece is DEFAULT",
          P("{\"format\":1,\"cyd\":{\"wordmark\":\"hidden\"}}", L) && L.slot[B_CYD][P_STOP].how == DEFAULT &&
          L.slot[B_WAVESHARE][P_WORDMARK].how == DEFAULT);
    check("empty board object counts as present", P("{\"format\":1,\"cyd\":{}}", L) && L.has[B_CYD]);
    check("every piece name parses", [&] {
        for (int i = 0; i < P_N; i++) {
            std::string s = std::string("{\"format\":1,\"waveshare\":{\"") + PIECE_NAME[i] + "\":[1,2,50,50]}}";
            if (!P(s, L) || L.slot[B_WAVESHARE][i].how != PLACED) return false;
        }
        return true;
    }());
    check("format 2 refused", !P("{\"format\":2}", L, why, sizeof why) && contains(why, "format"));
    check("missing format refused", !P("{\"cyd\":{}}", L, why, sizeof why) && contains(why, "format"));
    check("script key refused at top level",
          !P("{\"format\":1,\"script\":\"x\"}", L, why, sizeof why) && contains(why, "script"));
    check("script key refused inside a board",
          !P("{\"format\":1,\"cyd\":{\"script\":[1,2,3,4]}}", L, why, sizeof why) && contains(why, "script"));
    check("duplicate piece refused",
          !P("{\"format\":1,\"cyd\":{\"sd\":\"hidden\",\"sd\":\"hidden\"}}", L, why, sizeof why) &&
          contains(why, "duplicate"));
    check("duplicate board refused",
          !P("{\"format\":1,\"cyd\":{},\"cyd\":{}}", L, why, sizeof why) && contains(why, "duplicate"));
    check("array of 3 refused", !P("{\"format\":1,\"cyd\":{\"sd\":[1,2,3]}}", L));
    check("array of 5 refused", !P("{\"format\":1,\"cyd\":{\"sd\":[1,2,3,4,5]}}", L));
    check("non-integer refused", !P("{\"format\":1,\"cyd\":{\"sd\":[1,2.5,3,4]}}", L));
    check("other string refused", !P("{\"format\":1,\"cyd\":{\"sd\":\"gone\"}}", L));
    check("value over 32767 refused", !P("{\"format\":1,\"cyd\":{\"sd\":[32768,0,44,44]}}", L));
    check("value under -32768 refused", !P("{\"format\":1,\"cyd\":{\"sd\":[-32769,0,44,44]}}", L));
    check("negative x parses (check refuses it later)",
          P("{\"format\":1,\"cyd\":{\"sd\":[-5,0,44,44]}}", L) && L.slot[B_CYD][P_SD].x == -5);
    check("zero width refused", !P("{\"format\":1,\"cyd\":{\"sd\":[0,0,0,44]}}", L, why, sizeof why) &&
                                contains(why, "sd"));
    check("unknown piece ignored",
          P("{\"format\":1,\"cyd\":{\"radar\":[1,2,3,4]}}", L) && L.has[B_CYD]);
    check("unknown top-level key ignored", P("{\"format\":1,\"note\":\"hi\"}", L));
    check("board must be an object", !P("{\"format\":1,\"cyd\":[1]}", L));
    check("depth 4 refused", !P("{\"format\":1,\"x\":[[[1]]]}", L, why, sizeof why) && contains(why, "deep"));
    check("trailing data refused", !P("{\"format\":1} x", L));
    std::string big = "{\"format\":1,\"note\":\"" + std::string(MAX_JSON, 'a') + "\"}";
    check("over MAX_JSON refused", !P(big, L, why, sizeof why) && contains(why, "large"));
    {   // not NUL terminated: the byte after n is junk and must never be read as part of the JSON
        const std::string s = "{\"format\":1}";
        std::vector<char> buf(s.begin(), s.end());
        buf.push_back('x');
        check("not NUL terminated", parse(buf.data(), s.size(), L, why, sizeof why));
        check("truncated by n", !parse(buf.data(), s.size() - 1, L, why, sizeof why));
    }
    check("null input refused", !parse(nullptr, 5, L, why, sizeof why));
}

static void testFuzz() {
    const char* seeds[] = {
        "{\"format\":1,\"waveshare\":{\"menu\":[0,276,44,44],\"wordmark\":\"hidden\"}}",
        "{\"format\":1,\"cyd\":{\"stop\":[260,0,60,44],\"nav.poi\":[208,0,52,44]}}",
        "{\"format\":1,\"waveshare\":{},\"cyd\":{}}",
        "{\"format\":1,\"cyd\":{\"count.ble\":\"hidden\",\"count.5g\":[8,100,88,44]}}",
        "{\"format\":1,\"note\":[1,{\"a\":2}],\"waveshare\":{\"sd\":[-3,4,44,44]}}",
        "{\"format\":1,\"waveshare\":{\"status.gps\":[1,2,3,4],\"status.nodes\":[5,6,7,8]}}",
    };
    const char ins[] = "[]{}\",:-0123456789";
    srand(12345);
    long accepted = 0;
    bool invariant = true;
    for (int it = 0; it < 200000; it++) {
        std::string s = seeds[it % 6];
        int muts = 1 + rand() % 4;
        for (int m = 0; m < muts && !s.empty(); m++) {
            size_t at = (size_t)rand() % s.size();
            switch (rand() % 4) {
            case 0: s[at] = (char)(rand() % 256); break;
            case 1: s.resize(at); break;
            case 2: s.insert(at, 1, ins[rand() % (sizeof ins - 1)]); break;
            default: s.erase(at, 1); break;
            }
        }
        Layout L; char why[64];
        // Copy into an exact-size heap buffer so ASan catches any read past n.
        char* buf = (char*)malloc(s.size() ? s.size() : 1);
        memcpy(buf, s.data(), s.size());
        bool ok = parse(buf, s.size(), L, why, sizeof why);
        free(buf);
        if (!ok) continue;
        accepted++;
        for (int b = 0; b < B_N; b++)
            for (int i = 0; i < P_N; i++) {
                const Slot& sl = L.slot[b][i];
                if (sl.how != DEFAULT && sl.how != PLACED && sl.how != HIDDEN) invariant = false;
                if (sl.how == PLACED && (sl.w <= 0 || sl.h <= 0)) invariant = false;
            }
    }
    printf("  fuzz: 200000 mutations, %ld accepted\n", accepted);
    check("fuzz invariant holds", invariant);
}

// ---------- resolve + check ----------------------------------------------------
static const int BW[B_N] = {480, 320}, BH[B_N] = {320, 240};
static const char* BKEY[B_N] = {"waveshare", "cyd"};

static std::string rectJson(uieng::Rect r) {
    char b[64]; snprintf(b, sizeof b, "[%d,%d,%d,%d]", r.x, r.y, r.w, r.h); return b;
}
// One board's layout from "piece":value pairs (already JSON).
static bool R(int b, const std::string& pieces, uieng::ChromeOverride& o, char* why = nullptr, size_t cap = 0) {
    Layout L;
    std::string s = std::string("{\"format\":1,\"") + BKEY[b] + "\":{" + pieces + "}}";
    if (!P(s, L, why, cap)) { if (why) snprintf(why, cap, "PARSE FAILED"); return false; }
    return resolve(L, (Board)b, o, why, cap);
}
static bool same(uieng::Rect a, uieng::Rect b) { return a == b; }
static uieng::Rect shift(uieng::Rect r, int dx, int dy) { return {r.x + dx, r.y + dy, r.w, r.h}; }

static void testResolve() {
    char why[96], tag[96];
    for (int b = 0; b < B_N; b++) {
        const int W = BW[b], H = BH[b];
        const uieng::Chrome bc = uieng::builtinChrome(W, H);
        const uieng::ScanLayout bs = uieng::builtinScan(W, H);
        uieng::ChromeOverride o;

        snprintf(tag, sizeof tag, "%s: boardFor", BKEY[b]);
        check(tag, boardFor(W) == (Board)b);

        snprintf(tag, sizeof tag, "%s: empty board resolves to builtin", BKEY[b]);
        bool ok = R(b, "", o, why, sizeof why);
        ok = ok && same(o.c.status, bc.status) && same(o.c.nav, bc.nav) && same(o.c.body, bc.body) &&
             same(o.c.wordmark, bc.wordmark) && same(o.c.stop, bc.stop) && same(o.c.bridge, bc.bridge) &&
             same(o.c.menu, bc.menu) && same(o.c.sd, bc.sd);
        for (int i = 0; i < 4; i++) ok = ok && same(o.c.statusCells[i], bc.statusCells[i]) && same(o.c.navCells[i], bc.navCells[i]);
        for (int i = 0; i < 3; i++) ok = ok && same(o.tiles[i], bs.tiles[i]);
        ok = ok && same(o.hero, bs.hero) && same(o.counts, bs.counts) && same(o.scanBody, bs.body);
        check(tag, ok);

        snprintf(tag, sizeof tag, "%s: defaultRect matches builtin", BKEY[b]);
        check(tag, same(defaultRect((Board)b, P_STOP), bc.stop) && same(defaultRect((Board)b, P_GPS), bc.statusCells[0]) &&
                   same(defaultRect((Board)b, P_NAV_POI), bc.navCells[3]) && same(defaultRect((Board)b, P_COUNT_BLE), bs.tiles[2]));

        // Footer moved up under the header: the free area starts 44 px lower.
        std::string footer;
        const Piece nav[5] = {P_NAV_SCAN, P_NAV_DOMAIN, P_NAV_CODEX, P_NAV_POI, P_STOP};
        for (int i = 0; i < 5; i++) {
            uieng::Rect r = defaultRect((Board)b, nav[i]); r.y = 44;
            footer += std::string(i ? "," : "") + "\"" + PIECE_NAME[nav[i]] + "\":" + rectJson(r);
        }
        snprintf(tag, sizeof tag, "%s: footer under header shifts the body", BKEY[b]);
        ok = R(b, footer, o, why, sizeof why);
        const int dy = 44;   // free area {0,88,W,H-88}; body H-104 tall centred in it -> y = 96 = 52 + 44
        check(tag, ok && same(o.c.body, {8, 52 + dy, W - 16, H - 104}) && same(o.hero, shift(bs.hero, 0, dy)) &&
                   same(o.tiles[0], shift(bs.tiles[0], 0, dy)) && same(o.scanBody, shift(bs.body, 0, dy)) &&
                   same(o.counts, shift(bs.counts, 0, dy)) && o.c.navCells[0].y == 44);
        if (!ok) printf("       why: %s\n", why);

        snprintf(tag, sizeof tag, "%s: off screen left refused", BKEY[b]);
        uieng::Rect r = bc.sd; r.x = -1;
        check(tag, !R(b, "\"sd\":" + rectJson(r), o, why, sizeof why) && contains(why, "off screen"));
        snprintf(tag, sizeof tag, "%s: off screen right refused", BKEY[b]);
        r = bc.statusCells[2]; r.w += 1;
        check(tag, !R(b, "\"status.power\":" + rectJson(r), o, why, sizeof why) && contains(why, "off screen"));
        snprintf(tag, sizeof tag, "%s: off screen bottom refused", BKEY[b]);
        r = bc.stop; r.h += 1;
        check(tag, !R(b, "\"stop\":" + rectJson(r), o, why, sizeof why) && contains(why, "off screen"));

        for (int p = 0; p < P_N; p++) {
            if (p == P_WORDMARK || p == P_SD) continue;
            r = defaultRect((Board)b, (Piece)p); r.w = 43;
            snprintf(tag, sizeof tag, "%s: %s under 44 refused", BKEY[b], PIECE_NAME[p]);
            check(tag, !R(b, std::string("\"") + PIECE_NAME[p] + "\":" + rectJson(r), o, why, sizeof why) &&
                       contains(why, "44"));
        }
        snprintf(tag, sizeof tag, "%s: wordmark may be 20 tall", BKEY[b]);
        r = bc.wordmark; r.h = 20;
        check(tag, R(b, "\"wordmark\":" + rectJson(r), o, why, sizeof why) && same(o.c.wordmark, r));

        const Piece fixed[] = {P_MENU, P_STOP, P_BRIDGE, P_GPS, P_NODES, P_POWER, P_NAV_SCAN, P_NAV_DOMAIN, P_NAV_CODEX, P_NAV_POI};
        for (Piece p : fixed) {
            snprintf(tag, sizeof tag, "%s: hiding %s refused", BKEY[b], PIECE_NAME[p]);
            check(tag, !R(b, std::string("\"") + PIECE_NAME[p] + "\":\"hidden\"", o, why, sizeof why) &&
                       contains(why, "cannot be hidden"));
        }
        snprintf(tag, sizeof tag, "%s: hiding count.ble allowed", BKEY[b]);
        check(tag, R(b, "\"count.ble\":\"hidden\"", o, why, sizeof why) && same(o.tiles[2], {0, 0, 0, 0}));
        snprintf(tag, sizeof tag, "%s: hiding wordmark and sd allowed", BKEY[b]);
        check(tag, R(b, "\"wordmark\":\"hidden\",\"sd\":\"hidden\"", o, why, sizeof why) &&
                   same(o.c.wordmark, {0, 0, 0, 0}) && same(o.c.sd, {0, 0, 0, 0}) && same(o.c.statusCells[3], {0, 0, 0, 0}));

        snprintf(tag, sizeof tag, "%s: overlap refused", BKEY[b]);
        check(tag, !R(b, "\"stop\":" + rectJson(bc.navCells[3]), o, why, sizeof why) && contains(why, "overlaps"));
        snprintf(tag, sizeof tag, "%s: tile over the radar refused", BKEY[b]);
        r = {bs.hero.x, bs.hero.y, 60, 60};
        check(tag, !R(b, "\"count.wifi\":" + rectJson(r), o, why, sizeof why) && contains(why, "radar"));
        snprintf(tag, sizeof tag, "%s: tile over a tile refused", BKEY[b]);
        check(tag, !R(b, "\"count.wifi\":" + rectJson(bs.tiles[1]), o, why, sizeof why) && contains(why, "overlaps"));

        snprintf(tag, sizeof tag, "%s: too little room refused", BKEY[b]);
        r = {0, H / 2 - 22, W, 44};
        check(tag, !R(b, "\"menu\":" + rectJson(r), o, why, sizeof why) && contains(why, "too little room"));
    }
}

int main() {
    printf("== themelayout parse\n");
    testParse();
    testFuzz();
    printf("== themelayout resolve\n");
    testResolve();
    printf(fails ? "FAILED: %d\n" : "all passed\n", fails);
    return fails ? 1 : 0;
}
