// OMENS -- what has fired, newest first.
//
// An alert is a moment; this is the record. The columns that matter are the
// confidence it was graded at and whether it carries a position -- a hit
// without one cannot be gone back to, which is worth knowing at a glance.
#include "screens.h"
#include "palette.h"
#include "font5x7.h"
#include "layout.h"
#include "signatures.h"

namespace ui {
namespace {

constexpr int HEAD_Y = 15, RULE_Y = 23;
constexpr int ROW_Y  = 26, ROW_HT = 12;

void u2s(char* out, uint32_t v) {
    char tmp[12]; int n = 0;
    do { tmp[n++] = (char)('0' + v % 10); v /= 10; } while (v && n < 11);
    int i = 0; while (n) out[i++] = tmp[--n];
    out[i] = 0;
}
void cat(char* o, const char* s) { int i = 0; while (o[i]) i++; for (int k = 0; s[k]; k++) o[i++] = s[k]; o[i] = 0; }

void ago(char* out, uint32_t s) {
    out[0] = 0; char n[12];
    if (s < 60)        { u2s(n, s);        cat(out, n); cat(out, "S"); }
    else if (s < 3600) { u2s(n, s / 60);   cat(out, n); cat(out, "M"); }
    else               { u2s(n, s / 3600); cat(out, n); cat(out, "H"); }
}

uint16_t confColor(uint8_t c) {
    return c == CONF_HIGH ? pal::RED : c == CONF_MED ? pal::AMBER : pal::WITCH;
}

} // namespace

void drawOmens(Canvas& c, const State& s, Dirty* d) {
    c.clear(pal::VOID);
    char title[16] = {'O','M','E','N','S',0};
    if (s.omenTotal > MAX_OMEN_ROWS) {
        int k = 5; title[k++] = ' ';
        char n[8]; u2s(n, s.omenTotal);
        for (int i = 0; n[i] && k < 15; i++) title[k++] = n[i];
        title[k] = 0;
    }
    drawHeader(c, s, title);

    c.fill(1, BODY_Y, 158, BODY_H, pal::PANEL);
    c.rect(1, BODY_Y, 158, BODY_H, pal::EDGE);

    c.text(10, HEAD_Y, "OMEN", pal::EDGE_LIT);
    c.text(58, HEAD_Y, "SOURCE", pal::EDGE_LIT);
    c.textRight(155, HEAD_Y, "AGE", pal::EDGE_LIT);
    c.hline(5, RULE_Y, 148, pal::EDGE);

    if (s.nOmens == 0) {
        c.textCenter(W / 2, ROW_Y + 20, "NOTHING HAS FIRED", pal::DIM);
        c.textCenter(W / 2, ROW_Y + 32, "WHICH IS THE GOOD OUTCOME", pal::EDGE_LIT);
    }

    char buf[24];
    for (int i = 0; i < s.nOmens; i++) {
        const Omen& o = s.omens[i];
        const int y = ROW_Y + i * ROW_HT;
        const uint16_t cc = confColor(o.conf);

        c.fill(3, y + 2, 2, ROW_HT - 4, cc);
        c.text(9, y + 2, threatName(o.threat), cc);

        // A hit with no fix cannot be returned to, so say so rather than
        // leaving the row looking complete.
        if (o.located) c.fill(52, y + 5, 2, 2, pal::GREEN);
        else           c.text(51, y + 2, "-", pal::EDGE_LIT);

        char nm[12];
        int k = 0;
        for (; k < 9 && o.name[k]; k++) nm[k] = o.name[k];
        nm[k] = 0;
        c.text(58, y + 2, nm, pal::TEXT);

        ago(buf, o.agoS);
        c.textRight(155, y + 2, buf, pal::DIM);
        if (i) c.hline(5, y, 148, Canvas::mix(pal::PANEL, pal::EDGE, 1, 2));
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
        else {
            bool moved = s.nOmens != prev.nOmens || s.omenTotal != prev.omenTotal ||
                         s.omenScroll != prev.omenScroll;
            for (int i = 0; !moved && i < s.nOmens; i++)
                moved = s.omens[i].agoS != prev.omens[i].agoS ||
                        s.omens[i].threat != prev.omens[i].threat;
            if (moved) d->add(0, BODY_Y, W, BODY_H);
            dirtyChrome(*d, s, prev);
        }
        prev = s;
    }
}

} // namespace ui
