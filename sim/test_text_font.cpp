// test_text_font: scanr text through an active pack font (by role or by FontScope), and the 5x7 path unchanged.
#include "../ui/scan_render.h"
#include "fontfix.h"
#include <stdio.h>
#include <string.h>
#include <vector>

static int fails = 0;
static void check(const char* what, bool ok) {
    if (!ok) { ++fails; printf("FAIL %s\n", what); }
}

// A copy of the 5x7 txt() as it was before pack fonts: the reference for "unchanged".
static void refTxt(scanr::S& s, int gx, int gy, const char* t, int sc, uint16_t fg) {
    if (gy >= s.y0+s.h || gy+Font::H*sc <= s.y0) return;
    if (s.lo > 0 || s.hi < s.W) { int n = 0; while (t[n]) n++; if (gx >= s.hi || gx + n*Font::ADV*sc <= s.lo) return; }
    for (int i=0;t[i];i++) for (int ry=0;ry<Font::H;ry++){ uint8_t b=Font::row(t[i],ry);
        for (int cx=0;cx<Font::W;cx++){ if(!(b&(0x10>>cx))) continue;
            for (int ky=0;ky<sc;ky++) for (int kx=0;kx<sc;kx++)
                scanr::pt(s, gx+i*Font::ADV*sc+cx*sc+kx, gy+ry*sc+ky, fg); } }
}

static const int FW = 96, FH = 40;
static const uint16_t BG = 0x1234, INK = 0xFFFF;

// Draws `t` into a full FWxFH frame through strips of height `sh` and column window [lo,hi) (hi<0 = full).
template <class Fn>
static std::vector<uint16_t> render(int sh, int lo, int hi, Fn draw) {
    std::vector<uint16_t> frame(FW * FH, BG);
    const int xhi = hi < 0 ? FW : hi;
    for (int y = 0; y < FH; y += sh) {
        const int h = (y + sh > FH) ? FH - y : sh;
        std::vector<uint16_t> buf((size_t)h * (xhi - lo), BG);
        scanr::S s(buf.data(), FW, y, h, lo, hi);
        draw(s);
        for (int r = 0; r < h; ++r)
            for (int x = lo; x < xhi; ++x) frame[(y + r) * FW + x] = buf[(size_t)r * (xhi - lo) + (x - lo)];
    }
    return frame;
}

int main() {
    // 1. No font: identical to the reference routine at scales 1..3.
    for (int sc = 1; sc <= 3; ++sc) {
        auto a = render(FH, 0, -1, [&](scanr::S& s) { scanr::txt(s, 2, 3, "UMBRAVOX 123", sc, INK); });
        auto b = render(FH, 0, -1, [&](scanr::S& s) { refTxt(s, 2, 3, "UMBRAVOX 123", sc, INK); });
        check("5x7 unchanged", a == b);
    }
    check("5x7 lineH", scanr::lineH(2) == 14);
    check("5x7 textW", scanr::textW("ABC", 2) == Font::width("ABC", 2));
    check("5x7 charAdv", scanr::charAdv('A', 3) == Font::ADV * 3);
    check("5x7 glyphW", scanr::glyphW('A', 3) == Font::W * 3);

    const std::vector<uint8_t> bytes = fontfix::make();
    fontpack::Font f; const char* why = "";
    check("fixture parses", fontpack::parse(bytes.data(), bytes.size(), f, &why));

    {
        scanr::FontScope fs(&f, {0, 0, FW, FH});
        check("pack lineH", scanr::lineH(2) == 16);
        check("pack textW", scanr::textW("Aa", 1) == fontpack::width(f, "Aa", 1));
        check("pack charAdv", scanr::charAdv('A', 2) == 12);
        check("pack fitScale", scanr::fitScale("Aa", 30, 4) == 3);   // width 10/scale: 30 fits at 3
        // Lowercase: 'a' row 2 has pixels at x=1,2 (fixture), drawn at (10+1, 5+2).
        auto fr = render(FH, 0, -1, [&](scanr::S& s) { scanr::txt(s, 10, 5, "a", 1, INK); });
        check("lowercase 'a' drawn", fr[(5 + 2) * FW + 11] == INK && fr[(5 + 2) * FW + 10] == BG);
        // Strip and window invariance.
        auto full = render(FH, 0, -1, [&](scanr::S& s) { scanr::txt(s, 3, 4, "AaA aA", 2, INK); });
        auto strips = render(4, 0, -1, [&](scanr::S& s) { scanr::txt(s, 3, 4, "AaA aA", 2, INK); });
        check("pack text: 4-row strips equal one strip", full == strips);
        auto win = render(FH, 16, 48, [&](scanr::S& s) { scanr::txt(s, 3, 4, "AaA aA", 2, INK); });
        bool same = true;
        for (int y = 0; y < FH; ++y) for (int x = 16; x < 48; ++x) if (win[y * FW + x] != full[y * FW + x]) same = false;
        check("pack text: column window equals the full render there", same);
    }
    {
        // Clip: text at x=50 in a clip {0,0,60,32} leaves columns >= 60 and rows >= 32 untouched.
        scanr::FontScope fs(&f, {0, 0, 60, 32});
        auto fr = render(FH, 0, -1, [&](scanr::S& s) { scanr::txt(s, 50, 20, "AAAA", 2, INK); });
        bool clean = true, inked = false;
        for (int y = 0; y < FH; ++y) for (int x = 0; x < FW; ++x) {
            const bool in = x < 60 && y < 32;
            if (!in && fr[y * FW + x] != BG) clean = false;
            if (in && fr[y * FW + x] == INK) inked = true;
        }
        check("pack text clipped to the clip rect", clean && inked);
    }
    check("scope ended: back to 5x7", scanr::lineH(1) == 7);

    {
        const fontpack::Font* roles[scanr::ROLE_N] = {nullptr, &f, nullptr};
        scanr::setRoleFonts(roles);
        check("body role is 5x7", scanr::lineH(1) == 7);
        {
            scanr::RoleScope t(scanr::ROLE_TITLE);
            check("title role uses the font", scanr::lineH(1) == 8);
            { scanr::RoleScope b(scanr::ROLE_BODY); check("nested body role", scanr::lineH(1) == 7); }
            check("restored to title", scanr::lineH(1) == 8);
            { scanr::FontScope none(nullptr, {0, 0, FW, FH}); check("FontScope(nullptr) keeps the role font", scanr::lineH(1) == 8); }
        }
        check("after the title scope: 5x7", scanr::lineH(1) == 7);
        const fontpack::Font* clear[scanr::ROLE_N] = {nullptr, nullptr, nullptr};
        scanr::setRoleFonts(clear);
    }

    printf(fails ? "test_text_font: %d FAILED\n" : "test_text_font: all passed\n", fails);
    return fails ? 1 : 0;
}
