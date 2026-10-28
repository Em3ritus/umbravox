// Portrait SCAN: tiles in a row, the theme's dial, the alert card over it,
// the spectrum, the two-row footer. The dial, the alert rites and the
// spectrum are the landscape ones (drawDialAt, drawAlertIn, drawSpectrumAt)
// at portrait positions -- one copy of each, so they cannot drift.
#include "portrait.h"
#include "portrait_layout.h"
#include "screens.h"
#include "palette.h"
#include "font5x7.h"
#include <string.h>

namespace ui {
namespace {

void u2s(char* out, uint32_t v, int pad = 0) {
    char tmp[12];
    int n = 0;
    do { tmp[n++] = (char)('0' + v % 10); v /= 10; } while (v && n < 11);
    int i = 0;
    while (pad-- > n) out[i++] = '0';
    while (n) out[i++] = tmp[--n];
    out[i] = 0;
}

void tile(Canvas& c, int i, const char* label, uint32_t value, uint16_t accent) {
    int x, y, w, h;
    pl::tileRect(i, x, y, w, h);
    drawPanel(c, x, y, w, h, pal::PANEL);
    c.fill(x + 1, y + 1, 2, h - 2, accent);
    c.text(x + 5, y + 3, label, pal::DIM);
    char buf[12];
    u2s(buf, value);
    c.text(x + 5, y + 11, buf, pal::BRIGHT);
}

} // namespace

// The masthead in 13 characters: the theme's header if it fits, else its
// name ("CVLT OF BAPH0MAU5" -> "BAPH0MAU5"), else the header cut.
const char* portraitTitle(uint8_t theme) {
    const char* h = pal::header(theme);
    if (strlen(h) <= (size_t)pl::TITLE_MAX) return h;
    const char* n = pal::themeName(theme);
    if (strlen(n) <= (size_t)pl::TITLE_MAX) return n;
    return h;     // drawHeaderP cuts it
}

void drawScanP(Canvas& c, const State& s) {
    c.clear(pal::VOID);
    drawHeaderP(c, s, portraitTitle(s.set.theme));
    tile(c, 0, "WIFI",  (uint32_t)s.nets24 + s.nets5, pal::MAGENTA);
    tile(c, 1, "5 GHZ", s.nets5,                      pal::ICE);
    tile(c, 2, "BLE",   s.ble,                        pal::GREEN);

    drawPanel(c, pl::DIAL_X, pl::DIAL_Y, pl::DIAL_W, pl::dialH(), Canvas::mix(pal::VOID, pal::PANEL, 1, 2), true);
    {
        char ch[8] = {'C', 'H', 0};
        u2s(ch + 2, s.chan, 2);
        c.textRight(pl::DIAL_X + pl::DIAL_W - 4, pl::DIAL_Y + 3, ch, pal::DIM);
        if (s.nNodes) c.text(pl::DIAL_X + 3, pl::DIAL_Y + 3, "MESH", pal::GREEN);
        c.text(pl::DIAL_X + 3, pl::DIAL_Y + pl::dialH() - 9, "-90", pal::DIM);
    }
    drawDialAt(c, s, pl::dialCX(), pl::dialCY(), R_FIELD, R_RUNES, R_STAR, R_BOUND);
    drawAlertIn(c, s, pl::DIAL_X, pl::DIAL_Y, pl::DIAL_W, pl::dialH());
    if (SPEC_H > 0) drawSpectrumAt(c, s, pl::DIAL_X, pl::specY(), pl::DIAL_W, SPEC_H);
    drawFooterP(c, s);
    drawReticle(c, s);

    c.vignette();
    c.scanlines();
}

} // namespace ui
