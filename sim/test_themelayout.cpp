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
          L.slot[B_WAVESHARE][P_MENU].how == SLOT_PLACED && L.slot[B_WAVESHARE][P_MENU].x == 0 &&
          L.slot[B_WAVESHARE][P_MENU].y == 276 && L.slot[B_WAVESHARE][P_MENU].w == 44 &&
          L.slot[B_WAVESHARE][P_MENU].h == 44);
    check("hidden wordmark",
          P("{\"format\":1,\"cyd\":{\"wordmark\":\"hidden\"}}", L) && L.has[B_CYD] &&
          L.slot[B_CYD][P_WORDMARK].how == SLOT_HIDDEN);
    check("absent piece is SLOT_DEFAULT",
          P("{\"format\":1,\"cyd\":{\"wordmark\":\"hidden\"}}", L) && L.slot[B_CYD][P_STOP].how == SLOT_DEFAULT &&
          L.slot[B_WAVESHARE][P_WORDMARK].how == SLOT_DEFAULT);
    check("empty board object counts as present", P("{\"format\":1,\"cyd\":{}}", L) && L.has[B_CYD]);
    check("every piece name parses", [&] {
        for (int i = 0; i < P_N; i++) {
            std::string s = std::string("{\"format\":1,\"waveshare\":{\"") + PIECE_NAME[i] + "\":[1,2,50,50]}}";
            if (!P(s, L) || L.slot[B_WAVESHARE][i].how != SLOT_PLACED) return false;
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
    check("depth 5 refused", !P("{\"format\":1,\"x\":[[[[1]]]]}", L, why, sizeof why) && contains(why, "deep"));
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
                if (sl.how != SLOT_DEFAULT && sl.how != SLOT_PLACED && sl.how != SLOT_HIDDEN) invariant = false;
                if (sl.how == SLOT_PLACED && (sl.w <= 0 || sl.h <= 0)) invariant = false;
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
            if (p == P_WORDMARK || p == P_SD || p >= P_RADAR) continue;   // not tappable: no 44 px rule
            r = defaultRect((Board)b, (Piece)p); r.w = 43;
            snprintf(tag, sizeof tag, "%s: %s under 44 refused", BKEY[b], PIECE_NAME[p]);
            check(tag, !R(b, std::string("\"") + PIECE_NAME[p] + "\":" + rectJson(r), o, why, sizeof why) &&
                       contains(why, "44"));
        }
        snprintf(tag, sizeof tag, "%s: wordmark smaller than built-in refused", BKEY[b]);
        r = bc.wordmark; r.h = 20;
        check(tag, !R(b, "\"wordmark\":" + rectJson(r), o, why, sizeof why) && contains(why, "built-in size"));
        snprintf(tag, sizeof tag, "%s: tiny sd refused (review: [60,0,2,2] was accepted)", BKEY[b]);
        check(tag, !R(b, "\"wordmark\":\"hidden\",\"sd\":[60,0,2,2]", o, why, sizeof why) && contains(why, "built-in size"));
        snprintf(tag, sizeof tag, "%s: wordmark at its built-in size allowed", BKEY[b]);
        r = bc.wordmark; r.y = 0; r.h = 44;
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
        r = {bs.radar.x + bs.radar.w / 2 - 30, bs.radar.y + bs.radar.w / 2 - 30, 60, 60};
        check(tag, !R(b, "\"count.wifi\":" + rectJson(r), o, why, sizeof why) && contains(why, "radar"));
        snprintf(tag, sizeof tag, "%s: tile over a tile refused", BKEY[b]);
        check(tag, !R(b, "\"count.wifi\":" + rectJson(bs.tiles[1]), o, why, sizeof why) && contains(why, "overlaps"));

        snprintf(tag, sizeof tag, "%s: too little room refused", BKEY[b]);
        r = {0, H / 2 - 22, W, 44};
        check(tag, !R(b, "\"menu\":" + rectJson(r), o, why, sizeof why) && contains(why, "too little room"));
    }
}

