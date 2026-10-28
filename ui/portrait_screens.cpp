// Portrait GRIMOIRE, ATTUNE and FILES. Same controls as landscape, laid out
// for 120 px across (ui/portrait_layout.h); the values and labels come from
// the landscape screens' own helpers so a knob reads the same either way.
#include "portrait.h"
#include "portrait_layout.h"
#include "screens.h"
#include "palette.h"
#include "font5x7.h"
#include "runes.h"
#include "files.h"
#include "nav.h"
#include <string.h>

namespace ui {
namespace {

void cut(char* out, const char* in, int maxChars) {
    int i = 0;
    for (; in[i] && i < maxChars; i++) out[i] = in[i];
    out[i] = 0;
}

void body(Canvas& c) {
    c.fill(1, pl::BODY_Y, pl::PW - 2, pl::BODY_H - 1, pal::PANEL);
    c.rect(1, pl::BODY_Y, pl::PW - 2, pl::BODY_H - 1, pal::EDGE);
}

void button(Canvas& c, int x, int y, int w, int h, const char* label, uint16_t edge,
            uint16_t ink, uint16_t fill) {
    c.fill(x, y, w, h, fill);
    c.rect(x, y, w, h, edge);
    c.textCenter(x + w / 2 + 1, y + (h - Font::H) / 2, label, ink);
}

uint16_t darkFill() { return Canvas::mix(pal::PANEL, pal::VOID, 1, 3); }

void finish(Canvas& c, const State& s) {
    drawFooterP(c, s);
    drawReticle(c, s);
    c.vignette();
    c.scanlines();
}

} // namespace

// ---- GRIMOIRE ---------------------------------------------------------------

void drawGrimoireP(Canvas& c, const State& s) {
    c.clear(pal::VOID);
    drawHeaderP(c, s, screenName(SCR_GRIMOIRE, s.set.labels));
    for (int i = 0; i < GRIMOIRE_N; i++) {
        int x, y, w, h;
        pl::grimCell(i, x, y, w, h);
        const bool here = GRIMOIRE[i].screen == SCR_SCAN;
        c.fill(x, y, w, h, Canvas::mix(pal::VOID, pal::PANEL, 2, 3));
        c.rect(x, y, w, h, here ? pal::EDGE_LIT : pal::EDGE);
        const uint16_t m = Canvas::mix(pal::EDGE, pal::BONE, 1, 2);
        c.pixel(x + 2, y + 2, m);         c.pixel(x + w - 3, y + 2, m);
        c.pixel(x + 2, y + h - 3, m);     c.pixel(x + w - 3, y + h - 3, m);
        rune5c(c, GRIMOIRE[i].rune, x + w / 2, y + 6, pal::WITCH);
        c.textCenter(x + w / 2 + 1, y + 11, screenName(GRIMOIRE[i].screen, s.set.labels), pal::TEXT);
    }
    // The LABELS toggle, across the width: a live edge, it is a control.
    int x, y, w, h;
    pl::grimCell(GRIMOIRE_N, x, y, w, h);
    c.fill(x, y, w, h, Canvas::mix(pal::VOID, pal::PANEL, 2, 3));
    c.rect(x, y, w, h, pal::WITCH);
    c.text(x + 6, y + (h - Font::H) / 2, "LABELS", pal::WITCH_HI);
    c.textRight(x + w - 6, y + (h - Font::H) / 2, s.set.labels ? "PLAIN" : "BRAND",
                s.set.labels ? pal::GREEN : pal::DIM);
    finish(c, s);
}

// ---- ATTUNE -----------------------------------------------------------------

namespace {

void knob(Canvas& c, int y, const char* label, const char* value) {
    // Label over value, a pixel between them; no row rules in portrait, the
    // rows are too tight to carry them as well.
    c.text(pl::SET_LABEL_X, y + 1, label, pal::DIM);
    char v[pl::SET_VALUE_MAX + 1];
    cut(v, value, pl::SET_VALUE_MAX);
    c.text(pl::SET_LABEL_X, y + 9, v, pal::BRIGHT);
    button(c, pl::SET_MINUS_X, y + 1, pl::SET_BTN_W, pl::SET_BTN_H, "-", pal::EDGE_LIT, pal::WITCH_HI, darkFill());
    button(c, pl::SET_PLUS_X,  y + 1, pl::SET_BTN_W, pl::SET_BTN_H, "+", pal::EDGE_LIT, pal::WITCH_HI, darkFill());
}

} // namespace

void drawAttuneP(Canvas& c, const State& s) {
    c.clear(pal::VOID);
    drawHeaderP(c, s, screenName(SCR_ATTUNE, s.set.labels));
    body(c);
    const int page = s.setPage < SET_PAGES ? s.setPage : 0;
    for (int r = 0; r < SET_ROWS; r++) {
        const int y = pl::setRowY(r);
        if (r == SET_ROWS - 1) {
            // The pager and CLEAR, the same on every page.
            char pg[10] = "PAGE 1/2";
            pg[5] = (char)('1' + page);
            pg[7] = (char)('0' + SET_PAGES);
            button(c, pl::SET_PAGE_X, y + 1, pl::SET_PAGE_W, pl::SET_BTN_H, pg, pal::EDGE_LIT, pal::WITCH_HI, darkFill());
            button(c, pl::SET_WIDE_X, y + 1, pl::SET_WIDE_W, pl::SET_BTN_H, "CLEAR", pal::RED, pal::RED,
                   Canvas::mix(pal::PANEL, pal::RED, 1, 6));
            continue;
        }
        if (page == 1) {
            if (r == SET_AUTOLOG_ROW) { knob(c, y, "AUTO LOG", s.set.autoLog ? "ON" : "OFF"); continue; }
            if (r == SET_FILES_ROW) {
                c.text(pl::SET_LABEL_X, y + 5, screenName(SCR_FILES, s.set.labels), pal::DIM);
                button(c, pl::SET_WIDE_X, y + 1, pl::SET_WIDE_W, pl::SET_BTN_H, "OPEN", pal::EDGE_LIT, pal::WITCH_HI, darkFill());
                continue;
            }
            if (r == SET_ORIENT_ROW) {
                if (!portraitAllowed()) {          // never shown in portrait, but stay honest
                    c.text(pl::SET_LABEL_X, y + 1, "ORIENT", pal::DIM);
                    c.text(pl::SET_LABEL_X, y + 9, orientValue(s.set), pal::DIM);
                } else {
                    knob(c, y, "ORIENT", orientValue(s.set));
                }
                continue;
            }
            continue;          // free rows
        }
        char val[16];
        attuneRowValue(s.set, r, val);
        knob(c, y, attuneRowLabel(r), val);
    }
    finish(c, s);
}

// ---- FILES ------------------------------------------------------------------

namespace {

void filesRows(Canvas& c, const State& s) {
    if (!s.fileCard || s.nFileRows == 0) {
        const char* msg = s.fileCard ? "NO LOGS" : "NO CARD";
        c.textCenter(pl::PW / 2, pl::FILE_ROW_Y + 2 * pl::FILE_ROW_H + 4, msg, s.fileCard ? pal::DIM : pal::RED);
        return;
    }
    for (int i = 0; i < s.nFileRows; i++) {
        const int idx = s.fileScroll + i;
        const int y = pl::FILE_ROW_Y + i * pl::FILE_ROW_H;
        const bool sel = idx == s.fileSel, act = idx == s.fileActive;
        if (sel) {
            c.fill(pl::FILE_ROW_X, y, pl::FILE_ROW_W, pl::FILE_ROW_H - 1, Canvas::mix(pal::PANEL, pal::WITCH, 1, 3));
            c.fill(pl::FILE_ROW_X, y, 2, pl::FILE_ROW_H - 1, pal::WITCH_HI);
        } else if (i) {
            c.hline(pl::FILE_ROW_X + 2, y - 1, pl::FILE_ROW_W - 4, Canvas::mix(pal::PANEL, pal::EDGE, 1, 2));
        }
        char lab[FILE_LABEL_MAX + 1];
        fileLabel(s.fileRows[i].name, lab, sizeof lab);
        // Name on rows y..y+6, a blank row, then REC and the size on
        // y+8..y+14 (y+15 is the rule under the row). On the selected row the
        // size is BRIGHT: DIM all but vanishes on the selection fill.
        c.text(pl::FILE_ROW_X + 4, y, lab, sel ? pal::BRIGHT : pal::TEXT);
        if (act) c.text(pl::FILE_ROW_X + 4, y + 8, "REC", pal::RED);
        char sz[8];
        fmtBytes(s.fileRows[i].size, sz);
        c.textRight(pl::FILE_ROW_X + pl::FILE_ROW_W - 2, y + 8, sz, sel ? pal::BRIGHT : pal::DIM);
    }
    if (s.fileTotal > FILE_VIS) {
        const int top = pl::FILE_ROW_Y, h = FILE_VIS * pl::FILE_ROW_H - 1;
        const int sx = pl::FILE_ROW_X + pl::FILE_ROW_W + 2;
        c.vline(sx, top, h, pal::EDGE);
        const int th = h * FILE_VIS / s.fileTotal < 4 ? 4 : h * FILE_VIS / s.fileTotal;
        const int ty = top + (h - th) * s.fileScroll / (s.fileTotal - FILE_VIS);
        c.fill(sx - 1, ty, 3, th, pal::WITCH_HI);
    }
}

void filesBar(Canvas& c, const State& s) {
    const int ty = pl::FILE_BAR_Y + (pl::FILE_BAR_H - Font::H) / 2;
    if (s.fileResult == FR_DELETED)      c.text(5, ty, "DELETED", pal::GREEN);
    else if (s.fileResult == FR_FAILED)  c.text(5, ty, "FAILED", pal::RED);
    else if (s.fileSel >= 0 && s.fileSel == s.fileActive) c.text(5, ty, "LOGGING", pal::AMBER);
    else if (s.fileSel >= 0) {
        char sz[8];
        fmtBytes(s.fileSelSize, sz);
        c.text(5, ty, sz, pal::BRIGHT);
    } else if (s.fileCard && s.fileTotal) c.text(5, ty, "TAP A LOG", pal::DIM);

    const bool any = s.fileSel >= 0;
    const bool live = any && s.fileSel != s.fileActive;
    if (live)
        button(c, pl::FILE_DEL_X, pl::FILE_BAR_Y, pl::FILE_DEL_W, pl::FILE_BAR_H, "DELETE", pal::RED, pal::RED,
               Canvas::mix(pal::PANEL, pal::RED, 1, 6));
    else
        button(c, pl::FILE_DEL_X, pl::FILE_BAR_Y, pl::FILE_DEL_W, pl::FILE_BAR_H, any ? "IN USE" : "DELETE",
               pal::EDGE, pal::DIM, darkFill());
}

void filesConfirm(Canvas& c, const State& s) {
    c.fill(pl::FILE_CARD_X, pl::FILE_CARD_Y, pl::FILE_CARD_W, pl::FILE_CARD_H, Canvas::mix(pal::VOID, pal::PANEL, 2, 3));
    c.rect(pl::FILE_CARD_X, pl::FILE_CARD_Y, pl::FILE_CARD_W, pl::FILE_CARD_H, pal::RED);
    const int cx = pl::FILE_CARD_X + pl::FILE_CARD_W / 2 + 1;
    c.textCenter(cx, pl::FILE_CARD_Y + 4, "DELETE THIS LOG?", pal::RED);
    // To the second, on two lines: "2026-01-27" then "15:30:12".
    char lab[FILE_NAME_MAX + 4];
    fileLabelLong(s.fileSelName, lab, sizeof lab);
    char* sp = strchr(lab, ' ');
    if (sp) {
        *sp = 0;
        c.textCenter(cx, pl::FILE_CARD_Y + 16, lab, pal::BRIGHT);
        c.textCenter(cx, pl::FILE_CARD_Y + 25, sp + 1, pal::BRIGHT);
    } else {
        char one[17];
        cut(one, lab, 16);
        c.textCenter(cx, pl::FILE_CARD_Y + 20, one, pal::BRIGHT);
    }
    char sz[8];
    fmtBytes(s.fileSelSize, sz);
    c.textCenter(cx, pl::FILE_CARD_Y + 38, sz, pal::DIM);
    c.textCenter(cx, pl::FILE_CARD_Y + 47, "CANNOT UNDO", pal::DIM);
    button(c, pl::FILE_YES_X, pl::FILE_YES_Y, pl::FILE_YES_W, pl::FILE_YES_H, "YES", pal::RED, pal::RED,
           Canvas::mix(pal::PANEL, pal::RED, 1, 6));
    button(c, pl::FILE_NO_X, pl::FILE_NO_Y, pl::FILE_NO_W, pl::FILE_NO_H, "NO", pal::WITCH_HI, pal::BRIGHT,
           Canvas::mix(pal::PANEL, pal::WITCH, 1, 2));
}

} // namespace

void drawFilesP(Canvas& c, const State& s) {
    c.clear(pal::VOID);
    const char* title = screenName(SCR_FILES, s.set.labels);
    drawHeaderP(c, s, title);
    // The card's capacity ("28G") between the title and the badge, if it fits.
    if (s.fileCard) {
        char f[20];
        filesCardText(s, f);
        char* sp = strchr(f, ' ');
        if (sp) *sp = 0;
        const int right = pl::BADGE_X - 3;
        if (f[0] && pl::TITLE_X + Font::width(title, 1) + 4 <= right - Font::width(f, 1))
            c.textRight(right, 4, f, pal::DIM);
    }
    body(c);
    if (s.fileCard) {
        char cnt[24];
        filesCountText(s, cnt);
        c.text(4, pl::FILE_INFO_Y, cnt, s.filePartial ? pal::AMBER : pal::DIM);
        char u[20];
        filesLogsText(s, u);
        if (4 + Font::width(cnt, 1) + 6 <= pl::PW - 4 - Font::width(u, 1))
            c.textRight(pl::PW - 4, pl::FILE_INFO_Y, u, pal::DIM);
    }
    c.hline(3, pl::FILE_ROW_Y - 2, pl::PW - 6, pal::EDGE);
    filesRows(c, s);
    filesBar(c, s);
    if (s.fileConfirm) filesConfirm(c, s);
    finish(c, s);
}

} // namespace ui
