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
#include "themepack.h"
#include <stdio.h>

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
        case SW_GALAXY:   return "GALAXY";
        case SW_ORBIT:    return "ORBIT";
        case SW_STARFALL: return "STARFALL";
        case SW_PULSAR:   return "PULSAR";
        case SW_COMET:    return "COMET";
        case SW_AURORA:   return "AURORA";
        default:        return "RADAR";
    }
}

const char* alertName(uint8_t theme, uint8_t style) {
    if (theme >= pal::TH_COUNT) {
        // A pack plays the one rite its theme.json names; the cosmic style
        // setting does not apply to it.
        switch (pal::packRite(theme)) {
            case themepack::RITE_SUPERNOVA: return "SUPERNOVA";
            case themepack::RITE_VORTEX:    return "VORTEX";
            case themepack::RITE_COMET:     return "COMET";
            case themepack::RITE_LOCKON:    return "LOCK-ON";
            default:                        return "WARD";
        }
    }
    if (theme == pal::TH_BAPH0MAU5) return "SUMMONED";
    if (theme != pal::TH_COSMICMUFFIN) return "WARD";
    switch (style) {
        case AL_VORTEX: return "VORTEX";
        case AL_COMET:  return "COMET";
        case AL_LOCKON: return "LOCK-ON";
        default:        return "SUPERNOVA";
    }
}

void themeRowName(uint8_t theme, char* out, size_t cap) {
    if (!out || !cap) return;
    // "COSMIC MUFFIN" is two characters wider than this row allows; the scan
    // masthead carries the full name.
    const char* n = theme == pal::TH_COSMICMUFFIN ? "COSMIC" : pal::themeName(theme);
    size_t lim = cap - 1;
    if (lim > (size_t)ASP_NAME_MAX) lim = ASP_NAME_MAX;
    size_t i = 0;
    for (; i < lim && n[i]; i++) out[i] = n[i];
    out[i] = 0;
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

    char themeTxt[ASP_NAME_MAX + 1];
    themeRowName(s.set.theme, themeTxt, sizeof themeTxt);
    for (int r = 0; r < ASP_ROWS; r++) {
        const int y = ASP_ROW_Y + r * ASP_ROW_H;
        if (r) c.hline(5, y - 2, 148, Canvas::mix(pal::PANEL, pal::EDGE, 1, 2));

        // The ALERT row only has a choice on COSMIC MUFFIN; elsewhere (packs
        // included) it names the theme's one rite, with its buttons dimmed.
        const bool live = r < 2 || s.set.theme == pal::TH_COSMICMUFFIN;
        c.text(6, y + 2, r == 0 ? "THEME" : r == 1 ? "SWEEP" : "ALERT", pal::DIM);
        c.textRight(SET_MINUS_X - 4, y + 2,
                    r == 0 ? themeTxt
                  : r == 1 ? sweepName(s.set.sweep)
                           : alertName(s.set.theme, s.set.alertStyle),
                    live ? pal::BRIGHT : pal::DIM);
        button(c, SET_MINUS_X, y + 1, "-", live);
        button(c, SET_PLUS_X,  y + 1, "+", live);
    }

    // Theme packs skipped at boot: a count, and where the reasons are. Two
    // cells either side of the preview (see ASP_SKIP_* in layout.h); status,
    // so AMBER. Over 99 the count reads "99+" to stay inside its cell.
    if (s.packsSkipped > 0) {
        char line[12];
        if (s.packsSkipped > 99) snprintf(line, sizeof line, "99+SKIPPED");
        else snprintf(line, sizeof line, "%u SKIPPED", (unsigned)s.packsSkipped);
        c.text(ASP_SKIP_LX, ASP_SKIP_Y, line, pal::AMBER);
        c.textRight(ASP_SKIP_RX + ASP_SKIP_W, ASP_SKIP_Y, "DIAG LOG", pal::AMBER);
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
            // ...which already covers the skipped line; named anyway so it
            // still repaints if the body rect above ever narrows.
            if (prev.packsSkipped != s.packsSkipped) {
                d->add(ASP_SKIP_LX, ASP_SKIP_Y, ASP_SKIP_W, ASP_SKIP_H);
                d->add(ASP_SKIP_RX, ASP_SKIP_Y, ASP_SKIP_W, ASP_SKIP_H);
            }
            dirtyChrome(*d, s, prev);
        }
        prev = s;
    }
}

} // namespace ui
