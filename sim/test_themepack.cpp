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

    printf(fails ? "%d FAILED\n" : "all passed\n", fails);
    return fails ? 1 : 0;
}
