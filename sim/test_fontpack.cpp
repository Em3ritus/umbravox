// test_fontpack: the .fnt pixel font parser (ui/fontpack.h) on good and hostile input.
#include "../ui/fontpack.h"
#include "fontfix.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>

static int fails = 0;
static void check(const char* what, bool ok) {
    if (!ok) { ++fails; printf("FAIL %s\n", what); }
}

static bool parses(const std::vector<uint8_t>& d, fontpack::Font& f, const char** why) {
    *why = "";
    return fontpack::parse(d.data(), d.size(), f, why);
}

static void refused(const char* what, const std::vector<uint8_t>& d) {
    fontpack::Font f; const char* why = "";
    const bool ok = parses(d, f, &why);
    check(what, !ok && why && why[0]);
}

int main() {
    const std::vector<uint8_t> good = fontfix::make();
    fontpack::Font f; const char* why = "";
    check("valid font parses", parses(good, f, &why));
    check("height", f.h == 8 && f.ascent == 7);

    // 'A' pixels match the fixture rows.
    bool aOk = true;
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 5; ++x)
            if (fontpack::pixel(f, 'A', x, y) != (((fontfix::ROWS_A[y] << x) & 0x80) != 0)) aOk = false;
    check("'A' bitmap", aOk);
    check("'A' outside its width is blank", !fontpack::pixel(f, 'A', 5, 1) && !fontpack::pixel(f, 'A', -1, 1));
    check("'A' outside its height is blank", !fontpack::pixel(f, 'A', 0, 8) && !fontpack::pixel(f, 'A', 1, -1));
    bool differs = false;
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 5; ++x)
            if (fontpack::pixel(f, 'A', x, y) != fontpack::pixel(f, 'a', x, y)) differs = true;
    check("lowercase kept ('a' differs from 'A')", differs);
    check("'a' row 2", fontpack::pixel(f, 'a', 1, 2) && fontpack::pixel(f, 'a', 2, 2) && !fontpack::pixel(f, 'a', 0, 2));

    check("advance '~'", fontpack::advance(f, '~') == 4);
    check("advance DEL is 0", fontpack::advance(f, '\x7f') == 0);
    check("advance control char is 0", fontpack::advance(f, '\n') == 0);
    check("advance 'A'", fontpack::advance(f, 'A') == 6);
    // "Aa" at scale 2: advances 6+5 = 11, minus the last glyph's trailing gap (5-4 = 1) = 10, x2 = 20.
    check("width(\"Aa\",2)", fontpack::width(f, "Aa", 2) == 20);
    check("width(\"\")", fontpack::width(f, "", 3) == 0);
    check("width(\" \",1) has no negative gap", fontpack::width(f, " ", 1) == 0);

    // Refusals.
    { auto d = good; d[0] = 'X'; refused("bad magic", d); }
    { auto d = good; d[4] = 2; refused("version 2", d); }
    { auto d = good; d[5] = 5; refused("h 5", d); }
    { auto d = good; d[5] = 33; refused("h 33", d); }
    { auto d = good; d[8] = 94; refused("count 94", d); }
    { auto d = good; d[7] = 33; refused("first 33", d); }
    { auto d = good; d[12 + 33 * 4 + 1] = 33; refused("glyph w 33", d); }               // 'A' w
    { auto d = good; d[12 + 33 * 4 + 2] = 0xFF; d[12 + 33 * 4 + 3] = 0x0F; refused("offset past end", d); }
    { auto d = good; d.pop_back(); refused("last glyph runs past end", d); }
    { auto d = good; d.resize(fontpack::MAX_FONT + 1, 0); refused("over MAX_FONT", d); }
    { auto d = good; d.resize(11); refused("truncated header", d); }
    { auto d = good; d.resize(12 + 95 * 4 - 1); refused("truncated glyph table", d); }
    { std::vector<uint8_t> d; refused("empty", d); }

    // Random mutations: never a sanitizer error; a file that parses has every glyph in bounds (pixel() reads it all).
    srand(1234);
    int parsedMutants = 0;
    for (int i = 0; i < 2000; ++i) {
        auto d = good;
        const int n = 1 + rand() % 4;
        for (int k = 0; k < n; ++k) d[rand() % d.size()] = (uint8_t)rand();
        if (rand() % 5 == 0) d.resize(rand() % (d.size() + 1));
        fontpack::Font m; const char* w = "";
        if (fontpack::parse(d.data(), d.size(), m, &w)) {
            ++parsedMutants;
            for (int c = 32; c < 127; ++c)
                for (int y = 0; y < m.h; ++y)
                    for (int x = 0; x < fontpack::FONT_MAX_GLYPH_W; ++x) (void)fontpack::pixel(m, (char)c, x, y);
            (void)fontpack::width(m, "Hello, world ~", 2);
        } else {
            check("mutant refusal has a reason", w && w[0]);
        }
    }
    printf("mutants parsed: %d of 2000\n", parsedMutants);

    printf(fails ? "test_fontpack: %d FAILED\n" : "test_fontpack: all passed\n", fails);
    return fails ? 1 : 0;
}
