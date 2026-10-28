// ATTUNE -- the knobs.
//
// Four of them, and they are not arbitrary: each is a trade that was measured
// on this hardware rather than guessed at. Dwell sets how much of each channel
// you actually hear; BLE duty is the direct cost to WiFi capture, because
// there is one radio; bands narrows the hop when you only care about one.
#include "screens.h"
#include "prevstate.h"
#include "nav.h"
#include "palette.h"
#include "font5x7.h"
#include "layout.h"
#include "signatures.h"

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

void cat(char* out, const char* s) {
    int i = 0;
    while (out[i]) i++;
    for (int k = 0; s[k]; k++) out[i++] = s[k];
    out[i] = 0;
}

// Channels actually visited, which is what BANDS changes.
int activeChannels(const Settings& st) {
    return st.bands == 1 ? CH24 : st.bands == 2 ? CH5 : (CH24 + CH5);
}

void rowValue(const Settings& st, int row, char* out) {
    char tmp[12];
    out[0] = 0;
    switch (row) {
        case 0: cat(out, confName(st.alertMin)); break;
        case 4: u2s(out, st.bright); cat(out, "%"); break;
        case 5: {
            const uint16_t v = IDLE_S[st.idleIdx % IDLE_N];
            if (!v)            cat(out, "NEVER");
            else if (v < 60) { u2s(out, v);      cat(out, "S"); }
            else             { u2s(out, v / 60); cat(out, "M"); }
            if (v) cat(out, st.idleMode == IDLE_OFF ? " OFF" : st.idleMode == IDLE_ANIM ? " ANIM" : " DIM");
            break;
        }
        case 1: {
            // Dwell is the mechanism; the full cycle is what you actually
            // feel, and at 600ms across 21 channels that is 13 seconds
            // before a given channel is looked at again. Showing only the
            // dwell hides the consequence.
            u2s(out, st.dwellMs);
            cat(out, "MS (");
            const int cyc = ((int)st.dwellMs * activeChannels(st) + 500) / 1000;
            u2s(tmp, (uint32_t)(cyc < 1 ? 1 : cyc));
            cat(out, tmp);
            cat(out, "S)");
            break;
        }
        case 2: u2s(out, st.bleDuty); cat(out, "%");  break;
        case 3: cat(out, st.bands == 1 ? "2.4" : st.bands == 2 ? "5 GHZ" : "BOTH"); break;
        default: break;
    }
}

const char* rowLabel(int row) {
    switch (row) {
        case 0: return "ALERT";
        case 1: return "DWELL";
        case 2: return "BLE";
        case 3: return "BANDS";
        case 4: return "BRIGHT";
        case 5: return "IDLE";
        default: return "RESET";
    }
}

void button(Canvas& c, int x, int y, const char* glyph, bool live) {
    const uint16_t edge = live ? pal::EDGE_LIT : pal::EDGE;
    c.fill(x, y, SET_BTN_W, SET_BTN_H, Canvas::mix(pal::PANEL, pal::VOID, 1, 3));
    c.rect(x, y, SET_BTN_W, SET_BTN_H, edge);
    c.textCenter(x + SET_BTN_W / 2, y + 1, glyph, live ? pal::WITCH_HI : pal::DIM);
}

} // namespace

