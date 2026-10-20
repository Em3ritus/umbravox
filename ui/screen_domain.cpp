// DOMAIN -- where the things are, rather than what they are.
//
// The dial shows what is around you right now, by signal strength and a
// stable bearing that is not a bearing. This shows the opposite: real
// positions, accumulated, with your track behind you. It is the only screen
// that answers "where did I see that".
//
// North is up. There is a course in the GPS, but orienting to travel makes
// a stationary device spin, and a map that moves while you stand still is
// worse than one that is merely not aligned with the windscreen.
#include "screens.h"
#include "nav.h"
#include "palette.h"
#include "font5x7.h"
#include "layout.h"
#include "signatures.h"

namespace ui {
namespace {

uint16_t bandColor(uint8_t band) {
    switch (band) {
        case BAND_24: return pal::MAGENTA;
        case BAND_5:  return pal::ICE;
        default:      return pal::GREEN;
    }
}

void u2s(char* out, uint32_t v) {
    char tmp[12]; int n = 0;
    do { tmp[n++] = (char)('0' + v % 10); v /= 10; } while (v && n < 11);
    int i = 0; while (n) out[i++] = tmp[--n];
    out[i] = 0;
}
void cat(char* o, const char* s) { int i = 0; while (o[i]) i++; for (int k = 0; s[k]; k++) o[i++] = s[k]; o[i] = 0; }

} // namespace

void drawDomain(Canvas& c, const State& s, Dirty* d) {
    c.clear(pal::VOID);
    drawHeader(c, s, screenName(SCR_DOMAIN, s.set.labels));

    c.fill(1, BODY_Y, 158, BODY_H, Canvas::mix(pal::VOID, pal::PANEL, 1, 2));
    c.rect(1, BODY_Y, 158, BODY_H, pal::EDGE);

    // Ground: a lattice to give the plot somewhere to sit.
    for (int y = BODY_Y + 2; y < BODY_Y + BODY_H - 2; y++)
        for (int x = 3; x < 157; x++)
            if (x % 8 == 0 && y % 8 == 0) c.pixel(x, y, pal::GRID);

    // Range rings at a third, two thirds and full.
    for (int k = 1; k <= 3; k++)
        c.dottedCircle(MAP_CX, MAP_CY, MAP_R * k / 3, pal::EDGE, 2);
    for (int i = -MAP_R; i <= MAP_R; i += 4) {
        c.pixel(MAP_CX + i, MAP_CY, pal::EDGE);
        c.pixel(MAP_CX, MAP_CY + i, pal::EDGE);
    }
    c.text(MAP_CX - 2, BODY_Y + 3, "N", pal::DIM);

    // The track first, so sightings sit on top of where you drove.
    for (int i = 0; i < s.nTrack; i++) {
        const int x = MAP_CX + s.track[i].dx, y = MAP_CY + s.track[i].dy;
        // Witchlight, not edge grey: the track is *yours*, and at a glance
        // it has to separate from the field of sightings it runs through.
        // Older points fade, so direction of travel reads without an arrow.
        const int lum = 70 + (185 * i) / (s.nTrack > 1 ? s.nTrack - 1 : 1);
        const uint16_t col = Canvas::mix(pal::VOID, pal::WITCH, lum, 255);
        c.pixel(x, y, col);
        if (i == s.nTrack - 1) {          // the most recent point, emphasised
            c.pixel(x - 1, y, col);
            c.pixel(x + 1, y, col);
        }
    }

    for (int i = 0; i < s.nPlots; i++) {
        const Plot& p = s.plots[i];
        const int x = MAP_CX + p.dx, y = MAP_CY + p.dy;
        const uint16_t col = bandColor(p.band);
        c.fill(x, y, 2, 2, col);
        // Anything that matched a signature gets a ring, so the map answers
        // "where was that Flock camera" and not only "where were networks".
        if (p.threat != TH_NONE) c.circle(x, y, 3, pal::RED);
    }

    // You, last and brightest.
    c.hline(MAP_CX - 3, MAP_CY, 7, pal::WITCH_HI);
    c.vline(MAP_CX, MAP_CY - 3, 7, pal::WITCH_HI);
    c.pixel(MAP_CX, MAP_CY, pal::BRIGHT);

    char b[24], n[12];
    b[0] = 0; u2s(n, s.rangeM); cat(b, n); cat(b, "M");
    c.text(5, BODY_Y + BODY_H - 9, b, pal::DIM);

    b[0] = 0; u2s(n, s.nPlots); cat(b, n); cat(b, "/"); 
    { char m[12]; u2s(m, s.plotTotal); cat(b, m); }
    c.textRight(155, BODY_Y + BODY_H - 9, b, pal::TEXT);

    if (s.plotBeyond) {
        b[0] = 0; u2s(n, s.plotBeyond); cat(b, n); cat(b, " BEYOND");
        c.textRight(155, BODY_Y + 3, b, pal::EDGE_LIT);
    }
    if (s.plotTotal == 0)
        c.textCenter(MAP_CX, MAP_CY + 14, "NO FIXES YET", pal::DIM);

    drawFooter(c, s);
    drawReticle(c, s);
    c.vignette();
    c.scanlines();

    if (d) {
        static State prev;
        static bool first = true;
        d->clear();
        if (first) { d->add(0, 0, W, H); first = false; }
        else {
            if (s.nPlots != prev.nPlots || s.rangeM != prev.rangeM ||
                s.nTrack != prev.nTrack || s.plotTotal != prev.plotTotal)
                d->add(0, BODY_Y, W, BODY_H);
            dirtyChrome(*d, s, prev);
        }
        prev = s;
    }
}

} // namespace ui
