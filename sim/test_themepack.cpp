// Host test for the theme.json parser. A pack is untrusted data, so this is
// built under the address and UB sanitizers (see the Makefile) and ends with a
// mutation fuzz whose assertion is an invariant, not mere survival.
#include "../ui/themepack.h"
#include "../ui/palette.h"
#include "../ui/model.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>

using namespace themepack;

static int fails = 0;
static void check(const char* what, bool ok) {
    printf(ok ? "  ok   %s\n" : "  FAIL %s\n", what);
    if (!ok) fails++;
}

static const char* ROLES[N_COLORS] = {
    "void", "panel", "grid", "edge", "edgeLit", "dim", "text", "bright",
    "witch", "witchHi", "bone", "sweep", "sweepHi", "live", "liveHi",
    "magenta", "ice", "green", "amber", "red"};

// Builds a valid pack; each case replaces one piece.
struct Base {
    std::string format = "1", name = "\"MIDNIGHT EMBER\"", author = "\"@me\"",
                header = "\"MIDNIGHT EMBER\"", dial = "\"large\"",
                centre = "\"emblem\"", sweeps = "[\"radar\",\"galaxy\",\"orbit\"]",
                sweep = "\"galaxy\"", alert = "\"supernova\"", extra = "";
    std::string col[N_COLORS];
    bool skipCol[N_COLORS] = {};
    Base() {
        for (int i = 0; i < N_COLORS; i++) {
            char b[16];
            snprintf(b, sizeof b, "\"#%02X%02X%02X\"", i * 11, 7 + i, 28 + i);
            col[i] = b;
        }
        col[0] = "\"#09071C\"";
    }
    std::string str() const {
        std::string s = "{\"format\":" + format + ",\"name\":" + name +
            ",\"author\":" + author + ",\"header\":" + header + ",\"dial\":" + dial +
            ",\"centrepiece\":" + centre + ",\"sweeps\":" + sweeps +
            ",\"sweep\":" + sweep + ",\"alert\":" + alert + extra + ",\"colors\":{";
        bool first = true;
        for (int i = 0; i < N_COLORS; i++) {
            if (skipCol[i]) continue;
            if (!first) s += ",";
            first = false;
            s += std::string("\"") + ROLES[i] + "\":" + col[i];
        }
        return s + "}}";
    }
};

static bool run(const std::string& s, Pack& p, char* why) {
    why[0] = 0;
    // Heap copy of exactly n bytes: ASan catches any read past the end.
    char* h = (char*)malloc(s.size() ? s.size() : 1);
    memcpy(h, s.data(), s.size());
    bool ok = parse(h, s.size(), p, why, 64);
    free(h);
    return ok;
}

static void rejects(const char* what, const std::string& s) {
    Pack p; char why[64];
    bool ok = run(s, p, why);
    check(what, !ok && why[0] != 0);
}
static void accepts(const char* what, const std::string& s) {
    Pack p; char why[64];
    check(what, run(s, p, why));
}

static std::string q(int n, char c) { return "\"" + std::string(n, c) + "\""; }

// The invariant every accepted pack must satisfy.
static bool sane(const Pack& p) {
    if (strlen(p.name) < 1 || strlen(p.name) > (size_t)MAX_NAME) return false;
    if (strlen(p.header) < 1 || strlen(p.header) > (size_t)MAX_HEADER) return false;
    if (strlen(p.author) > (size_t)MAX_AUTHOR) return false;
    for (const char* c = p.header; *c; c++) if (*c < 32 || *c > 95) return false;
    if (!p.sweeps || p.sweep >= ui::SW_COUNT || !(p.sweeps & (1u << p.sweep))) return false;
    if (p.sweeps & (1u << ui::SW_GAZE)) return false;
    if (p.sweeps >> ui::SW_COUNT) return false;
    return true;
}


// ---- emblem.bin ----------------------------------------------------------
typedef std::string Bytes;

// A valid emblem: header, ncol palette entries, w*h indices (i % ncol).
static Bytes emblem(int w, int h, int ncol) {
    Bytes b;
    b += "UVE"; b += (char)1;
    b += (char)w; b += (char)h; b += (char)ncol; b += (char)0;
    for (int i = 0; i < ncol; i++) { b += (char)(0x10 + i); b += (char)(0xF0 - i); }
    for (int i = 0; i < w * h; i++) b += (char)(ncol ? i % ncol : 0);
    return b;
}

