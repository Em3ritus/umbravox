// ASPECT -- what the instrument looks like.
//
// Appearance is not a capture trade-off, so it does not belong in ATTUNE
// beside dwell and BLE duty. ATTUNE is also full: layout.h documents seven
// rows as its ceiling, and all seven are used.
//
// The preview calls the real dial rather than drawing a picture of one. A
// picture would drift, and the whole reason ui/layout.h exists is that two
// copies of the same geometry always do.
#include "screens.h"
#include "palette.h"
#include "font5x7.h"
#include "layout.h"
#include "nav.h"

namespace ui {
namespace {

void button(Canvas& c, int x, int y, const char* glyph, bool live) {
    const uint16_t edge = live ? pal::EDGE_LIT : pal::EDGE;
    c.fill(x, y, SET_BTN_W, SET_BTN_H, Canvas::mix(pal::PANEL, pal::VOID, 1, 3));
    c.rect(x, y, SET_BTN_W, SET_BTN_H, edge);
    c.textCenter(x + SET_BTN_W / 2, y + 1, glyph, live ? pal::WITCH_HI : pal::DIM);
}

} // namespace

const char* sweepName(uint8_t id) {
    switch (id) {
        case SW_GAZE:   return "GAZE";
        case SW_PULSE:  return "PULSE";
        case SW_STAR:   return "STAR";
        case SW_CANDLE: return "CANDLE";
        default:        return "RADAR";
    }
}

void drawAspect(Canvas& c, const State& s, Dirty* d) {
    c.clear(pal::VOID);
    drawHeader(c, s, screenName(SCR_ASPECT, s.set.labels));

    c.fill(1, BODY_Y, 158, BODY_H, pal::PANEL);
    c.rect(1, BODY_Y, 158, BODY_H, pal::EDGE);

    // Scale the ACTIVE theme's own radii down to the preview's bound, so the
    // proportions hold whichever theme is selected -- a fixed set of radii
    // fits one theme and mangles the other.
    //
    // FACE is a fixed bitmap and does not scale with them, so on BAPH0MAU5 the
    // preview crops the seal to its middle: horns and eyes, which is the part
    // that identifies it. That is a preview behaving like a window onto the
    // dial, not a defect.
    const int pf = R_FIELD * ASP_R / R_BOUND;
    const int pr = R_RUNES * ASP_R / R_BOUND;
    const int ps = R_STAR  * ASP_R / R_BOUND;
    drawDialAt(c, s, W / 2, ASP_CY, pf, pr, ps, ASP_R);

    for (int r = 0; r < ASP_ROWS; r++) {
        const int y = ASP_ROW_Y + r * ASP_ROW_H;
        if (r) c.hline(5, y - 2, 148, Canvas::mix(pal::PANEL, pal::EDGE, 1, 2));

        c.text(6, y + 2, r == 0 ? "THEME" : "SWEEP", pal::DIM);
        c.textRight(SET_MINUS_X - 4, y + 2,
                    r == 0 ? pal::themeName(s.set.theme) : sweepName(s.set.sweep),
                    pal::BRIGHT);
        button(c, SET_MINUS_X, y + 1, "-", true);
        button(c, SET_PLUS_X,  y + 1, "+", true);
    }

    drawFooter(c, s);
    drawReticle(c, s);
    c.vignette();
    c.scanlines();

    if (d) {
        static State prev;
        static bool  first = true;
        d->clear();
        if (first) {
            d->add(0, 0, W, H);
            first = false;
        } else if (prev.set.theme != s.set.theme) {
            // A theme change repaints everything: the dirty regions of the
            // old palette and geometry say nothing about the new ones.
            d->add(0, 0, W, H);
        } else {
            d->add(0, BODY_Y, W, BODY_H);   // the preview animates every frame
            dirtyChrome(*d, s, prev);
        }
        prev = s;
    }
}

} // namespace ui
