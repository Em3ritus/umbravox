// Portrait chrome: header, footer, the placeholder card, and the dispatch.
#include "portrait.h"
#include "portrait_layout.h"
#include "screens.h"
#include "palette.h"
#include "font5x7.h"
#include "nav.h"
#include <string.h>

namespace ui {
namespace {

void u2s(char* out, uint32_t v) {
    char tmp[12];
    int n = 0;
    do { tmp[n++] = (char)('0' + v % 10); v /= 10; } while (v && n < 11);
    int i = 0;
    while (n) out[i++] = tmp[--n];
    out[i] = 0;
}

} // namespace

bool portraitCore(uint8_t screen) {
    return screen == SCR_SCAN || screen == SCR_GRIMOIRE || screen == SCR_ATTUNE ||
           screen == SCR_FILES;
}

void drawHeaderP(Canvas& c, const State& s, const char* title) {
    c.fill(0, 0, pl::PW, pl::HDR_H - 1, pal::PANEL);
    c.hline(0, pl::HDR_H - 1, pl::PW, pal::EDGE_LIT);

    // The menu chip (SCAN) or the back arrow (everywhere else), as landscape.
    c.fill(0, 0, pl::CHIP_W, pl::HDR_H - 1, Canvas::mix(pal::PANEL, pal::WITCH, 1, 2));
    c.rect(0, 0, pl::CHIP_W, pl::HDR_H - 1, pal::WITCH_HI);
    if (s.screen == SCR_SCAN) {
        c.hline(4, 4, 8, pal::BRIGHT);
        c.hline(4, 6, 8, pal::BRIGHT);
        c.hline(4, 8, 8, pal::BRIGHT);
    } else {
        static const uint8_t ARROW[7] = {
            0b0001000, 0b0011000, 0b0111000, 0b1111111,
            0b0111000, 0b0011000, 0b0001000,
        };
        for (int j = 0; j < 7; j++)
            for (int i = 0; i < 7; i++)
                if ((ARROW[j] >> (6 - i)) & 1) c.pixel(4 + i, 3 + j, pal::BRIGHT);
    }

    // Cut to what fits between the chip and the badge.
    char t[pl::TITLE_MAX + 1];
    int n = 0;
    for (; title && title[n] && n < pl::TITLE_MAX; n++) t[n] = title[n];
    t[n] = 0;
    c.text(pl::TITLE_X, 4, t, pal::WITCH_HI);

    const uint16_t bc = s.on5 ? pal::ICE : pal::MAGENTA;
    c.rect(pl::BADGE_X, 2, pl::BADGE_W, 10, pal::EDGE);
    c.textCenter(pl::BADGE_X + pl::BADGE_W / 2 + 1, 3, s.on5 ? "5G" : "24", bc);
}

void drawFooterP(Canvas& c, const State& s) {
    c.hline(0, pl::FOOT_Y, pl::PW, pal::EDGE_LIT);
    c.fill(0, pl::FOOT_Y + 1, pl::PW, pl::PH - pl::FOOT_Y - 1, pal::PANEL);
    const int ty = pl::FOOT_TY;

    const uint16_t fc = s.gpsFix ? pal::GREEN : pal::AMBER;
    drawStatRune(c, 3, ty + 1, fc, s.gpsFix);
    c.text(11, ty, s.gpsFix ? "FIXED" : "NO GPS", fc);

    // Mesh nodes, SCAN only, right-aligned: the glyph (or "N" in plain
    // labels) and up to two digits, DIM at none.
    if (s.screen == SCR_SCAN) {
        const uint16_t nc = s.nNodes ? pal::GREEN : pal::DIM;
        char num[4];
        u2s(num, s.nNodes > 99 ? 99 : s.nNodes);
        const int right = pl::PW - 4;
        if (s.set.labels == 1) {
            char lab[4] = {'N', num[0], num[1], 0};
            c.textRight(right, ty, lab, nc);
        } else {
            const int tw = Font::width(num, 1);
            const int gx = right - tw - 7;
            static const uint8_t MESH[5] = {0b00100, 0b01010, 0b01010, 0b10001, 0b11111};
            for (int j = 0; j < 5; j++)
                for (int i = 0; i < 5; i++)
                    if ((MESH[j] >> (4 - i)) & 1) c.pixel(gx + i, ty + 1 + j, nc);
            c.textRight(right, ty, num, nc);
        }
    }

    // START / STOP across the width: the record dot and what a tap will do.
    // Live on SCAN only (the tap is SCAN's); elsewhere it reports, DIM.
    const bool rec    = (s.logState == 1);
    const bool nocard = (s.logState == 2) || (s.logState == 0 && !s.sdOk);
    const bool inert  = s.screen != SCR_SCAN;
    const int bx = pl::LOG_X + 1, by = pl::LOG_Y + 1, bw = pl::LOG_W - 2, bh = pl::LOG_H - 2;
    const uint16_t red = inert ? pal::DIM : pal::RED;
    c.fill(bx, by, bw, bh, Canvas::mix(pal::PANEL, pal::VOID, 1, 3));
    c.rect(bx, by, bw, bh, inert ? pal::EDGE : (rec || nocard) ? pal::RED : pal::EDGE_LIT);
    const char* label = nocard ? "NO CARD" : rec ? "STOP" : "START";
    const uint16_t ink = nocard ? red : inert ? pal::DIM : rec ? pal::BRIGHT : pal::WITCH_HI;
    const int total = 8 + Font::width(label, 1);
    const int x0 = bx + (bw - total) / 2;
    const int dy = by + bh / 2;
    if (rec) c.disc(x0 + 2, dy, 2, red);
    else     c.circle(x0 + 2, dy, 2, nocard ? red : inert ? pal::DIM : pal::WITCH_HI);
    c.text(x0 + 8, dy - Font::H / 2, label, ink);
}

void drawPlaceholderP(Canvas& c, const State& s) {
    c.clear(pal::VOID);
    drawHeaderP(c, s, screenName(s.screen, s.set.labels));
    c.fill(1, pl::BODY_Y, pl::PW - 2, pl::BODY_H - 1, pal::PANEL);
    c.rect(1, pl::BODY_Y, pl::PW - 2, pl::BODY_H - 1, pal::EDGE);

    drawPanel(c, pl::PH_CARD_X, pl::PH_CARD_Y, pl::PH_CARD_W, pl::PH_CARD_H,
              Canvas::mix(pal::VOID, pal::PANEL, 1, 2), true);
    const int cx = pl::PH_CARD_X + pl::PH_CARD_W / 2;
    c.textCenter(cx, pl::PH_CARD_Y + 8, screenName(s.screen, s.set.labels), pal::BRIGHT, 2);
    c.textCenter(cx, pl::PH_CARD_Y + 30, "PORTRAIT LAYOUT", pal::WITCH_HI);
    c.textCenter(cx, pl::PH_CARD_Y + 40, "COMING", pal::WITCH_HI);
    // How to get the whole UI back, from the one place it always is.
    c.textCenter(cx, pl::PH_CARD_Y + 54, "TO LANDSCAPE:", pal::DIM);
    c.textCenter(cx, pl::PH_CARD_Y + 63, "ATTUNE PAGE 2", pal::DIM);

    // BACK: the large target, as well as the header chip.
    c.fill(pl::PH_BACK_X, pl::PH_BACK_Y, pl::PH_BACK_W, pl::PH_BACK_H, Canvas::mix(pal::PANEL, pal::WITCH, 1, 3));
    c.rect(pl::PH_BACK_X, pl::PH_BACK_Y, pl::PH_BACK_W, pl::PH_BACK_H, pal::WITCH_HI);
    static const uint8_t ARROW[7] = {
        0b0001000, 0b0011000, 0b0111000, 0b1111111,
        0b0111000, 0b0011000, 0b0001000,
    };
    const int tw = 9 + Font::width("BACK", 1);
    const int ax = pl::PH_BACK_X + (pl::PH_BACK_W - tw) / 2, ay = pl::PH_BACK_Y + (pl::PH_BACK_H - 7) / 2;
    for (int j = 0; j < 7; j++)
        for (int i = 0; i < 7; i++)
            if ((ARROW[j] >> (6 - i)) & 1) c.pixel(ax + i, ay + j, pal::BRIGHT);
    c.text(ax + 9, ay, "BACK", pal::BRIGHT);

    drawFooterP(c, s);
    drawReticle(c, s);
    c.vignette();
    c.scanlines();
}

void drawPortrait(Canvas& c, const State& s) {
    switch (s.screen) {
        case SCR_SCAN:     drawScanP(c, s);     break;
        case SCR_GRIMOIRE: drawGrimoireP(c, s); break;
        case SCR_ATTUNE:   drawAttuneP(c, s);   break;
        case SCR_FILES:    drawFilesP(c, s);    break;
        default:           drawPlaceholderP(c, s); break;
    }
}

} // namespace ui