void drawAttune(Canvas& c, const State& s, Dirty* d) {
    c.clear(pal::VOID);
    drawHeader(c, s, screenName(SCR_ATTUNE, s.set.labels));

    c.fill(1, BODY_Y, 158, BODY_H, pal::PANEL);
    c.rect(1, BODY_Y, 158, BODY_H, pal::EDGE);

    const int page = s.setPage < SET_PAGES ? s.setPage : 0;
    for (int r = 0; r < SET_ROWS; r++) {
        const int y = SET_Y + r * SET_H;
        if (r) c.hline(5, y - 2, 148, Canvas::mix(pal::PANEL, pal::EDGE, 1, 2));

        if (r == SET_ROWS - 1) {
            c.text(6, y + 2, rowLabel(r), pal::DIM);
            // The pager, the same on every page so it never moves under the
            // finger: tap to go round the pages.
            char pg[10] = "PAGE 1/2";
            pg[5] = (char)('1' + page);
            pg[7] = (char)('0' + SET_PAGES);
            c.fill(SET_PAGE_X, y + 1, SET_PAGE_W, SET_BTN_H, Canvas::mix(pal::PANEL, pal::VOID, 1, 3));
            c.rect(SET_PAGE_X, y + 1, SET_PAGE_W, SET_BTN_H, pal::EDGE_LIT);
            c.textCenter(SET_PAGE_X + SET_PAGE_W / 2, y + 3, pg, pal::WITCH_HI);
            // The destructive one gets a single wide button and the only red
            // on the screen, so it cannot be mistaken for a knob.
            const int bx = SET_MINUS_X;
            const int bw = SET_PLUS_X + SET_BTN_W - SET_MINUS_X;
            c.fill(bx, y + 1, bw, SET_BTN_H, Canvas::mix(pal::PANEL, pal::RED, 1, 6));
            c.rect(bx, y + 1, bw, SET_BTN_H, pal::RED);
            c.textCenter(bx + bw / 2, y + 3, "CLEAR", pal::RED);
            continue;
        }

        if (page == 1) {
            // Page 2: AUTO LOG, FILES, then rows left free for what comes next.
            if (r == SET_FILES_ROW) {
                c.text(6, y + 2, screenName(SCR_FILES, s.set.labels), pal::DIM);
                const int bx = SET_MINUS_X;
                const int bw = SET_PLUS_X + SET_BTN_W - SET_MINUS_X;
                c.fill(bx, y + 1, bw, SET_BTN_H, Canvas::mix(pal::PANEL, pal::VOID, 1, 3));
                c.rect(bx, y + 1, bw, SET_BTN_H, pal::EDGE_LIT);
                c.textCenter(bx + bw / 2, y + 3, "OPEN", pal::WITCH_HI);
                continue;
            }
            if (r != SET_AUTOLOG_ROW) continue;
            c.text(6, y + 2, "AUTO LOG", pal::DIM);
            c.textRight(SET_MINUS_X - 4, y + 2, s.set.autoLog ? "ON" : "OFF", pal::BRIGHT);
            button(c, SET_MINUS_X, y + 1, "-", true);
            button(c, SET_PLUS_X,  y + 1, "+", true);
            continue;
        }

        c.text(6, y + 2, rowLabel(r), pal::DIM);

        char val[16];
        rowValue(s.set, r, val);
        c.textRight(SET_MINUS_X - 4, y + 2, val, pal::BRIGHT);

        button(c, SET_MINUS_X, y + 1, "-", true);
        button(c, SET_PLUS_X,  y + 1, "+", true);
    }

    drawFooter(c, s);
    drawReticle(c, s);
    c.vignette();
    c.scanlines();

    if (d) {
        State& prev = prevState(SCR_ATTUNE);   // PSRAM on the board
        static bool  first = true;
        d->clear();
        if (first) {
            d->add(0, 0, W, H);
            first = false;
        } else {
            // The DWELL row shows a cycle time derived from BANDS, so the
            // body moves when either does.
            if (s.set.bright != prev.set.bright || s.set.dwellMs != prev.set.dwellMs ||
                s.set.bleDuty != prev.set.bleDuty || s.set.bands != prev.set.bands ||
                s.set.alertMin != prev.set.alertMin || s.set.idleIdx != prev.set.idleIdx ||
                s.set.idleMode != prev.set.idleMode || s.set.autoLog != prev.set.autoLog ||
                s.setPage != prev.setPage)
                d->add(0, BODY_Y, W, BODY_H);
            dirtyChrome(*d, s, prev);
        }
        prev = s;
    }
}

} // namespace ui