static bool saneE(const Emblem& e) {
    if (e.w < 1 || e.w > MAX_EMBLEM_DIM || e.h < 1 || e.h > MAX_EMBLEM_DIM) return false;
    if (e.ncol < 2 || e.ncol > MAX_EMBLEM_COLORS) return false;
    if (!e.px) return false;
    for (int i = 0; i < e.w * e.h; i++) if (e.px[i] >= e.ncol) return false;
    return true;
}

// Runs parseEmblem on a heap copy of exactly n bytes (ASan sees any overread).
static bool runE(const Bytes& s, Emblem& e, char* why, bool* sane = nullptr) {
    why[0] = 0;
    uint8_t* h = (uint8_t*)malloc(s.size() ? s.size() : 1);
    memcpy(h, s.data(), s.size());
    bool ok = parseEmblem(h, s.size(), e, why, 64);
    if (sane) *sane = ok && saneE(e);  // px is only valid while h is alive
    free(h);
    return ok;
}
// Rejected, and for the intended reason (a substring of why).
static void rejectsE(const char* what, const Bytes& s, const char* reason) {
    Emblem e; char why[64];
    bool ok = runE(s, e, why);
    bool r = !ok && strstr(why, reason) != nullptr;
    if (!r) printf("       why = \"%s\", wanted \"%s\"\n", why, reason);
    check(what, r);
}
static void acceptsE(const char* what, const Bytes& s) {
    Emblem e; char why[64];
    check(what, runE(s, e, why));
}
static void emblemTests() {
    Emblem e; char why[64];
    {
        Bytes b = emblem(72, 72, 16);
        bool ok = runE(b, e, why);
        check("72x72/16 emblem parses", ok);
        check("emblem fields", e.w == 72 && e.h == 72 && e.ncol == 16);
        check("palette is little-endian", e.pal[0] == 0xF010 && e.pal[1] == 0xEF11);
        // px aliases the caller's buffer; runE has freed its copy, so re-parse
        // straight from b, which is still alive.
        Emblem e2;
        bool ok2 = parseEmblem((const uint8_t*)b.data(), b.size(), e2, why, sizeof why);
        check("px points into the caller's buffer at the indices",
              ok2 && e2.px == (const uint8_t*)b.data() + 8 + 32 && e2.px[5] == 5 && e2.px[16] == 0);
    }
    acceptsE("96x96 ok", emblem(96, 96, 16));
    acceptsE("1x1 ok", emblem(1, 1, 2));
    rejectsE("97x96 rejected", emblem(97, 96, 4), "size must be 1-96");
    rejectsE("96x97 rejected", emblem(96, 97, 4), "size must be 1-96");
    rejectsE("w 0 rejected", emblem(0, 8, 4), "size must be 1-96");
    rejectsE("h 0 rejected", emblem(8, 0, 4), "size must be 1-96");
    rejectsE("ncol 1 rejected", emblem(8, 8, 1), "colours must be 2-16");
    rejectsE("ncol 0 rejected", emblem(8, 8, 0), "colours must be 2-16");
    rejectsE("ncol 17 rejected", emblem(8, 8, 17), "colours must be 2-16");
    acceptsE("ncol 2 ok", emblem(8, 8, 2));
    acceptsE("ncol 16 ok", emblem(8, 8, 16));
    { Bytes b = emblem(8, 8, 4); b[0] = 'X'; rejectsE("wrong magic rejected", b, "bad magic"); }
    { Bytes b = emblem(8, 8, 4); b[3] = 2; rejectsE("version 2 rejected", b, "version"); }
    { Bytes b = emblem(8, 8, 4); b[3] = 0; rejectsE("version 0 rejected", b, "version"); }
    { Bytes b = emblem(8, 8, 4); b[7] = 1; rejectsE("reserved byte rejected", b, "reserved"); }
    {   // every proper prefix of a valid emblem is rejected as truncated
        Bytes b = emblem(8, 8, 4);
        int bad = 0;
        for (size_t n = 0; n < b.size(); n++) {
            Emblem t; char w[64];
            if (runE(b.substr(0, n), t, w) || !strstr(w, "truncated")) bad++;
        }
        check("every truncation 0..full-1 rejected as truncated", bad == 0);
    }
    { Bytes b = emblem(8, 8, 4); b += (char)0;
      rejectsE("one trailing byte rejected", b, "trailing"); }
    { Bytes b = emblem(8, 8, 4); b[8 + 8 + 63] = 4;
      rejectsE("last index == ncol rejected", b, "index"); }
    { Bytes b = emblem(8, 8, 4); b[8 + 8] = (char)255;
      rejectsE("first index 255 rejected", b, "index"); }
    {   // Size cap: a valid 96x96/16 header (9256 bytes) padded past the cap.
        // Legal dims/ncol top out at 9256, so the cap is only reachable with
        // trailing data; at exactly the cap the trailing check must fire
        // instead, which proves the cap is not firing early.
        Bytes b = emblem(96, 96, 16);
        b.resize(MAX_EMBLEM, 0);
        rejectsE("9728 bytes: trailing, not the cap", b, "trailing");
        b.push_back(0);
        rejectsE("9729 bytes rejected by the size cap", b, "too large");
    }
    rejectsE("empty emblem rejected", "", "truncated");
    {   // null pointer and whyCap tolerance
        Emblem t; char w[8];
        check("null data rejected", !parseEmblem(nullptr, 0, t, w, sizeof w));
        Bytes b = emblem(8, 8, 4);
        b[0] = 'X';
        check("whyCap 0 tolerated",
              !parseEmblem((const uint8_t*)b.data(), b.size(), t, nullptr, 0));
        bool ok = parseEmblem((const uint8_t*)b.data(), b.size(), t, w, sizeof w);
        check("emblem why truncated and terminated", !ok && strlen(w) == 7);
    }
    {   // Mutation fuzz with the invariant: sane accept or reasoned reject.
        const Bytes seeds[] = {emblem(8, 8, 4), emblem(3, 5, 16), emblem(1, 1, 2)};
        srand(0xE3B1E3);
        int accepted = 0, bad = 0;
        for (int it = 0; it < 20000; it++) {
            Bytes s = seeds[it % 3];
            int muts = 1 + rand() % 4;
            for (int m = 0; m < muts && !s.empty(); m++) {
                size_t pos = (size_t)rand() % s.size();
                switch (rand() % 3) {
                case 0: s[pos] = (char)(rand() & 0xFF); break;
                case 1: s.insert(s.begin() + pos, (char)(rand() & 0xFF)); break;
                default: s.erase(pos, 1);
                }
            }
            Emblem fe; char fw[64];
            bool sn = false;
            bool ok = runE(s, fe, fw, &sn);
            if (ok) { accepted++; if (!sn) bad++; }
            else if (!fw[0]) bad++;
        }
        printf("  emblem fuzz: %d accepted of 20000\n", accepted);
        check("emblem fuzz: invariant held for every mutation", bad == 0);
    }
}

