// FILES -- the session logs on the card, newest first, and the one control
// that deletes. Everything a tap does is decided in files.cpp; this only
// draws what State says.
#include "screens.h"
#include "prevstate.h"
#include "files.h"
#include "nav.h"
#include "palette.h"
#include "font5x7.h"
#include "layout.h"
#include <string.h>

namespace ui {
namespace {

void cat(char* out, const char* s) {
    int i = (int)strlen(out);
    while (*s) out[i++] = *s++;
    out[i] = 0;
}

void button(Canvas& c, int x, int y, int w, int h, const char* label, uint16_t edge,
            uint16_t ink, uint16_t fill) {
    c.fill(x, y, w, h, fill);
    c.rect(x, y, w, h, edge);
    c.textCenter(x + w / 2, y + (h - Font::H) / 2, label, ink);
}

void drawRows(Canvas& c, const State& s) {
    if (!s.fileCard || s.nFileRows == 0) {
        const char* msg = s.fileCard ? "NO LOGS" : "NO CARD";
        c.textCenter(W / 2, FILE_ROW_Y + 2 * FILE_ROW_H + 2, msg, s.fileCard ? pal::DIM : pal::RED, 1);
        return;
    }
    for (int i = 0; i < s.nFileRows; i++) {
        const int idx = s.fileScroll + i;
        const int y = FILE_ROW_Y + i * FILE_ROW_H;
        const bool sel = idx == s.fileSel, act = idx == s.fileActive;
        if (sel) {
            c.fill(FILE_ROW_X, y, FILE_ROW_W, FILE_ROW_H - 1, Canvas::mix(pal::PANEL, pal::WITCH, 1, 3));
            c.rect(FILE_ROW_X, y, FILE_ROW_W, FILE_ROW_H - 1, pal::WITCH_HI);
        } else if (i) {
            c.hline(FILE_ROW_X + 2, y - 1, FILE_ROW_W - 4, Canvas::mix(pal::PANEL, pal::EDGE, 1, 2));
        }
        char lab[FILE_LABEL_MAX + 1];       // cut so it never reaches REC
        fileLabel(s.fileRows[i].name, lab, sizeof lab);
        c.text(FILE_ROW_X + 2, y + 2, lab, sel ? pal::BRIGHT : pal::TEXT);
        if (act) c.text(FILE_REC_X, y + 2, "REC", pal::RED);
        char sz[8];
        fmtBytes(s.fileRows[i].size, sz);
        c.textRight(FILE_ROW_X + FILE_ROW_W - 2, y + 2, sz, pal::DIM);
    }
    // Scrollbar, when there is more than one screen of rows.
    if (s.fileTotal > FILE_VIS) {
        const int top = FILE_ROW_Y, h = FILE_VIS * FILE_ROW_H - 1;
        c.vline(FILE_ROW_X + FILE_ROW_W + 2, top, h, pal::EDGE);
        const int th = h * FILE_VIS / s.fileTotal < 4 ? 4 : h * FILE_VIS / s.fileTotal;
        const int ty = top + (h - th) * s.fileScroll / (s.fileTotal - FILE_VIS);
        c.fill(FILE_ROW_X + FILE_ROW_W + 1, ty, 3, th, pal::WITCH_HI);
    }
}

void drawBar(Canvas& c, const State& s) {
    const int y = FILE_BAR_Y;
    const uint16_t dark = Canvas::mix(pal::PANEL, pal::VOID, 1, 3);
    if (s.fileResult == FR_DELETED)      c.text(6, y + 3, "DELETED", pal::GREEN);
    else if (s.fileResult == FR_FAILED)  c.text(6, y + 3, "FAILED", pal::RED);
    else if (s.fileSel >= 0 && s.fileSel == s.fileActive) c.text(6, y + 3, "LOGGING", pal::AMBER);
    else if (s.fileSel >= 0) {
        char sz[8];
        fmtBytes(s.fileSelSize, sz);
        c.text(6, y + 3, sz, pal::BRIGHT);
    } else if (s.fileCard && s.fileTotal) c.text(6, y + 3, "TAP A LOG", pal::DIM);

    const bool any = s.fileSel >= 0;
    const bool live = any && s.fileSel != s.fileActive;
    if (live)
        button(c, FILE_DEL_X, y, FILE_DEL_W, FILE_BAR_H, "DELETE", pal::RED, pal::RED,
               Canvas::mix(pal::PANEL, pal::RED, 1, 6));
    else
        button(c, FILE_DEL_X, y, FILE_DEL_W, FILE_BAR_H, any ? "IN USE" : "DELETE", pal::EDGE,
               pal::DIM, dark);
}

void drawConfirm(Canvas& c, const State& s) {
    c.fill(FILE_CARD_X, FILE_CARD_Y, FILE_CARD_W, FILE_CARD_H, Canvas::mix(pal::VOID, pal::PANEL, 2, 3));
    c.rect(FILE_CARD_X, FILE_CARD_Y, FILE_CARD_W, FILE_CARD_H, pal::RED);
    const int cx = FILE_CARD_X + FILE_CARD_W / 2;
    c.textCenter(cx, FILE_CARD_Y + 4, "DELETE THIS LOG?", pal::RED);
    char lab[FILE_NAME_MAX + 4];
    fileLabelLong(s.fileSelName, lab, sizeof lab);     // to the second
    c.textCenter(cx, FILE_CARD_Y + 15, lab, pal::BRIGHT);
    char sz[8], line[24] = "";
    fmtBytes(s.fileSelSize, sz);
    cat(line, sz);
    cat(line, " - CANNOT UNDO");
    c.textCenter(cx, FILE_CARD_Y + 26, line, pal::DIM);
    // YES small and far left; NO large and default, where DELETE was.
    button(c, FILE_YES_X, FILE_YES_Y, FILE_YES_W, FILE_YES_H, "YES", pal::RED, pal::RED,
           Canvas::mix(pal::PANEL, pal::RED, 1, 6));
    button(c, FILE_NO_X, FILE_NO_Y, FILE_NO_W, FILE_NO_H, "NO", pal::WITCH_HI, pal::BRIGHT,
           Canvas::mix(pal::PANEL, pal::WITCH, 1, 2));
}

} // namespace

void drawFiles(Canvas& c, const State& s, Dirty* d) {
    c.clear(pal::VOID);
    drawHeader(c, s, screenName(SCR_FILES, s.set.labels));
    // The card's capacity, between the title and the band badge.
    if (s.fileCard) {
        char f[20];
        filesCardText(s, f);
        c.textRight(137, 2, f, pal::DIM);
    }

    c.fill(1, BODY_Y, 158, BODY_H, pal::PANEL);
    c.rect(1, BODY_Y, 158, BODY_H, pal::EDGE);

    if (s.fileCard) {
        // A bounded walk may have missed the newest logs: say so in words.
        char cnt[24];
        filesCountText(s, cnt);
        c.text(5, FILE_INFO_Y, cnt, s.filePartial ? pal::AMBER : pal::DIM);
        char u[20];
        filesLogsText(s, u);
        c.textRight(154, FILE_INFO_Y, u, pal::DIM);
    }
    c.hline(3, FILE_ROW_Y - 2, 154, pal::EDGE);

    drawRows(c, s);
    drawBar(c, s);
    if (s.fileConfirm) drawConfirm(c, s);

    drawFooter(c, s);
    drawReticle(c, s);
    c.vignette();
    c.scanlines();

    if (d) {
        State& prev = prevState(SCR_FILES);   // PSRAM on the board
        static bool first = true;
        d->clear();
        if (first) { d->add(0, 0, W, H); first = false; }
        else {
            bool body = s.nFileRows != prev.nFileRows || s.fileTotal != prev.fileTotal ||
                        s.fileFound != prev.fileFound || s.fileScroll != prev.fileScroll ||
                        s.fileSel != prev.fileSel || s.fileActive != prev.fileActive ||
                        s.fileConfirm != prev.fileConfirm || s.fileResult != prev.fileResult ||
                        s.fileCard != prev.fileCard || s.filePartial != prev.filePartial ||
                        s.fileLogKB != prev.fileLogKB || s.fileSelSize != prev.fileSelSize ||
                        memcmp(s.fileSelName, prev.fileSelName, sizeof s.fileSelName) != 0;
            for (int i = 0; !body && i < s.nFileRows; i++)
                body = s.fileRows[i].size != prev.fileRows[i].size ||
                       memcmp(s.fileRows[i].name, prev.fileRows[i].name, FILE_NAME_MAX) != 0;
            if (body) d->add(0, BODY_Y, W, BODY_H);
            if (s.cardMB != prev.cardMB || s.fileCard != prev.fileCard)
                d->add(0, 0, W, HDR_H);
            dirtyChrome(*d, s, prev);
        }
        prev = s;
    }
}

} // namespace ui
