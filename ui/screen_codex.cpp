// CODEX -- the register of what has been found.
//
// One row per network: band, name, signal, channel. The dial answers "what is
// around me right now"; this answers "what have I actually got", which is the
// question you have on a drive.
#include "screens.h"
#include "prevstate.h"
#include "palette.h"
#include "font5x7.h"
#include "layout.h"
#include "nav.h"

namespace ui {
namespace {

constexpr int BAR_X  = 94, BAR_W = 5, BAR_GAP = 1, BARS = 4;
constexpr int SSID_CH = 14;                     // characters that fit

uint16_t bandColor(uint8_t band) {
    switch (band) {
        case BAND_24: return pal::MAGENTA;
        case BAND_5:  return pal::ICE;
        default:      return pal::GREEN;
    }
}

void u2s(char* out, uint32_t v) {
    char tmp[12];
    int n = 0;
    do { tmp[n++] = (char)('0' + v % 10); v /= 10; } while (v && n < 11);
    int i = 0;
    while (n) out[i++] = tmp[--n];
    out[i] = 0;
}

// Four blocks rather than a continuous bar: at this size a bar's length is
// hard to read at a glance, but a count of lit blocks is immediate.
int bars(int8_t rssi) {
    if (rssi >= -55) return 4;
    if (rssi >= -67) return 3;
    if (rssi >= -78) return 2;
    if (rssi >= -88) return 1;
    return 0;
}

void drawRow(Canvas& c, int y, const Sighting& g) {
    const uint16_t bc = bandColor(g.band);
    c.fill(2, y + 1, 2, ROW_H - 3, bc);

    if (g.ssid[0]) {
        char name[SSID_CH + 1];
        int i = 0;
        for (; i < SSID_CH && g.ssid[i]; i++) name[i] = g.ssid[i];
        name[i] = 0;
        c.text(7, y + 2, name, pal::TEXT);
    } else {
        // A hidden network is a real finding, not a blank row.
        c.text(7, y + 2, "<HIDDEN>", pal::DIM);
    }

    const int lit = bars(g.rssi);
    for (int i = 0; i < BARS; i++) {
        const int bx = BAR_X + i * (BAR_W + BAR_GAP);
        const int bh = 2 + i * 2;                      // stepped, like a meter
        c.fill(bx, y + ROW_H - 3 - bh, BAR_W, bh, i < lit ? bc : pal::EDGE);
    }

    if (g.band == BAND_BLE) {
        c.textRight(151, y + 2, "BLE", pal::DIM);   // no channel to show
    } else {
        char ch[8] = {'C', 'H', 0};
        u2s(ch + 2, g.chan);
        c.textRight(151, y + 2, ch, pal::DIM);
    }
}

void drawScrollbar(Canvas& c, const State& s) {
    if (s.totalSights <= ROWS) return;
    const int trackY = BODY_Y + 2, trackH = BODY_H - 4;
    c.fill(155, trackY, 2, trackH, pal::EDGE);

    int thumb = trackH * ROWS / s.totalSights;
    if (thumb < 4) thumb = 4;
    const int span = s.totalSights - ROWS;
    const int pos = span > 0 ? (trackH - thumb) * s.scroll / span : 0;
    c.fill(155, trackY + pos, 2, thumb, pal::WITCH);
}

// A cheap fingerprint of what is on screen. Comparing it frame to frame is
// what keeps the codex from blitting its whole body 60 times a second when
// nothing has moved -- the body is 72% of the canvas, so that matters.
uint32_t rowsHash(const State& s) {
    uint32_t h = 2166136261u;
    h ^= (uint32_t)s.scroll;      h *= 16777619u;
    h ^= (uint32_t)s.totalSights; h *= 16777619u;
    h ^= (uint32_t)s.nSights;     h *= 16777619u;
    for (int i = 0; i < s.nSights; i++) {
        const Sighting& g = s.sights[i];
        for (int k = 0; k < 6; k++) { h ^= g.mac[k]; h *= 16777619u; }
        h ^= (uint8_t)g.rssi; h *= 16777619u;
        h ^= g.chan;          h *= 16777619u;
    }
    return h;
}

} // namespace

void drawCodex(Canvas& c, const State& s, Dirty* d) {
    c.clear(pal::VOID);
    // Titled by what it is showing, rather than a generic name plus a badge.
    const char* title = s.filter == F_5G  ? "5 GHZ"
                      : s.filter == F_BLE ? "BLE" : "WIFI";
    drawHeader(c, s, title);

    c.fill(1, BODY_Y, 158, BODY_H, pal::PANEL);
    c.rect(1, BODY_Y, 158, BODY_H, pal::EDGE);

    if (s.nSights == 0) {
        c.textCenter(W / 2, BODY_Y + BODY_H / 2 - 3, "NOTHING YET", pal::DIM);
    } else {
        for (int i = 0; i < s.nSights && i < ROWS; i++) {
            const int y = BODY_Y + 1 + i * ROW_H;
            drawRow(c, y, s.sights[i]);
            if (i)  c.hline(5, y, 148, Canvas::mix(pal::PANEL, pal::EDGE, 1, 2));
        }
        drawScrollbar(c, s);
    }

    drawFooter(c, s);
    drawReticle(c, s);

    c.vignette();
    c.scanlines();

    if (d) {
        static uint32_t prevHash = 0;
        State&    prev = prevState(SCR_CODEX);   // PSRAM on the board
        static bool     first = true;
        d->clear();
        if (first) {
            d->add(0, 0, W, H);
        } else {
            const uint32_t h = rowsHash(s);
            if (h != prevHash) d->add(0, BODY_Y, W, BODY_H);
            dirtyChrome(*d, s, prev);
            prevHash = h;
        }
        if (first) { prevHash = rowsHash(s); first = false; }
        prev = s;
    }
}

} // namespace ui