static Pack named(const char* n) {
    Pack p; memset(&p, 0, sizeof p);
    strncpy(p.name, n, MAX_NAME);
    return p;
}

static void admissionTests() {
    // folder names
    check("folder ok_1-A ok", folderNameOk("ok_1-A"));
    check("folder 1 char ok", folderNameOk("a"));
    check("folder 31 chars ok", folderNameOk(std::string(31, 'a').c_str()));
    check("folder 32 chars rejected", !folderNameOk(std::string(32, 'a').c_str()));
    check("folder empty rejected", !folderNameOk(""));
    check("folder . rejected", !folderNameOk("."));
    check("folder .. rejected", !folderNameOk(".."));
    check("folder a/b rejected", !folderNameOk("a/b"));
    check("folder a\\b rejected", !folderNameOk("a\\b"));
    check("folder with space rejected", !folderNameOk("a b"));
    check("folder non-ASCII rejected", !folderNameOk("\xC3\xA9"));
    check("folder high byte rejected", !folderNameOk("a\xFF"));
    check("folder dot inside rejected", !folderNameOk("a.b"));
    check("folder newline rejected", !folderNameOk("a\n"));
    check("folder NULL rejected", !folderNameOk(nullptr));

    // admit
    const char* none[1] = {nullptr};
    check("admit plain ok", admit("neon", named("NEON"), none, 0) == ADMIT_OK);
    check("admit bad folder", admit("a/b", named("NEON"), none, 0) == ADMIT_BAD_FOLDER);
    check("admit empty folder", admit("", named("NEON"), none, 0) == ADMIT_BAD_FOLDER);
    check("admit NULL folder", admit(nullptr, named("NEON"), none, 0) == ADMIT_BAD_FOLDER);
    check("admit reserved UMBRAVOX", admit("a", named("UMBRAVOX"), none, 0) == ADMIT_RESERVED);
    check("admit reserved CVLT", admit("a", named("CVLT OF BAPH0MAU5"), none, 0) == ADMIT_RESERVED);
    check("admit reserved COSMIC MUFFIN", admit("a", named("COSMIC MUFFIN"), none, 0) == ADMIT_RESERVED);
    check("admit reserved BAPH0MAU5 (the built-in's displayed name)", admit("a", named("BAPH0MAU5"), none, 0) == ADMIT_RESERVED);
    check("BAPH0MAU not reserved", admit("a", named("BAPH0MAU"), none, 0) == ADMIT_OK);
    check("BAPH0MAU55 not reserved", admit("a", named("BAPH0MAU55"), none, 0) == ADMIT_OK);
    check("lowercase baph0mau5 not reserved", admit("a", named("baph0mau5"), none, 0) == ADMIT_OK);
    check("COSMIC MVFFIN (masthead only) not reserved", admit("a", named("COSMIC MVFFIN"), none, 0) == ADMIT_OK);
    check("COSMIC MUFFIN<space> not reserved", admit("a", named("COSMIC MUFFIN "), none, 0) == ADMIT_OK);
    check("UMBRAVOX2 not reserved", admit("a", named("UMBRAVOX2"), none, 0) == ADMIT_OK);
    check("UMBRAVO not reserved", admit("a", named("UMBRAVO"), none, 0) == ADMIT_OK);
    check("lowercase umbravox not reserved (case-sensitive)", admit("a", named("umbravox"), none, 0) == ADMIT_OK);
    {
        const char* taken[2] = {"ONE", "TWO"};
        check("duplicate of first rejected", admit("a", named("ONE"), taken, 2) == ADMIT_DUPLICATE);
        check("duplicate of last rejected", admit("a", named("TWO"), taken, 2) == ADMIT_DUPLICATE);
        check("near-duplicate ONE2 ok", admit("a", named("ONE2"), taken, 2) == ADMIT_OK);
        check("duplicate case-sensitive", admit("a", named("one"), taken, 2) == ADMIT_OK);
        check("nTaken bounds the scan", admit("a", named("TWO"), taken, 1) == ADMIT_OK);
        const char* holes[2] = {nullptr, "TWO"};
        check("NULL taken entry skipped", admit("a", named("TWO"), holes, 2) == ADMIT_DUPLICATE);
        check("NULL taken array with n 0 ok", admit("a", named("ONE"), nullptr, 0) == ADMIT_OK);
        check("NULL taken array with n>0 does not crash", admit("a", named("ONE"), nullptr, 3) == ADMIT_OK);
    }
    {
        const char* t[MAX_PACKS + 1] = {"P0","P1","P2","P3","P4","P5","P6","P7","P8"};
        check("8th pack admitted (7 taken)", admit("a", named("NEW"), t, MAX_PACKS - 1) == ADMIT_OK);
        check("9th pack full (8 taken)", admit("a", named("NEW"), t, MAX_PACKS) == ADMIT_FULL);
        check("full beyond 8 too", admit("a", named("NEW"), t, MAX_PACKS + 1) == ADMIT_FULL);
        check("bad folder wins over full", admit("a/b", named("NEW"), t, MAX_PACKS) == ADMIT_BAD_FOLDER);
    }

    // reasons
    const Admit all[] = {ADMIT_OK, ADMIT_BAD_FOLDER, ADMIT_RESERVED, ADMIT_DUPLICATE, ADMIT_FULL};
    bool why_ok = true;
    for (Admit a : all) {
        const char* w = admitWhy(a);
        if (!w || !*w || w[strlen(w) - 1] == '\n') why_ok = false;
    }
    check("admitWhy non-empty, no trailing newline", why_ok);
    bool distinct = true;
    for (int i = 0; i < 5; i++) for (int j = i + 1; j < 5; j++)
        if (!strcmp(admitWhy(all[i]), admitWhy(all[j]))) distinct = false;
    check("admitWhy distinct per value", distinct);
    check("admitWhy out-of-range value still a string",
          admitWhy((Admit)99) && *admitWhy((Admit)99));

    // sortNames
    {
        char n[4][32] = {"b", "a", "B", "_x"};
        sortNames(n, 4);
        check("sort: uppercase < _ < lowercase", !strcmp(n[0], "B") && !strcmp(n[1], "_x") &&
              !strcmp(n[2], "a") && !strcmp(n[3], "b"));
        char m[4][32] = {"a", "-", "9", "A"};
        sortNames(m, 4);
        check("sort: - < 9 < A < a", !strcmp(m[0], "-") && !strcmp(m[1], "9") &&
              !strcmp(m[2], "A") && !strcmp(m[3], "a"));
        char p[3][32] = {"ab", "a", "abc"};
        sortNames(p, 3);
        check("sort: prefix before longer", !strcmp(p[0], "a") && !strcmp(p[1], "ab") && !strcmp(p[2], "abc"));
        char z[2][32]; memset(z, 0, sizeof z);
        sortNames(z, 0); sortNames(z, 1);
        check("sort: n=0 and n=1 untouched", z[0][0] == 0 && z[1][0] == 0);
        char one[1][32] = {"solo"};
        sortNames(one, 1);
        check("sort: n=1 keeps entry", !strcmp(one[0], "solo"));
        sortNames(nullptr, 3);
        sortNames(one, -1);
        check("sort: NULL / negative n harmless", !strcmp(one[0], "solo"));
        // equal strings: stable (tag the bytes after the NUL to tell them apart)
        char s[3][32]; memset(s, 0, sizeof s);
        strcpy(s[0], "x"); s[0][5] = 'A';
        strcpy(s[1], "a");
        strcpy(s[2], "x"); s[2][5] = 'B';
        sortNames(s, 3);
        check("sort: equal strings keep order", !strcmp(s[0], "a") && s[1][5] == 'A' && s[2][5] == 'B');
        // unterminated 32-byte entries must not be read past
        struct { char a[32]; char b[32]; char guard[32]; } u;
        memset(&u, 'z', sizeof u);
        memset(u.a, 'y', 32);           // no NUL
        memcpy(u.b, "a", 2);
        memset(u.guard, 0, sizeof u.guard);
        char (*arr)[32] = (char (*)[32])&u;
        sortNames(arr, 2);
        check("sort: unterminated entry sorted by its 32 bytes", !strcmp(u.a, "a") && u.b[0] == 'y' && u.b[31] == 'y');
        // 31 vs 32 same prefix
        char q2[2][32];
        memset(q2[0], 'k', 32);
        memset(q2[1], 'k', 31); q2[1][31] = 0;
        sortNames(q2, 2);
        check("sort: 31-char prefix before 32-char", q2[0][31] == 0 && q2[1][31] == 'k');
        // determinism: same set in any order gives same first 8
        char r1[10][32], r2[10][32];
        const char* set[10] = {"j","c","a","i","e","b","h","d","g","f"};
        for (int i = 0; i < 10; i++) { memset(r1[i], 0, 32); memset(r2[i], 0, 32);
            strcpy(r1[i], set[i]); strcpy(r2[i], set[9 - i]); }
        sortNames(r1, 10); sortNames(r2, 10);
        bool same = true;
        for (int i = 0; i < 10; i++) if (strcmp(r1[i], r2[i])) same = false;
        check("sort: order independent of input order", same && !strcmp(r1[0], "a") && !strcmp(r1[7], "h"));
    }

    // boot-loop guard
    {
        char b[64];
        formatLoading(b, sizeof b, "foo", 1234);
        check("formatLoading foo:1234", !strcmp(b, "foo:1234"));
        formatLoading(b, sizeof b, "foo", 0);
        check("formatLoading size 0", !strcmp(b, "foo:0"));
        formatLoading(b, sizeof b, "foo", 4294967295u);
        check("formatLoading max u32", !strcmp(b, "foo:4294967295"));
        char c[6]; memset(c, 'Q', sizeof c);
        formatLoading(c, sizeof c, "foo", 1234);
        check("formatLoading truncates, NUL-terminated", !strcmp(c, "foo:1"));
        char d[9]; memset(d, 'Q', sizeof d);
        formatLoading(d, 4, "foo", 1234);
        check("formatLoading cap 4 -> 'foo' and nothing past cap",
              !strcmp(d, "foo") && d[4] == 'Q' && d[8] == 'Q');
        char e[3]; memset(e, 'Q', sizeof e);
        formatLoading(e, 1, "foo", 1);
        check("formatLoading cap 1 -> empty", e[0] == 0 && e[1] == 'Q');
        formatLoading(e, 0, "foo", 1);
        check("formatLoading cap 0 writes nothing", e[0] == 0 && e[1] == 'Q');
        formatLoading(nullptr, 8, "foo", 1);
        char g[8]; memset(g, 'Q', sizeof g);
        formatLoading(g, sizeof g, nullptr, 1);
        check("formatLoading NULL folder terminates", memchr(g, 0, sizeof g) != nullptr);
        std::string lng(100, 'a');
        char h[16];
        formatLoading(h, sizeof h, lng.c_str(), 99);
        check("formatLoading long folder bounded", strlen(h) == 15);

        check("skip: exact folder and size", skipAfterCrash("foo:1234", "foo", 1234));
        check("skip: size changed 1235", !skipAfterCrash("foo:1234", "foo", 1235));
        check("skip: size changed 1233", !skipAfterCrash("foo:1234", "foo", 1233));
        check("skip: other folder", !skipAfterCrash("foo:1234", "bar", 1234));
        check("skip: folder prefix", !skipAfterCrash("foo:1234", "fo", 1234));
        check("skip: folder extension", !skipAfterCrash("foo:1234", "foox", 1234));
        check("skip: empty record", !skipAfterCrash("", "foo", 1234));
        check("skip: no colon", !skipAfterCrash("foo", "foo", 1234));
        check("skip: just colon", !skipAfterCrash(":", "foo", 1234));
        check("skip: non-numeric size", !skipAfterCrash("foo:abc", "foo", 1234));
        check("skip: empty size", !skipAfterCrash("foo:", "foo", 1234));
        check("skip: trailing junk", !skipAfterCrash("foo:1234x", "foo", 1234));
        check("skip: trailing newline", !skipAfterCrash("foo:1234\n", "foo", 1234));
        check("skip: trailing space", !skipAfterCrash("foo:1234 ", "foo", 1234));
        check("skip: leading space", !skipAfterCrash(" foo:1234", "foo", 1234));
        check("skip: leading zero", !skipAfterCrash("foo:01234", "foo", 1234));
        check("skip: negative", !skipAfterCrash("foo:-1234", "foo", 1234));
        check("skip: size overflows u32 (wraps to 1234)", !skipAfterCrash("foo:4294968530", "foo", 1234));
        check("skip: huge digits", !skipAfterCrash("foo:99999999999999999999999", "foo", 1234));
        check("skip: u32 max matches", skipAfterCrash("foo:4294967295", "foo", 4294967295u));
        check("skip: size 0 matches", skipAfterCrash("foo:0", "foo", 0));
        check("skip: empty folder never", !skipAfterCrash(":5", "", 5));
        check("skip: invalid folder never", !skipAfterCrash("a/b:5", "a/b", 5));
        check("skip: NULL record", !skipAfterCrash(nullptr, "foo", 1));
        check("skip: NULL folder", !skipAfterCrash("foo:1", nullptr, 1));
        std::string big = "foo:1234" + std::string(500, '0');
        check("skip: record longer than any buffer", !skipAfterCrash(big.c_str(), "foo", 1234));
        std::string bigf = std::string(31, 'f') + ":1";
        check("skip: 31-char folder matches", skipAfterCrash(bigf.c_str(), std::string(31, 'f').c_str(), 1));
        // round trip
        char rt[64];
        formatLoading(rt, sizeof rt, std::string(31, 'z').c_str(), 65536);
        check("skip: round-trips what formatLoading wrote",
              skipAfterCrash(rt, std::string(31, 'z').c_str(), 65536) &&
              !skipAfterCrash(rt, std::string(31, 'z').c_str(), 65537));
    }
}