// Plan 2 Task 3: the radar group (radar, spectrum, channel, badge).
static void testRadarGroup() {
    char why[96], tag[96];
    for (int b = 0; b < B_N; b++) {
        const int W = BW[b];
        const uieng::ScanLayout bs = uieng::builtinScan(W, BH[b]);
        uieng::ChromeOverride o;
        auto T = [&](const char* name, bool ok) { snprintf(tag, sizeof tag, "%s: %s", BKEY[b], name); check(tag, ok); };

        T("defaults equal the built-in parts", R(b, "", o, why, sizeof why) && same(o.radar, bs.radar) && same(o.spectrum, bs.spectrum) &&
                                                  same(o.channel, bs.channel) && same(o.badge, bs.badge) && same(o.hero, bs.hero));
        T("defaultRect knows the group", same(defaultRect((Board)b, P_RADAR), bs.radar) && same(defaultRect((Board)b, P_BADGE), bs.badge));
        uieng::Rect r = bs.radar; r.w -= 2;
        T("radar must be square", !R(b, "\"radar\":" + rectJson(r), o, why, sizeof why) && contains(why, "square"));
        r = {bs.radar.x, bs.radar.y, 52, 52};
        T("radar under 53 refused", !R(b, "\"radar\":" + rectJson(r), o, why, sizeof why) && contains(why, "radar"));
        r = {bs.radar.x, bs.radar.y, bs.radar.w + 2, bs.radar.w + 2};
        T("radar over today's box refused", !R(b, "\"radar\":" + rectJson(r), o, why, sizeof why) && contains(why, "radar"));
        r = {bs.radar.x, bs.radar.y, 101, 101};
        T("radar 101 accepted", R(b, "\"radar\":" + rectJson(r), o, why, sizeof why) && same(o.radar, r));
        r = bs.spectrum; r.w = 41;
        T("spectrum under 42 wide refused", !R(b, "\"spectrum\":" + rectJson(r), o, why, sizeof why) && contains(why, "spectrum"));
        r = bs.spectrum; r.h = 21;
        T("spectrum under 22 tall refused", !R(b, "\"spectrum\":" + rectJson(r), o, why, sizeof why) && contains(why, "spectrum"));
        r = bs.channel; r.w -= 1;
        T("channel smaller than built-in refused", !R(b, "\"channel\":" + rectJson(r), o, why, sizeof why) && contains(why, "built-in size"));
        r = {bs.badge.x, bs.badge.y, 23, 23};
        T("badge under 24 refused", !R(b, "\"badge\":" + rectJson(r), o, why, sizeof why) && contains(why, "badge"));
        T("all four hideable", R(b, "\"radar\":\"hidden\",\"spectrum\":\"hidden\",\"channel\":\"hidden\",\"badge\":\"hidden\"", o, why, sizeof why) &&
                               o.radar.w == 0 && o.spectrum.w == 0 && o.channel.w == 0 && o.badge.w == 0 && same(o.hero, bs.hero));
        T("the group may overlap itself (as the built-in does)", R(b, "\"badge\":" + rectJson({bs.radar.x, bs.radar.y, 40, 40}), o, why, sizeof why));
        r = {bs.tiles[0].x, bs.tiles[0].y, 60, 60};
        T("radar over a tile refused", !R(b, "\"radar\":" + rectJson(r), o, why, sizeof why) && contains(why, "overlaps"));
        // hero = bounding box of the visible group
        r = {bs.radar.x, bs.radar.y, 61, 61};
        const bool ok = R(b, "\"radar\":" + rectJson(r) + ",\"spectrum\":\"hidden\",\"channel\":\"hidden\",\"badge\":\"hidden\"", o, why, sizeof why);
        T("hero is the bounding box of the visible group", ok && same(o.hero, r));
        // Review I1: the group's bounding box (the alert area) must not enclose a tile or a chrome piece.
        const std::string enclose = b == 0 ? "\"radar\":[124,120,101,101],\"count.wifi\":[240,120,100,60]"
                                           : "\"radar\":[104,52,53,53],\"count.wifi\":[170,80,60,60]";
        T("radar parts enclosing a tile refused", !R(b, enclose, o, why, sizeof why) && contains(why, "enclose"));
    }
}

// ---------- styles, style block, decor (plan 4) ----------------------------------
static bool PS(const std::string& s, uieng::BoardLook* looks, char* why = nullptr, size_t cap = 0) {
    Layout L;
    return parse(s.data(), s.size(), L, looks, why, cap);
}
static std::string W1(const std::string& board) { return "{\"format\":1,\"waveshare\":{" + board + "}}"; }

