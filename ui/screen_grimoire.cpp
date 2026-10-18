// GRIMOIRE -- every working this device knows, as a grid.
//
// Navigation had been running out of header: each new screen needed another
// mark in a bar that was already carrying a title, two runes and a band
// badge. A grid scales, and it is also the honest shape for what this is
// becoming -- a set of tools rather than one instrument with accessories.
#include "screens.h"
#include "palette.h"
#include "font5x7.h"
#include "layout.h"
#include "runes.h"
#include "nav.h"

namespace ui {

// Tile order is the order of the enum below it, so the hit test and the
// drawing cannot disagree about which cell is which.
const GrimoireEntry GRIMOIRE[GRIMOIRE_N] = {
    {SCR_SCAN,   0,  "SCAN"},
    {SCR_CODEX,  2,  "CODEX"},
    {SCR_DOMAIN, 4,  "DOMAIN"},
    {SCR_ECHOES, 5,  "ECHOES"},
    {SCR_MESH,   6,  "MESH"},
    {SCR_OMENS,  12, "OMENS"},
    {SCR_ATTUNE, 15, "ATTUNE"},
    {SCR_ASPECT, 11, "ASPECT"},
};

void grimoireCell(int i, int& x, int& y, int& w, int& h) {
    w = GRIM_W; h = GRIM_H;
    x = GRIM_X + (i % GRIM_COLS) * (GRIM_W + GRIM_GAP);
    y = GRIM_Y + (i / GRIM_COLS) * (GRIM_H + GRIM_GAP);
}

void drawGrimoire(Canvas& c, const State& s, Dirty* d) {
    c.clear(pal::VOID);
    drawHeader(c, s, "GRIMOIRE");

    for (int i = 0; i < GRIMOIRE_N; i++) {
        int x, y, w, h;
        grimoireCell(i, x, y, w, h);
        const bool here = GRIMOIRE[i].screen == SCR_SCAN;   // where you came from

        c.fill(x, y, w, h, Canvas::mix(pal::VOID, pal::PANEL, 2, 3));
        c.rect(x, y, w, h, here ? pal::EDGE_LIT : pal::EDGE);
        // Corner marks, as on every other card here.
        const uint16_t m = Canvas::mix(pal::EDGE, pal::BONE, 1, 2);
        c.pixel(x + 2, y + 2, m);         c.pixel(x + w - 3, y + 2, m);
        c.pixel(x + 2, y + h - 3, m);     c.pixel(x + w - 3, y + h - 3, m);

        rune5c(c, GRIMOIRE[i].rune, x + w / 2, y + 9, pal::WITCH);
        c.textCenter(x + w / 2, y + h - 10, GRIMOIRE[i].label, pal::TEXT);
    }

    drawFooter(c, s);
    drawReticle(c, s);
    c.vignette();
    c.scanlines();

    if (d) {
        static State prev;
        static bool first = true;
        d->clear();
        if (first) { d->add(0, 0, W, H); first = false; }
        else dirtyChrome(*d, s, prev);
        prev = s;
    }
}

} // namespace ui