// keepName: a bounded listing keeps the `cap` smallest names whatever order
// the directory yields them in.
static void keepNameTests() {
    printf("keepName:\n");
    // 100 names, fed forwards and backwards into 8 slots.
    char all[100][32];
    for (int i = 0; i < 100; i++) snprintf(all[i], 32, "pack%03d", (i * 37) % 100);
    char fwd[8][32], rev[8][32];
    memset(fwd, 0, sizeof fwd); memset(rev, 0, sizeof rev);
    int nf = 0, nr = 0;
    for (int i = 0; i < 100; i++) nf = keepName(fwd, nf, 8, all[i]);
    for (int i = 99; i >= 0; i--) nr = keepName(rev, nr, 8, all[i]);
    check("keep: count stops at cap", nf == 8 && nr == 8);
    sortNames(fwd, nf); sortNames(rev, nr);
    bool same = true, smallest = true;
    for (int i = 0; i < 8; i++) {
        if (strcmp(fwd[i], rev[i])) same = false;
        char want[32]; snprintf(want, 32, "pack%03d", i);
        if (strcmp(fwd[i], want)) smallest = false;
    }
    check("keep: the same names whatever the order", same);
    check("keep: they are the cap smallest", smallest);

    char few[4][32]; memset(few, 0, sizeof few);
    int n = 0;
    n = keepName(few, n, 4, "b");
    n = keepName(few, n, 4, "a");
    check("keep: appends while there is room", n == 2 && !strcmp(few[0], "b") && !strcmp(few[1], "a"));
    char longName[40]; memset(longName, 'x', 39); longName[39] = 0;
    n = keepName(few, n, 4, longName);
    check("keep: a name of 32+ bytes is not kept", n == 2);
    char edge[33]; memset(edge, 'y', 31); edge[31] = 0;
    n = keepName(few, n, 4, edge);
    check("keep: a 31-byte name is kept whole", n == 3 && strlen(few[2]) == 31);
    n = keepName(few, n, 4, nullptr);
    check("keep: NULL is not kept", n == 3);
    check("keep: cap 0 keeps nothing", keepName(few, 0, 0, "a") == 0);
    check("keep: NULL array keeps nothing", keepName(nullptr, 0, 4, "a") == 0);
    // Full, and the new name is larger than everything held: dropped.
    char full[2][32] = {"a", "b"};
    n = keepName(full, 2, 2, "c");
    check("keep: full + larger name drops it", n == 2 && !strcmp(full[0], "a") && !strcmp(full[1], "b"));
    n = keepName(full, 2, 2, "B");
    check("keep: full + smaller name evicts the largest", n == 2 && !strcmp(full[0], "a") && !strcmp(full[1], "B"));
}