static void testStyles() {
    Layout L; char why[96];
    uieng::BoardLook looks[B_N];

    check("object form equals the array form",
          parse(W1("\"menu\":{\"rect\":[0,276,44,44]}").data(), W1("\"menu\":{\"rect\":[0,276,44,44]}").size(), L, nullptr, 0) &&
          L.slot[B_WAVESHARE][P_MENU].how == SLOT_PLACED && L.slot[B_WAVESHARE][P_MENU].x == 0 &&
          L.slot[B_WAVESHARE][P_MENU].y == 276 && L.slot[B_WAVESHARE][P_MENU].w == 44);
    check("no style keys: look.any false",
          PS(W1("\"menu\":[0,276,44,44]"), looks) && !looks[B_WAVESHARE].any && !looks[B_CYD].any);
    check("object form with only rect: look.any false", PS(W1("\"menu\":{\"rect\":[0,276,44,44]}"), looks) && !looks[B_WAVESHARE].any);

    check("each style key lands", PS(W1("\"wordmark\":{\"rect\":[60,0,200,44],\"font\":\"f3\",\"scale\":2,\"ink\":\"bright\","
                                       "\"fill\":\"#ff0000\",\"edge\":\"edgeLit\",\"frame\":\"a2\",\"slice\":[4,5,6,7],\"bg\":\"a11\"}"), looks) && [&] {
        const uieng::PieceStyle& s = looks[B_WAVESHARE].piece[P_WORDMARK];
        return looks[B_WAVESHARE].any && s.font == 3 && s.scale == 2 && s.ink.kind == 1 && s.ink.v == 7 &&
               s.fill.kind == 2 && s.fill.v == 0xF800 && s.edge.kind == 1 && s.edge.v == 4 && s.frame == 2 &&
               s.slice[0] == 4 && s.slice[1] == 5 && s.slice[2] == 6 && s.slice[3] == 7 && s.bg == 11 &&
               looks[B_WAVESHARE].fontsUsed == (1u << 3) && looks[B_WAVESHARE].artsUsed == ((1u << 2) | (1u << 11));
    }());
    check("upper-case hex", PS(W1("\"menu\":{\"rect\":[0,276,44,44],\"fill\":\"#00FF00\"}"), looks) &&
          looks[B_WAVESHARE].piece[P_MENU].fill.v == 0x07E0);
    check("unknown key in a piece object is skipped (plan 5's effect)",
          PS(W1("\"menu\":{\"rect\":[0,276,44,44],\"effect\":\"flash\",\"speed\":\"fast\"}"), looks));

    struct Bad { const char* what; std::string json; const char* reason; };
    const Bad bad[] = {
        {"piece object without rect", W1("\"menu\":{\"font\":\"f0\"}"), "rect"},
        {"unknown role name", W1("\"menu\":{\"rect\":[0,276,44,44],\"ink\":\"purple\"}"), "colour"},
        {"short hex", W1("\"menu\":{\"rect\":[0,276,44,44],\"ink\":\"#fff\"}"), "colour"},
        {"bad hex digit", W1("\"menu\":{\"rect\":[0,276,44,44],\"ink\":\"#ggffff\"}"), "colour"},
        {"font f8", W1("\"menu\":{\"rect\":[0,276,44,44],\"font\":\"f8\"}"), "font"},
        {"art a16", W1("\"menu\":{\"rect\":[0,276,44,44],\"frame\":\"a16\"}"), "art"},
        {"art g1", W1("\"menu\":{\"rect\":[0,276,44,44],\"bg\":\"g1\"}"), "art"},
        {"font a1", W1("\"menu\":{\"rect\":[0,276,44,44],\"font\":\"a1\"}"), "font"},
        {"scale 0", W1("\"menu\":{\"rect\":[0,276,44,44],\"scale\":0}"), "scale"},
        {"scale 5", W1("\"menu\":{\"rect\":[0,276,44,44],\"scale\":5}"), "scale"},
        {"slice of 3", W1("\"menu\":{\"rect\":[0,276,44,44],\"slice\":[1,2,3]}"), "slice"},
        {"slice 256", W1("\"menu\":{\"rect\":[0,276,44,44],\"slice\":[1,2,3,256]}"), "slice"},
        {"duplicate key in a piece", W1("\"menu\":{\"rect\":[0,276,44,44],\"font\":\"f0\",\"font\":\"f1\"}"), "duplicate"},
        {"decor.8", W1("\"decor.8\":{\"rect\":[0,0,10,10],\"art\":\"a0\"}"), "decor"},
        {"decor.x", W1("\"decor.x\":{\"rect\":[0,0,10,10],\"art\":\"a0\"}"), "decor"},
        {"decor without art", W1("\"decor.0\":{\"rect\":[0,0,10,10]}"), "art"},
        {"decor without rect", W1("\"decor.0\":{\"art\":\"a0\"}"), "rect"},
        {"duplicate decor", W1("\"decor.1\":{\"rect\":[0,0,10,10],\"art\":\"a0\"},\"decor.1\":{\"rect\":[0,0,10,10],\"art\":\"a0\"}"), "duplicate"},
        {"icons: 7 entries", W1("\"style\":{\"icons\":[\"a0\",null,null,null,null,null,\"a1\"]}"), "icons"},
        {"style: font ref in bg", W1("\"style\":{\"bg\":\"f0\"}"), "art"},
        {"style: bad role font", W1("\"style\":{\"title\":\"a0\"}"), "font"},
        {"style: duplicate", W1("\"style\":{},\"style\":{}"), "duplicate"},
        {"depth 5", W1("\"menu\":{\"rect\":[0,276,44,44],\"x\":{\"y\":[1]}}"), "deep"},
    };
    for (const Bad& b : bad) {
        why[0] = 0;
        const bool ok = PS(b.json, looks, why, sizeof why);
        char label[160]; snprintf(label, sizeof label, "refused: %s (%s)", b.what, why);
        check(label, !ok && contains(why, b.reason));
    }

    check("style block lands", PS(W1("\"style\":{\"title\":\"f0\",\"body\":\"f1\",\"numbers\":\"f2\",\"bg\":\"a0\",\"panel\":\"a1\","
                                    "\"panelSlice\":[3,3,3,3],\"highlight\":{\"fill\":\"witch\",\"edge\":\"#0000ff\"},"
                                    "\"icons\":[\"a4\",null,\"a5\"]}"), looks) && [&] {
        const uieng::ScreenStyle& s = looks[B_WAVESHARE].screen;
        return looks[B_WAVESHARE].any && s.roleFont[0] == 1 && s.roleFont[1] == 0 && s.roleFont[2] == 2 &&   // body, title, numbers
               s.bg == 0 && s.panel == 1 && s.panelSlice[0] == 3 && s.hiFill.kind == 1 && s.hiEdge.kind == 2 &&
               s.hiEdge.v == 0x001F && s.icon[0] == 4 && s.icon[1] == -1 && s.icon[2] == 5 && s.icon[3] == -1 &&
               looks[B_WAVESHARE].fontsUsed == 7 && looks[B_WAVESHARE].artsUsed == ((1u << 0) | (1u << 1) | (1u << 4) | (1u << 5));
    }());
    check("decor lands by N", PS(W1("\"decor.0\":{\"rect\":[10,60,40,30],\"art\":\"a7\"},\"decor.5\":{\"rect\":[100,60,20,20],\"art\":\"a8\"}"), looks) && [&] {
        const uieng::BoardLook& k = looks[B_WAVESHARE];
        return k.any && k.nDecor == 2 && k.decor[0].art == 7 && k.decor[0].r == uieng::Rect{10, 60, 40, 30} &&
               k.decor[5].art == 8 && k.decor[1].art == -1 && k.artsUsed == ((1u << 7) | (1u << 8));
    }());

    // build(): contrast and decor checks need the board and the colours.
    uint16_t colors[20];
    for (int i = 0; i < 20; i++) colors[i] = 0x0000;
    colors[1] = 0x2104;   // panel: dark grey
    colors[6] = 0xFFFF;   // text: white
    colors[7] = 0xFFFF;   // bright
    PackLayout* pl = new PackLayout();
    const std::string lowContrast = W1("\"menu\":{\"rect\":[0,0,44,44],\"ink\":\"panel\",\"fill\":\"panel\"}");
    check("menu ink = fill refused for contrast",
          !build(lowContrast.data(), lowContrast.size(), *pl, why, sizeof why, colors) && contains(why, "contrast"));
    const std::string okContrast = W1("\"menu\":{\"rect\":[0,0,44,44],\"ink\":\"text\",\"fill\":\"panel\"}");
    check("menu text on panel accepted", build(okContrast.data(), okContrast.size(), *pl, why, sizeof why, colors) &&
          pl->lo.look[B_WAVESHARE] == &pl->look[B_WAVESHARE] && pl->lo.look[B_CYD] == nullptr);
    const std::string stopHex = W1("\"stop\":{\"rect\":[384,276,96,44],\"ink\":\"#101010\",\"fill\":\"#000000\"}");
    check("stop hex ink on hex fill under 3:1 refused", !build(stopHex.data(), stopHex.size(), *pl, why, sizeof why, colors) && contains(why, "stop"));
    const std::string wordmarkLow = W1("\"wordmark\":{\"rect\":[44,0,164,44],\"ink\":\"panel\",\"fill\":\"panel\"}");
    check("wordmark is not contrast-checked", build(wordmarkLow.data(), wordmarkLow.size(), *pl, why, sizeof why, colors));
    const std::string decorOff = W1("\"decor.0\":{\"rect\":[470,60,20,20],\"art\":\"a0\"}");
    check("decor off screen refused", !build(decorOff.data(), decorOff.size(), *pl, why, sizeof why, colors) && contains(why, "decor.0"));
    const std::string noStyle = W1("\"menu\":[0,0,44,44]");
    check("no style: lo.look null", build(noStyle.data(), noStyle.size(), *pl, why, sizeof why, colors) && pl->lo.look[B_WAVESHARE] == nullptr);

    // Size: a 16000-byte valid layout (padded with spaces) parses; 16385 bytes refused.
    std::string big = W1("\"menu\":[0,276,44,44]");
    big.insert(1, 16000 - big.size(), ' ');
    check("16000-byte layout parses", big.size() == 16000 && parse(big.data(), big.size(), L, nullptr, 0));
    std::string huge = big; huge.insert(1, MAX_JSON + 1 - huge.size(), ' ');
    check("16385-byte layout refused", huge.size() == MAX_JSON + 1 && !parse(huge.data(), huge.size(), L, why, sizeof why));
    check("MAX_JSON is 16384", MAX_JSON == 16384);
    delete pl;
}

