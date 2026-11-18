// test_packassets: the pack font/art registry (ui/packassets.h) answers only for the theme that owns the set.
#include "../ui/packassets.h"
#include "../ui/palette.h"
#include "fontfix.h"
#include "artfix.h"
#include <stdio.h>

static int fails = 0;
static void check(const char* what, bool ok) {
    if (!ok) { ++fails; printf("FAIL %s\n", what); }
}

int main() {
    const auto fb = fontfix::make();
    const auto ab = artfix::make(4, 4, 1, true);
    fontpack::Font f; artpack::Art a; const char* why = "";
    check("fixtures parse", fontpack::parse(fb.data(), fb.size(), f, &why) && artpack::parse(ab.data(), ab.size(), a, &why));

    pal::setTheme(3);
    check("currentTheme", pal::currentTheme() == 3);
    check("empty before set", assets::font(0) == nullptr && assets::art(0) == nullptr);

    const fontpack::Font* fonts[assets::MAX_FONTS] = {};
    const artpack::Art* arts[assets::MAX_ARTS] = {};
    fonts[2] = &f; arts[11] = &a;
    assets::set(3, fonts, arts);
    check("theme 3 active: its entries", assets::font(2) == &f && assets::art(11) == &a);
    check("unset entries stay null", assets::font(0) == nullptr && assets::art(0) == nullptr);
    check("out of range is null", assets::font(-1) == nullptr && assets::font(8) == nullptr && assets::art(16) == nullptr);
    pal::setTheme(4);
    check("theme 4 active: nothing", assets::font(2) == nullptr && assets::art(11) == nullptr);
    pal::setTheme(3);
    check("back to theme 3", assets::font(2) == &f);
    assets::clear(4);
    check("clear of another theme leaves the set", assets::font(2) == &f);
    assets::clear(3);
    check("clear(3) empties it", assets::font(2) == nullptr && assets::art(11) == nullptr);

    printf(fails ? "test_packassets: %d FAILED\n" : "test_packassets: all passed\n", fails);
    return fails ? 1 : 0;
}