int main() {
    Pack p; char why[64];

    {
        Base b;
        bool ok = run(b.str(), p, why);
        check("valid pack parses", ok);
        check("name/author/header copied",
              !strcmp(p.name, "MIDNIGHT EMBER") && !strcmp(p.author, "@me") &&
              !strcmp(p.header, "MIDNIGHT EMBER"));
        check("dial/centre/rite", p.dial == DIAL_LARGE && p.centre == CENTRE_EMBLEM &&
                                  p.rite == RITE_SUPERNOVA);
        check("sweeps mask and choice",
              p.sweeps == ((1u << ui::SW_RADAR) | (1u << ui::SW_GALAXY) | (1u << ui::SW_ORBIT)) &&
              p.sweep == ui::SW_GALAXY);
        check("#09071C converts to pal::rgb(9,7,28)",
              p.color[R_VOID] == pal::rgb(9, 7, 28));
        check("role order: next colour", p.color[R_PANEL] == pal::rgb(11, 8, 29));
        check("last colour is red", p.color[R_RED] == pal::rgb(19 * 11, 26, 47));
    }
    { Base b; b.format = "2"; rejects("format 2 rejected", b.str()); }
    { Base b; b.format = "0"; rejects("format 0 rejected", b.str()); }
    { Base b; b.extra = ",\"future\":{\"a\":[1,2,{\"b\":null}],\"c\":true}";
      accepts("unknown key (nested) ignored", b.str()); }
    { Base b; b.name = ""; std::string s = b.str();
      size_t a = s.find("\"name\":"), e = s.find(",\"author\"");
      s.erase(a, e - a + 1); rejects("missing name rejected", s); }
    for (int i = 0; i < N_COLORS; i++) {
        Base b; b.skipCol[i] = true;
        char w[48]; snprintf(w, sizeof w, "missing colour %s rejected", ROLES[i]);
        rejects(w, b.str());
    }
    { Base b; b.name = q(20, 'A'); accepts("name 20 chars ok", b.str()); }
    { Base b; b.name = q(21, 'A'); rejects("name 21 chars rejected", b.str()); }
    { Base b; b.name = "\"\""; rejects("empty name rejected", b.str()); }
    { Base b; b.name = "\"Ember\""; rejects("lowercase name rejected", b.str()); }
    { Base b; b.name = "\"EMBER$\""; rejects("name with $ rejected", b.str()); }
    { Base b; b.name = "\"A-B 1.2\""; accepts("name punctuation - . ok", b.str()); }
    { Base b; b.header = q(18, 'X'); accepts("header 18 ok", b.str()); }
    { Base b; b.header = q(19, 'X'); rejects("header 19 rejected", b.str()); }
    { Base b; b.header = "\"ember 9\"";
      check("header lowercase folded", run(b.str(), p, why) && !strcmp(p.header, "EMBER 9")); }
    { Base b; b.header = "\"A{B\""; rejects("header char outside font rejected", b.str()); }
    { Base b; b.author = q(24, 'a'); accepts("author 24 ok", b.str()); }
    { Base b; b.author = q(25, 'a'); rejects("author 25 rejected", b.str()); }
    { Base b; b.sweeps = "[]"; rejects("empty sweeps rejected", b.str()); }
    { Base b; b.sweeps = "[\"radar\"]"; b.sweep = "\"galaxy\"";
      rejects("sweep not in sweeps rejected", b.str()); }
    { Base b; b.sweeps = "[\"radar\",\"gaze\"]"; b.sweep = "\"radar\"";
      rejects("gaze rejected", b.str()); }
    { Base b; b.sweeps = "[\"galaxy\"]"; b.sweep = "\"galaxy\"";
      accepts("radar absent, sweep galaxy accepted", b.str()); }
    { Base b; b.sweeps = "[\"radar\",\"pulse\",\"star\",\"candle\",\"galaxy\",\"orbit\","
                         "\"starfall\",\"pulsar\",\"comet\",\"aurora\"]";
      accepts("all ten sweeps accepted", b.str()); }
    { Base b; b.sweeps = "[\"warp\"]"; b.sweep = "\"radar\"";
      rejects("unknown sweep rejected", b.str()); }
    { Base b; b.alert = "\"summon\""; rejects("alert summon rejected", b.str()); }
    { Base b; b.alert = "\"ward\""; accepts("alert ward ok", b.str()); }
    { Base b; b.dial = "\"huge\""; rejects("bad dial rejected", b.str()); }
    { Base b; b.centre = "\"seal\""; rejects("bad centrepiece rejected", b.str()); }
    { Base b; b.col[3] = "\"#GG0000\""; rejects("bad hex rejected", b.str()); }
    { Base b; b.col[3] = "\"#12345\""; rejects("short hex rejected", b.str()); }
    { Base b; b.col[3] = "\"123456\""; rejects("missing # rejected", b.str()); }
    { Base b; b.col[3] = "\"#1234567\""; rejects("long hex rejected", b.str()); }
    { Base b; b.extra = ",\"dial\":\"compact\""; rejects("duplicate key rejected", b.str()); }
    { Base b; std::string s = b.str(); s.insert(s.size() - 2, ",\"void\":\"#000000\"");
      rejects("duplicate colour rejected", s); }
    { Base b; b.name = "5"; rejects("number for string rejected", b.str()); }
    { Base b; b.format = "\"1\""; rejects("string for number rejected", b.str()); }
    { Base b; b.sweeps = "\"radar\""; rejects("string for array rejected", b.str()); }

    {   // Size limit: pad with whitespace to exactly 4096 / 4097 bytes.
        Base b; std::string s = b.str();
        std::string at = s + std::string(MAX_JSON - s.size(), ' ');
        accepts("4096 bytes accepted", at);
        rejects("4097 bytes rejected", at + " ");
    }
    rejects("empty input rejected", "");
    rejects("lone { rejected", "{");
    rejects("[] rejected", "[]");
    { Base b; rejects("trailing garbage rejected", b.str() + "x"); }
    rejects("10000-deep nesting rejected", std::string(10000, '[') + std::string(10000, ']'));
    { Base b; b.extra = ",\"x\":" + std::string(500, '[') + std::string(500, ']');
      rejects("deep nesting inside a pack rejected", b.str()); }
    { Base b; b.author = "\"" + std::string(100 * 1024, 'a') + "\"";
      rejects("100 KB string rejected", b.str()); }
    { Base b; std::string s = b.str(); s[s.find("EMBER")] = '\0';
      rejects("embedded NUL rejected", s); }
    { Pack q0; char w[8];
      check("null json rejected", !parse(nullptr, 0, q0, w, sizeof w));
      check("whyCap 0 tolerated", !parse("{", 1, q0, nullptr, 0)); 
      bool ok = parse("{", 1, q0, w, sizeof w);
      check("why truncated and terminated", !ok && strlen(w) == 7); }

    {   // Mutation fuzz: flip/insert/delete bytes; result is either a sane
        // pack or a rejection with a reason. Sanitizers catch the rest.
        Base b; const std::string seed = b.str();
        srand(0xC0FFEE);
        int accepted = 0, bad = 0;
        for (int it = 0; it < 20000; it++) {
            std::string s = seed;
            int muts = 1 + rand() % 4;
            for (int m = 0; m < muts && !s.empty(); m++) {
                size_t pos = (size_t)rand() % s.size();
                switch (rand() % 3) {
                case 0: s[pos] = (char)(rand() & 0xFF); break;
                case 1: s.insert(s.begin() + pos, (char)(rand() & 0xFF)); break;
                default: s.erase(pos, 1);
                }
            }
            Pack fp; char fw[64];
            bool ok = run(s, fp, fw);
            if (ok) { accepted++; if (!sane(fp)) bad++; }
            else if (!fw[0]) bad++;
        }
        printf("  fuzz: %d accepted of 20000\n", accepted);
        check("fuzz: invariant held for every mutation", bad == 0);
    }

    emblemTests();
    admissionTests();
    keepNameTests();

    printf(fails ? "%d FAILED\n" : "all passed\n", fails);
    return fails ? 1 : 0;
}