// Fuzz the new shapes: whatever parses keeps every style reference in range.
static void testStyleFuzz() {
    const char* seeds[] = {
        "{\"format\":1,\"waveshare\":{\"menu\":{\"rect\":[0,276,44,44],\"font\":\"f3\",\"ink\":\"#ff00ff\",\"slice\":[1,2,3,4]}}}",
        "{\"format\":1,\"cyd\":{\"style\":{\"title\":\"f0\",\"icons\":[\"a4\",null],\"highlight\":{\"fill\":\"witch\"}}}}",
        "{\"format\":1,\"cyd\":{\"decor.3\":{\"rect\":[0,60,20,20],\"art\":\"a15\"}}}",
    };
    const char ins[] = "[]{}\",:-0123456789afn#";
    srand(4321);
    long accepted = 0;
    bool invariant = true;
    for (int it = 0; it < 100000; it++) {
        std::string s = seeds[it % 3];
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
        Layout L; char why[64]; uieng::BoardLook looks[B_N];
        char* buf = (char*)malloc(s.size() ? s.size() : 1);
        memcpy(buf, s.data(), s.size());
        bool ok = parse(buf, s.size(), L, looks, why, sizeof why);
        free(buf);
        if (!ok) continue;
        accepted++;
        for (int b = 0; b < B_N; b++) {
            const uieng::BoardLook& k = looks[b];
            for (int i = 0; i < P_N; i++) {
                const uieng::PieceStyle& ps = k.piece[i];
                if (ps.font < -1 || ps.font > 7 || ps.frame < -1 || ps.frame > 15 || ps.bg < -1 || ps.bg > 15 || ps.scale > 4) invariant = false;
                if (ps.ink.kind > 2 || (ps.ink.kind == 1 && ps.ink.v >= 20)) invariant = false;
            }
            int n = 0;
            for (int d = 0; d < uieng::MAX_DECOR; d++) if (k.decor[d].art >= 0) { n++; if (k.decor[d].art > 15) invariant = false; }
            if (n != k.nDecor) invariant = false;
            for (int i = 0; i < 6; i++) if (k.screen.icon[i] < -1 || k.screen.icon[i] > 15) invariant = false;
        }
    }
    printf("  style fuzz: 100000 mutations, %ld accepted\n", accepted);
    check("style fuzz invariant holds", invariant);
}

int main() {
    testStyles();
    testStyleFuzz();
    printf("== themelayout parse\n");
    testParse();
    testFuzz();
    printf("== themelayout resolve\n");
    testResolve();
    printf("== radar group\n");
    testRadarGroup();
    printf(fails ? "FAILED: %d\n" : "all passed\n", fails);
    return fails ? 1 : 0;
}
