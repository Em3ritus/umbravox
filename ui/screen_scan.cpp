#include "screens.h"
#include "palette.h"
#include "font5x7.h"
#include "runes.h"
#include "layout.h"
#include "signatures.h"
#include "seal.h"
#include "sweep.h"
#include "nav.h"
#include <math.h>

namespace ui {
namespace {

// ---- small helpers ------------------------------------------------------

// Unsigned to decimal, optionally zero-padded. No stdio on the device path.
void u2s(char* out, uint32_t v, int pad = 0) {
    char tmp[12];
    int n = 0;
    do { tmp[n++] = (char)('0' + v % 10); v /= 10; } while (v && n < 11);
    int i = 0;
    while (pad-- > n) out[i++] = '0';
    while (n) out[i++] = tmp[--n];
    out[i] = 0;
}

uint16_t bandColor(uint8_t band) {
    switch (band) {
        case BAND_24: return pal::MAGENTA;
        case BAND_5:  return pal::ICE;
        default:      return pal::GREEN;
    }
}

// Radar convention: 0 degrees is up, angle increases clockwise.
void polar(int cx, int cy, int r, float deg, int& x, int& y) {
    const float a = deg * 3.14159265f / 180.0f;
    x = cx + (int)lrintf(sinf(a) * (float)r);
    y = cy - (int)lrintf(cosf(a) * (float)r);
}

// The {7/3} heptagram: seven points, each joined to the one three along.
// UMBRAVOX's figure. The alert is inscribed with the active theme's own
// figure -- the ward is part of the same working, not separate chrome -- so
// alertFigure() below picks this or the seal's diamond-and-triangles.
void heptagram(Canvas& c, int cx, int cy, int r, uint16_t col) {
    int sx[7], sy[7];
    for (int k = 0; k < 7; k++)
        polar(cx, cy, r, (float)k * 360.0f / 7.0f, sx[k], sy[k]);
    for (int k = 0; k < 7; k++)
        c.line(sx[k], sy[k], sx[(k + 3) % 7], sy[(k + 3) % 7], col);
}

// Lattice, clipped to the scrying field. Outside the inner circle it would
// fight the rune band, which needs clean ground to read against.
void latticeDisc(Canvas& c, int cx, int cy, int r) {
    for (int j = cy - r; j <= cy + r; j++)
        for (int i = cx - r; i <= cx + r; i++) {
            if (i % 6 || j % 6) continue;
            const int dx = i - cx, dy = j - cy;
            if (dx * dx + dy * dy <= r * r) c.pixel(i, j, pal::GRID);
        }
}

// A card. `cross` swaps the corner dots for inscription marks -- used on the
// circle, which is the one panel that should look consecrated rather than
// merely drawn.
void panel(Canvas& c, int x, int y, int w, int h, uint16_t fillCol, bool cross = false) {
    c.fill(x, y, w, h, fillCol);
    c.rect(x, y, w, h, pal::EDGE);
    const uint16_t m = Canvas::mix(pal::EDGE, pal::BONE, 1, 2);
    const int ix[4] = {x + 2, x + w - 3, x + 2, x + w - 3};
    const int iy[4] = {y + 2, y + 2, y + h - 3, y + h - 3};
    for (int k = 0; k < 4; k++) {
        c.pixel(ix[k], iy[k], m);
        if (cross) {
            c.pixel(ix[k] - 1, iy[k], m); c.pixel(ix[k] + 1, iy[k], m);
            c.pixel(ix[k], iy[k] - 1, m); c.pixel(ix[k], iy[k] + 1, m);
        }
    }
}

// 5x5 status rune for the footer.
void statRune(Canvas& c, int x, int y, uint16_t col, bool filled) {
    static const uint8_t O[5] = {0b00100, 0b01010, 0b10001, 0b01010, 0b00100};
    static const uint8_t F[5] = {0b00100, 0b01110, 0b11111, 0b01110, 0b00100};
    const uint8_t* g = filled ? F : O;
    for (int j = 0; j < 5; j++)
        for (int i = 0; i < 5; i++)
            if ((g[j] >> (4 - i)) & 1) c.pixel(x + i, y + j, col);
}

// ---- pieces -------------------------------------------------------------

}  // namespace

const char* screenName(uint8_t screen, bool plain) {
    switch (screen) {
        case SCR_SCAN:     return "SCAN";                        // header shows the wordmark
        case SCR_CODEX:    return plain ? "FOUND"    : "CODEX";
        case SCR_DETAIL:   return plain ? "DETAIL"   : "SIGHTING";
        case SCR_DOMAIN:   return plain ? "MAP"      : "DOMAIN";
        case SCR_MESH:     return "MESH";
        case SCR_OMENS:    return plain ? "ALERTS"   : "OMENS";
        case SCR_ECHOES:   return plain ? "PROBES"   : "ECHOES";
        case SCR_GRIMOIRE: return plain ? "MENU"     : "GRIMOIRE";
        case SCR_ATTUNE:   return plain ? "SETTINGS" : "ATTUNE";
        case SCR_ASPECT:   return plain ? "DISPLAY"  : "ASPECT";
        default:           return "";
    }
}

void drawHeader(Canvas& c, const State& s, const char* title) {
    c.fill(0, 0, W, HDR_H - 1, pal::PANEL);
    c.hline(0, HDR_H - 1, W, pal::EDGE_LIT);

    // The menu button: a filled chip with a menu glyph, so it reads as the way
    // in to everything -- the grid, settings, alerts -- rather than as a mark.
    // A back arrow sits in the same spot on every sub-screen.
    const int mbw = 14;
    c.fill(0, 0, mbw, HDR_H - 1, Canvas::mix(pal::PANEL, pal::WITCH, 1, 2));
    c.rect(0, 0, mbw, HDR_H - 1, pal::WITCH_HI);
    if (s.screen == 0) {
        c.hline(3, 3, 8, pal::BRIGHT);        // three lines: the universal "menu"
        c.hline(3, 5, 8, pal::BRIGHT);
        c.hline(3, 7, 8, pal::BRIGHT);
    } else {
        static const uint8_t ARROW[7] = {
            0b0001000, 0b0011000, 0b0111000, 0b1111111,
            0b0111000, 0b0011000, 0b0001000,
        };
        for (int j = 0; j < 7; j++)
            for (int i = 0; i < 7; i++)
                if ((ARROW[j] >> (6 - i)) & 1) c.pixel(3 + i, 2 + j, pal::BRIGHT);
    }
    c.text(mbw + 3, 2, title, pal::WITCH_HI);

    // No cryptic header shortcut-marks any more: OMENS (alerts) and ATTUNE
    // (settings) are reached from the menu button, which opens a labelled grid.
    // That frees the masthead for the full wordmark and drops two mystery runes.

    // No battery gauge: this board has no battery sensing, and a gauge that
    // cannot be measured is a number made up on the device's behalf.
    const uint16_t bc = s.on5 ? pal::ICE : pal::MAGENTA;
    c.rect(140, 1, 17, 9, pal::EDGE);
    c.textCenter(148, 2, s.on5 ? "5G" : "24", bc);
}

namespace {

void drawTile(Canvas& c, int y, const char* label, uint32_t value, uint16_t accent) {
    panel(c, COL_L_X, y, COL_L_W, TILE_H, pal::PANEL);
    c.fill(COL_L_X + 1, y + 1, 2, TILE_H - 2, accent);
    c.text(COL_L_X + 6, y + 4, label, pal::DIM);
    c.text(COL_L_X + COL_L_W - 7, y + 4, ">", pal::EDGE_LIT);   // opens the codex
    char buf[12];
    u2s(buf, value);
    c.text(COL_L_X + 6, y + 12, buf, pal::BRIGHT, 2);
}

void drawSpectrum(Canvas& c, const State& s) {
    panel(c, COL_R_X, SPEC_Y, COL_R_W, SPEC_H, pal::PANEL);

    const int bw = 3, gap = 1, sep = 5;
    const int total = CH24 * (bw + gap) + sep + CH5 * (bw + gap) - gap;
    int x = COL_R_X + (COL_R_W - total) / 2;
    const int base = SPEC_Y + SPEC_H - 3;
    const int maxH = 10;

    c.hline(COL_R_X + 3, base + 1, COL_R_W - 6, pal::EDGE);
    c.text(COL_R_X + COL_R_W - 7, SPEC_Y + 2, ">", pal::EDGE_LIT);   // opens MESH

    // A node count, so the mesh is visible without opening it.
    if (s.nNodes) {
        char nb[6] = {0};
        nb[0] = (char)('0' + (s.nNodes > 9 ? 9 : s.nNodes));
        nb[1] = 'N';
        c.text(COL_R_X + 3, SPEC_Y + 2, nb, pal::GREEN);
    }

    for (int i = 0; i < CH24 + CH5; i++) {
        const bool is5 = i >= CH24;
        if (i == CH24) x += sep;
        const int load = is5 ? s.load5[i - CH24] : s.load24[i];
        const int chNum = is5 ? CHAN5[i - CH24] : CHAN24[i];
        const bool here = (s.on5 == is5) && (chNum == s.chan);

        const int h = 1 + load * (maxH - 1) / 100;
        uint16_t col = Canvas::mix(pal::EDGE, is5 ? pal::ICE : pal::MAGENTA,
                                   40 + load * 60 / 100, 100);
        if (here) col = pal::BRIGHT;

        c.fill(x, base - h + 1, bw, h, col);
        if (here) c.fill(x, SPEC_Y + 2, bw, 1, pal::WITCH_HI);
        x += bw + gap;
    }
}

}  // namespace

void drawFooter(Canvas& c, const State& s) {
    c.hline(0, FOOT_Y, W, pal::EDGE_LIT);
    c.fill(0, FOOT_Y + 1, W, H - FOOT_Y - 1, pal::PANEL);
    const int ty = FOOT_Y + 5;

    const uint16_t fc = s.gpsFix ? pal::GREEN : pal::AMBER;
    statRune(c, 3, ty + 1, fc, s.gpsFix);
    if (s.gpsFix) {
        char b[12] = {'F', 'I', 'X', ' ', 0};
        u2s(b + 4, s.sats, 2);
        int n = 0; while (b[n]) n++;
        b[n] = 'S'; b[n + 1] = 'V'; b[n + 2] = 0;
        c.text(11, ty, b, fc);
    } else {
        c.text(11, ty, "NO FIX", fc);
    }

    // The record control, framed as a button so it reads as tappable, sitting
    // on the right. The universal record dot: hollow = ready to log, filled =
    // recording. The elapsed time shows only while recording -- there is no
    // idle clock, which was just noise competing with the controls.
    const bool rec    = (s.logState == 1);
    const bool nocard = (s.logState == 2) || (s.logState == 0 && !s.sdOk);
    const int  rx = LOG_X, ry = FOOT_Y + 2, rw = LOG_W, rh = H - FOOT_Y - 3;
    c.rect(rx, ry, rw, rh, (rec || nocard) ? pal::RED : pal::EDGE_LIT);
    const int dx = rx + 5, dy = ty + 3;
    if (rec) c.disc(dx, dy, 2, pal::RED);
    else     c.circle(dx, dy, 2, nocard ? pal::RED : pal::WITCH_HI);

    if (nocard) {
        c.text(rx + 12, ty, "NO CARD", pal::RED);
    } else if (rec) {
        char r[12] = {'R', 'E', 'C', ' ', 0};
        uint32_t mm = s.logSecs / 60, ss = s.logSecs % 60;   // saturate past 99:59
        if (mm > 99) { mm = 99; ss = 59; }
        u2s(r + 4, mm, 2); r[6] = ':'; u2s(r + 7, ss, 2);
        c.text(rx + 12, ty, r, pal::BRIGHT);
    } else {
        c.text(rx + 12, ty, "REC", pal::WITCH_HI);
    }
}

// The alert card takes over the circle, which is the largest area and the
// least information-dense at the moment something is being named. The tiles,
// spectrum and footer stay readable underneath it.
//
// It materialises rather than appearing: a flare, the ward tracing itself
// closed, the name resolving out of noise, and then a slow pulse while it
// holds. The timings are short enough that it never delays the reading --
// the whole thing is legible inside a second.
namespace {

constexpr int ALERT_FLARE_MS = 90;    // the strike
constexpr int ALERT_TRACE_MS = 300;   // the ward closing
constexpr int ALERT_CHAR_MS  = 55;    // per character of the name resolving

// A point `d` steps clockwise around a rectangle's perimeter, starting at
// top-centre. Used to trace the border on and to walk runes around it.
void perim(int x, int y, int w, int h, int d, int& px, int& py) {
    const int half = w / 2;
    const int segA = w - half - 1;     // top-centre -> top-right
    const int segB = h - 1;            // down the right
    const int segC = w - 1;            // along the bottom
    const int segD = h - 1;            // up the left
    const int total = segA + segB + segC + segD + half;
    d = ((d % total) + total) % total;

    if (d < segA)                 { px = x + half + d;       py = y; return; }
    d -= segA;
    if (d < segB)                 { px = x + w - 1;          py = y + d; return; }
    d -= segB;
    if (d < segC)                 { px = x + w - 1 - d;      py = y + h - 1; return; }
    d -= segC;
    if (d < segD)                 { px = x;                  py = y + h - 1 - d; return; }
    d -= segD;
    px = x + d; py = y;
}

int perimLen(int w, int h) { return (w - w / 2 - 1) + (h - 1) + (w - 1) + (h - 1) + w / 2; }

// Cheap deterministic noise, so the scramble differs per frame and per slot
// without carrying a PRNG around.
uint32_t noise(uint32_t a, uint32_t b) {
    uint32_t h = a * 374761393u + b * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}

// The inverted pentagram -- point down -- that frames the Baphomet seal. The
// summoning figure for the BAPH0MAU5 alert, where UMBRAVOX uses the ward.
// Optionally hands back the five outer points so the hold can set them alight.
void pentagram(Canvas& c, int cx, int cy, int r, uint16_t col,
               int* px = nullptr, int* py = nullptr) {
    int sx[5], sy[5];
    for (int k = 0; k < 5; k++) polar(cx, cy, r, 180.0f + (float)k * 72.0f, sx[k], sy[k]);
    for (int k = 0; k < 5; k++) c.line(sx[k], sy[k], sx[(k + 2) % 5], sy[(k + 2) % 5], col);
    if (px && py) for (int k = 0; k < 5; k++) { px[k] = sx[k]; py[k] = sy[k]; }
}

// The same pentagram drawn only part-way -- one continuous unicursal stroke
// (0->2->4->1->3->0) advanced by num/den -- so it can inscribe itself. The pen
// tip comes back in tip{x,y} for the spark that rides it.
int pentagramTrace(Canvas& c, int cx, int cy, int r, uint16_t col,
                   int num, int den, int& tipx, int& tipy) {
    int sx[5], sy[5];
    for (int k = 0; k < 5; k++) polar(cx, cy, r, 180.0f + (float)k * 72.0f, sx[k], sy[k]);
    static const int ord[6] = {0, 2, 4, 1, 3, 0};
    long prog = (long)num * 5000 / (den > 0 ? den : 1);
    if (prog > 5000) prog = 5000;
    if (prog < 0) prog = 0;
    const int full = (int)(prog / 1000), part = (int)(prog % 1000);
    for (int i = 0; i < full; i++)
        c.line(sx[ord[i]], sy[ord[i]], sx[ord[i + 1]], sy[ord[i + 1]], col);
    tipx = sx[ord[full]]; tipy = sy[ord[full]];
    if (full < 5) {
        const int ax = sx[ord[full]], ay = sy[ord[full]];
        const int bx = sx[ord[full + 1]], by = sy[ord[full + 1]];
        tipx = ax + (bx - ax) * part / 1000;
        tipy = ay + (by - ay) * part / 1000;
        c.line(ax, ay, tipx, tipy, col);
    }
    return full;
}

} // namespace

void alertFigure(Canvas& c, const State& s, int cx, int cy, int r, uint16_t col) {
    if (s.set.theme == pal::TH_BAPH0MAU5) sealFigure(c, cx, cy, r, col);
    else                                  heptagram(c, cx, cy, r, col);
}

namespace {

// The threat name resolving out of marks, and the supporting detail arriving
// only once it has settled. Shared by both themes' rites and drawn on top of
// whatever ceremony framed the card.
void alertNameAndDetail(Canvas& c, const State& s, uint16_t edge,
                        int X, int Y, int Wd, int age) {
    const int cx = X + Wd / 2;
    rune5c(c, 0, cx, Y + 10, edge);        // the eye

    // Runes rather than keyboard symbols: a name surfacing from marks reads as
    // a thing being named, where '#$%&' reads as a terminal with a bad line.
    const char* name = threatName(s.alertThreat);
    int n = 0; while (name[n]) n++;
    const int settle = ALERT_TRACE_MS;
    const int startX = cx - Font::width(name, 2) / 2;
    for (int i = 0; i < n; i++) {
        const int gx = startX + i * Font::ADV * 2;
        const int lockAt = settle + i * ALERT_CHAR_MS;
        if (age >= lockAt) {
            const char ch[2] = {name[i], 0};
            c.text(gx, Y + 17, ch, pal::BRIGHT, 2);
        } else {
            rune5sc(c, (int)noise((uint32_t)(age / 70), (uint32_t)i),
                    gx + Font::W, Y + 17 + Font::H, edge, 2);
        }
    }

    // The supporting detail arrives only once the name has settled, so the
    // eye reads the threat first.
    const int tail = settle + n * ALERT_CHAR_MS;
    if (age > tail)        c.textCenter(cx, Y + 36, s.alertWhy, pal::DIM);
    if (age > tail + 80 && s.alertName[0])
        c.textCenter(cx, Y + 46, s.alertName, pal::TEXT);
    if (age > tail + 160)  c.textCenter(cx, Y + 58, confName(s.alertConf), edge);
}

// BAPH0MAU5's rite: the seal takes the card where UMBRAVOX traces a ward.
// Fire climbs out of the centre while an inverted pentagram inscribes itself;
// the pentacle snaps shut and its five points gutter as it holds. Confidence
// rides the fire -- violet embers at LOW, a green-white blaze with a red heart
// at HIGH -- so a weak guess never burns like a certainty.
void drawAlertSummoned(Canvas& c, const State& s, uint16_t edge,
                       int X, int Y, int Wd, int Hd, int age, int mx, int my) {
    const uint16_t core = s.alertConf == CONF_HIGH ? Canvas::mix(pal::RED,   pal::LIVE_HI, 1, 2)
                        : s.alertConf == CONF_MED  ? Canvas::mix(pal::AMBER, pal::LIVE_HI, 1, 2)
                        :                            Canvas::mix(pal::WITCH, pal::LIVE,    1, 3);
    const int heatN = s.alertConf == CONF_HIGH ? 5 : s.alertConf == CONF_MED ? 3 : 2;  // /10
    const int r = Hd / 2 - 6;

    // 1. The strike: the pentagram slams down oversized and contracts onto the
    //    ring it will inscribe, the confidence colour washing out of it.
    if (age < ALERT_FLARE_MS) {
        const int k = ALERT_FLARE_MS - age;
        c.fill(X, Y, Wd, Hd, Canvas::mix(pal::VOID, edge, k, ALERT_FLARE_MS * 2));
        pentagram(c, mx, my, r + k / 4, Canvas::mix(edge, pal::BRIGHT, k, ALERT_FLARE_MS));
        return;
    }

    const bool held = age >= ALERT_TRACE_MS;

    // 2. The fire climbs from the centre: a bloom that grows with the drawing,
    //    then settles to a guttering ember once the ring is sealed.
    const int fade = held ? 2 : 1;        // the held ember is dimmer than the rising fire
    int br;
    if (!held) br = (r + 6) * (age - ALERT_FLARE_MS) / (ALERT_TRACE_MS - ALERT_FLARE_MS);
    else       br = r / 2 + (int)(noise((uint32_t)(age / 80), 11) % 7) - 3;
    if (br > 0) {
        c.disc(mx, my, br,         Canvas::mix(pal::VOID, edge, heatN, 10 * fade));
        c.disc(mx, my, br * 2 / 3, Canvas::mix(pal::VOID, edge, heatN, 6 * fade));
        c.disc(mx, my, br / 3,     Canvas::mix(pal::VOID, core, 1, fade));
    }

    if (!held) {
        // 3. The pentagram inscribes itself, a green spark riding the pen.
        pentagram(c, mx, my, r, Canvas::mix(pal::VOID, pal::WITCH, 1, 6));  // ghost of the rest
        int tipx, tipy;
        pentagramTrace(c, mx, my, r, pal::WITCH_HI, age - ALERT_FLARE_MS,
                       ALERT_TRACE_MS - ALERT_FLARE_MS, tipx, tipy);
        c.disc(tipx, tipy, 1, pal::LIVE_HI);
    } else {
        // 4. Sealed: a dim pentagram under a closed pentacle, five points
        //    alight, the downward point -- aimed at the name -- brightest.
        int px[5], py[5];
        pentagram(c, mx, my, r, Canvas::mix(pal::VOID, pal::WITCH, 1, 3), px, py);
        c.circle(mx, my, r, Canvas::mix(pal::VOID, pal::WITCH, 1, 2));
        for (int k = 0; k < 5; k++) {
            const int fl = (int)(noise((uint32_t)(age / 90), (uint32_t)k) % 20);
            c.disc(px[k], py[k], 1, Canvas::mix(edge, pal::LIVE_HI, 20 + fl, 44));
        }
        c.disc(px[0], py[0], 2, pal::LIVE_HI);

        // the inner edge wanders by candlelight, as the UMBRAVOX ward does.
        const uint32_t fl = noise((uint32_t)(age / 90), 7) % 24;
        c.rect(X + 1, Y + 1, Wd - 2, Hd - 2, Canvas::mix(pal::VOID, edge, 14 + (int)fl, 60));
    }

    alertNameAndDetail(c, s, edge, X, Y, Wd, age);
}

} // namespace

void drawAlert(Canvas& c, const State& s) {
    if (!s.alertActive || s.alertThreat == TH_NONE) return;

    // Colour is the confidence, not the threat. A LOW-confidence hit on
    // something frightening is still a guess, and should not look like a
    // certainty.
    const uint16_t edge = s.alertConf == CONF_HIGH ? pal::RED
                        : s.alertConf == CONF_MED  ? pal::AMBER : pal::WITCH;

    const int X = COL_R_X, Y = DIAL_Y, Wd = COL_R_W, Hd = DIAL_H;
    const int age = (int)s.alertAge;

    c.fill(X, Y, Wd, Hd, pal::VOID);
    const int mx = X + Wd / 2, my = Y + Hd / 2;

    // BAPH0MAU5 runs its own rite -- the seal takes the card -- where
    // UMBRAVOX traces a ward. Both share the name resolving out of marks.
    if (s.set.theme == pal::TH_BAPH0MAU5) {
        drawAlertSummoned(c, s, edge, X, Y, Wd, Hd, age, mx, my);
        return;
    }

    // 1. The strike: a sigil slams down. The figure arrives oversized and
    //    contracts onto the card while the colour wash decays out of it.
    if (age < ALERT_FLARE_MS) {
        const int k = ALERT_FLARE_MS - age;
        c.fill(X, Y, Wd, Hd, Canvas::mix(pal::VOID, edge, k, ALERT_FLARE_MS * 2));
        alertFigure(c, s, mx, my, 30 + k / 5, Canvas::mix(edge, pal::BRIGHT, k, ALERT_FLARE_MS));
        return;
    }

    // The figure stays, faint, under everything that follows.
    alertFigure(c, s, mx, my, 30, Canvas::mix(pal::VOID, edge, 1, 5));

    // 2. The ward closes: the border traces itself from top-centre, both
    //    ways at once, and only locks shut at the end.
    const int plen = perimLen(Wd, Hd);
    const int drawn = age >= ALERT_TRACE_MS ? plen
                    : plen * age / ALERT_TRACE_MS;
    for (int d = 0; d <= drawn / 2; d++) {
        int px, py;
        perim(X, Y, Wd, Hd, d, px, py);   c.pixel(px, py, edge);
        perim(X, Y, Wd, Hd, -d, px, py);  c.pixel(px, py, edge);
    }

    if (age >= ALERT_TRACE_MS) {
        // 3. The seal: where the two ends meet, briefly, as the ring closes.
        const int sinceSeal = age - ALERT_TRACE_MS;
        if (sinceSeal < 120) {
            const int k = 120 - sinceSeal;
            const uint16_t f = Canvas::mix(edge, pal::BRIGHT, k, 120);
            c.disc(X + Wd / 2, Y, 1 + k / 40, f);
            rune5c(c, 3, X + Wd / 2, Y, f);
        }

        // 4. It holds by candlelight, not on a sine wave -- the brightness
        //    wanders rather than breathing evenly.
        const uint32_t fl = noise((uint32_t)(age / 90), 7) % 24;
        const uint16_t inner = Canvas::mix(pal::VOID, edge, 14 + (int)fl, 60);
        c.rect(X + 1, Y + 1, Wd - 2, Hd - 2, inner);

        // 5. Inscription: runes walking the perimeter, against the flicker.
        for (int i = 0; i < 8; i++) {
            int px, py;
            perim(X, Y, Wd, Hd, (age / 30) + i * plen / 8, px, py);
            rune5c(c, i + (int)(age / 900), px, py, Canvas::mix(pal::VOID, edge, 2, 3));
        }
    }

    alertNameAndDetail(c, s, edge, X, Y, Wd, age);
}

void drawReticle(Canvas& c, const State& s) {
    if (!s.touchDown) return;
    const int x = s.touchX, y = s.touchY;
    c.circle(x, y, 5, pal::WITCH_HI);
    c.circle(x, y, 2, pal::WITCH);
    c.hline(x - 7, y, 3, pal::WITCH_HI);
    c.hline(x + 5, y, 3, pal::WITCH_HI);
    c.vline(x, y - 7, 3, pal::WITCH_HI);
    c.vline(x, y + 5, 3, pal::WITCH_HI);
    c.pixel(x, y, pal::BRIGHT);
}

namespace {

}  // namespace

void dirtyChrome(Dirty& d, const State& s, const State& p) {
    // The header carries the band badge and the battery, both of which change
    // on their own schedule while you are sitting on some other screen.
    if (s.on5 != p.on5 || s.screen != p.screen || s.filter != p.filter)
        d.add(0, 0, W, HDR_H);

    if (s.uptimeS != p.uptimeS || s.sats != p.sats ||
        s.gpsFix != p.gpsFix || s.sdOk != p.sdOk ||
        s.logState != p.logState || s.logSecs != p.logSecs)
        d.add(0, FOOT_Y, W, H - FOOT_Y);

    // The reticle needs two regions: where it is, and where it was.
    constexpr int R = 9;
    if (s.touchDown) d.add(s.touchX - R, s.touchY - R, R * 2, R * 2);
    if (p.touchDown && (p.touchX != s.touchX || p.touchY != s.touchY || !s.touchDown))
        d.add(p.touchX - R, p.touchY - R, R * 2, R * 2);
}

namespace {

// Which regions this frame actually changed. The circle is always moving, so
// it is always dirty; everything else only when its numbers do.
void markDirty(Dirty& d, const State& s, const State& p, bool first) {
    d.clear();

    // First frame pushes everything, and it has to: most of this screen is
    // static chrome -- panel fills, borders, corner marks, the `-90` ring
    // label -- that no later region covers, because none of it ever changes.
    // Marking only the moving parts here leaves that chrome drawn in the
    // canvas but never sent, so the glass keeps whatever fillScreen() left.
    if (first) {
        d.add(0, 0, W, H);
        return;
    }

    // The alert card covers the whole panel, not just the circle's bounding
    // box, so while one is up (or has just cleared) the panel goes as a unit.
    if (s.alertActive || p.alertActive)
        d.add(COL_R_X, DIAL_Y, COL_R_W, DIAL_H);
    else
        d.add(DIAL_CX - R_BOUND, DIAL_CY - R_BOUND, 2 * R_BOUND + 1, 2 * R_BOUND + 1);

    // The channel readout and the MESH marker sit outside that box, so they
    // need their own region -- and it must repaint when a node comes or goes.
    if (s.chan != p.chan || s.nNodes != p.nNodes)
        d.add(COL_R_X + 1, DIAL_Y + 1, COL_R_W - 2, 10);

    if (s.nets24 != p.nets24 || s.nets5 != p.nets5)
        d.add(COL_L_X, TILE_Y[0], COL_L_W, TILE_H * 2 + 2);
    if (s.ble != p.ble)
        d.add(COL_L_X, TILE_Y[2], COL_L_W, TILE_H);

    bool spec = s.chan != p.chan || s.on5 != p.on5 || s.nNodes != p.nNodes;
    for (int i = 0; !spec && i < CH24; i++) spec = s.load24[i] != p.load24[i];
    for (int i = 0; !spec && i < CH5;  i++) spec = s.load5[i]  != p.load5[i];
    if (spec && SPEC_H > 0) d.add(COL_R_X, SPEC_Y, COL_R_W, SPEC_H);

    dirtyChrome(d, s, p);
}

} // namespace

void drawDialAt(Canvas& c, const State& s, int cx, int cy,
                int rField, int rRunes, int rStar, int rBound) {
    // The eyes answer a finding, so the seal needs to know whether the search
    // has just turned one up and roughly where.
    bool found = false;
    int  foundAngle = 0;
    for (int i = 0; i < s.nContacts; i++)
        if (searchTouches(s.set.sweep, s.frame, s.contacts[i].angle, s.contacts[i].dist, rField)) {
            found = true; foundAngle = s.contacts[i].angle; break;
        }

    if (s.set.theme == pal::TH_BAPH0MAU5) {
        // heat is alertConf * 85, and alertConf is one of CONF_LOW..CONF_OFF
        // (0..3), so heat only ever takes the values 0, 85, 170 or 255.
        // seal.cpp's blink gate depends on that discreteness: a continuous
        // mapping would change blink behaviour in the 1..39 band.
        drawSeal(c, cx, cy, rField, rRunes, rStar, rBound,
                 s.frame, found, foundAngle,
                 s.alertActive ? (uint8_t)(s.alertConf * 85) : 0,
                 searchLift(s.set.sweep, s.frame), s.set.sweep);
    } else {
        latticeDisc(c, cx, cy, rField - 1);

        // The inscribed figure. A {7/3} heptagram: seven points, each joined to
        // the one three along. Static, so the rune ring is the only thing moving
        // and the two do not fight.
        heptagram(c, cx, cy, rStar, Canvas::mix(pal::PANEL, pal::WITCH, 1, 3));

        // Range rings and crosshair, under everything live.
        c.dottedCircle(cx, cy, 14, pal::EDGE, 2);
        c.dottedCircle(cx, cy, 7,  pal::EDGE, 2);
        for (int i = -rField; i <= rField; i += 3) {
            c.pixel(cx + i, cy, pal::EDGE);
            c.pixel(cx, cy + i, pal::EDGE);
        }

        // The circle proper: field bound, rune band, outer bound.
        c.circle(cx, cy, rField, pal::EDGE_LIT);
        c.circle(cx, cy, rBound, pal::EDGE_LIT);

        // Runes walk the band, slowly and against the sweep. Dimming the ones
        // away from the beam keeps the ring from reading as a flat border.
        const float spin = -(float)s.frame * 0.7f;
        for (int k = 0; k < RUNE_N; k++) {
            const float a = spin + (float)k * (360.0f / (float)RUNE_N);
            int rx, ry;
            polar(cx, cy, rRunes, a, rx, ry);
            const int lum = searchMarkLum(s.set.sweep, s.frame, a, 100);
            rune5c(c, k, rx, ry, Canvas::mix(pal::VOID, pal::BONE, lum, 255));
        }
    }

    int eyeLx, eyeRx, eyeY;
    sealEyes(eyeLx, eyeRx, eyeY);
    drawSearch(c, s.set.sweep, cx, cy, rField,
               eyeLx, eyeRx, eyeY, s.frame);

    // Contacts. Radius is RSSI. A blip the beam just crossed is drawn hot and
    // crossed; the rest decay so the field does not silt up.
    for (int i = 0; i < s.nContacts; i++) {
        const Contact& k = s.contacts[i];
        int x, y;
        polar(cx, cy, contactRadius(k.dist, rField), (float)k.angle, x, y);

        const bool hot = searchTouches(s.set.sweep, s.frame, k.angle, k.dist, rField);
        int lum = 255 - (int)k.age;
        if (lum < 115) lum = 115;
        if (hot) lum = 255;

        const uint16_t col = Canvas::mix(pal::VOID, bandColor(k.band), lum, 255);
        c.pixel(x, y, col);
        if (hot) {
            c.pixel(x - 1, y, col); c.pixel(x + 1, y, col);
            c.pixel(x, y - 1, col); c.pixel(x, y + 1, col);
            c.pixel(x, y, pal::BRIGHT);
        }
    }

    c.pixel(cx, cy, pal::WITCH_HI);   // us
}

void drawScan(Canvas& c, const State& s, Dirty* d) {
    c.clear(pal::VOID);

    // Title tracks the active theme. UMBRAVOX keeps its name (and its render
    // baselines); the cult theme announces itself in full.
    // The scan screen is home: it wears the brand wordmark even in plain-label
    // mode, which renames the sub-screens, not the masthead.
    drawHeader(c, s, s.set.theme == pal::TH_BAPH0MAU5 ? "CVLT OF BAPH0MAU5"
                                                      : "UMBRAVOX");
    drawTile(c, TILE_Y[0], "WIFI",  s.nets24 + s.nets5, pal::MAGENTA);
    drawTile(c, TILE_Y[1], "5 GHZ", s.nets5,            pal::ICE);
    drawTile(c, TILE_Y[2], "BLE",   s.ble,              pal::GREEN);
    panel(c, COL_R_X, DIAL_Y, COL_R_W, DIAL_H,
          Canvas::mix(pal::VOID, pal::PANEL, 1, 2), true);
    {
        char ch[8] = {'C', 'H', 0};
        u2s(ch + 2, s.chan, 2);
        c.textRight(COL_R_X + COL_R_W - 4, DIAL_Y + 3, ch, pal::DIM);
        // Parked on the collection channel for the mesh: say so, or a channel
        // that never moves reads as a stuck radio. Coverage comes from the
        // nodes then, shown mesh-wide on the spectrum and the dial.
        if (s.nNodes) c.text(COL_R_X + 3, DIAL_Y + 3, "MESH", pal::GREEN);
        c.text(COL_R_X + 3, DIAL_Y + DIAL_H - 9, "-90", pal::DIM);
    }
    drawDialAt(c, s, DIAL_CX, DIAL_CY, R_FIELD, R_RUNES, R_STAR, R_BOUND);
    drawAlert(c, s);
    if (SPEC_H > 0) drawSpectrum(c, s);
    drawFooter(c, s);
    drawReticle(c, s);

    c.vignette();    // light lives in the middle of the circle
    c.scanlines();   // last, so it sits over the type as well

    if (d) {
        static State prev;
        static bool  first = true;
        markDirty(*d, s, prev, first);
        prev  = s;
        first = false;
    }
}

} // namespace ui
