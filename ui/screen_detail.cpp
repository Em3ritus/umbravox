// SIGHTING -- everything known about one device.
//
// The codex answers "what have I got"; this answers "what is this, and is it
// getting closer". The signal history is the reason the screen earns its
// place: a single RSSI number tells you nothing, while a line that has been
// climbing for the last twenty sightings tells you that you are driving
// toward whatever it is.
#include "screens.h"
#include "prevstate.h"
#include "nav.h"
#include "palette.h"
#include "font5x7.h"
#include "layout.h"
#include "signatures.h"
#include "wifi_auth.h"

namespace ui {
namespace {

constexpr int CHART_Y = 62, CHART_H = 30;
constexpr int CHART_X = 6,  CHART_W = 148;
constexpr int RSSI_TOP = -30, RSSI_BOT = -95;   // the chart's vertical range

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

void cat(char* o, const char* s) { int i = 0; while (o[i]) i++; for (int k = 0; s[k]; k++) o[i++] = s[k]; o[i] = 0; }

// Seconds to something a glance can use: 45S, 12M, 3H.
void ago(char* out, uint32_t secs) {
    out[0] = 0;
    char n[12];
    if (secs < 60)        { u2s(n, secs);        cat(out, n); cat(out, "S"); }
    else if (secs < 3600) { u2s(n, secs / 60);   cat(out, n); cat(out, "M"); }
    else                  { u2s(n, secs / 3600); cat(out, n); cat(out, "H"); }
}

void macStr(char* out, const uint8_t* m) {
    static const char* H = "0123456789ABCDEF";
    int k = 0;
    for (int i = 0; i < 6; i++) {
        out[k++] = H[(m[i] >> 4) & 0xF];
        out[k++] = H[m[i] & 0xF];
        if (i < 5) out[k++] = ':';
    }
    out[k] = 0;
}

int rssiToY(int rssi) {
    if (rssi > RSSI_TOP) rssi = RSSI_TOP;
    if (rssi < RSSI_BOT) rssi = RSSI_BOT;
    return CHART_Y + CHART_H - 1
         - (rssi - RSSI_BOT) * (CHART_H - 1) / (RSSI_TOP - RSSI_BOT);
}

// Filled area rather than a line: at this size a one-pixel trace disappears
// into the lattice, while a filled shape reads as a shape.
void drawChart(Canvas& c, const Detail& d) {
    c.fill(CHART_X, CHART_Y, CHART_W, CHART_H, Canvas::mix(pal::VOID, pal::PANEL, 1, 2));
    c.rect(CHART_X, CHART_Y, CHART_W, CHART_H, pal::EDGE);

    // Reference lines at -50 and -75, so the shape has a scale to sit against.
    static const int REF[2] = {-50, -75};
    for (int k = 0; k < 2; k++) {
        const int y = rssiToY(REF[k]);
        for (int x = CHART_X + 2; x < CHART_X + CHART_W - 2; x += 3) c.pixel(x, y, pal::EDGE);
    }

    if (d.histN == 0) {
        c.textCenter(CHART_X + CHART_W / 2, CHART_Y + CHART_H / 2 - 3, "NO HISTORY", pal::DIM);
        return;
    }

    const uint16_t col = bandColor(d.band);
    const int step = CHART_W > 8 ? (CHART_W - 6) / HIST_N : 1;
    for (int i = 0; i < d.histN; i++) {
        const int x = CHART_X + 3 + i * step;
        const int y = rssiToY(d.hist[i]);
        c.vline(x, y, CHART_Y + CHART_H - 1 - y, Canvas::mix(pal::VOID, col, 1, 3));
        c.pixel(x, y, col);
        if (i == d.histN - 1) {        // the newest sample, called out
            c.pixel(x, y - 1, pal::BRIGHT);
            c.pixel(x, y,     pal::BRIGHT);
        }
    }
}

} // namespace

void drawDetail(Canvas& c, const State& s, Dirty* d) {
    const Detail& v = s.detail;
    c.clear(pal::VOID);
    drawHeader(c, s, screenName(SCR_DETAIL, s.set.labels));

    if (!v.valid) {
        c.textCenter(W / 2, BODY_Y + 30, "GONE", pal::DIM);
    } else {
        const uint16_t bc = bandColor(v.band);

        c.text(6, 15, v.ssid[0] ? v.ssid : "<HIDDEN>",
               v.ssid[0] ? pal::BRIGHT : pal::DIM);

        char buf[24];
        macStr(buf, v.mac);
        c.text(6, 25, buf, pal::TEXT);

        // Band, channel and signal on one line: the three facts you compare
        // between two devices.
        buf[0] = 0;
        cat(buf, v.band == BAND_5 ? "5GHZ" : v.band == BAND_24 ? "2.4GHZ" : "BLE");
        if (v.band != BAND_BLE) { char n[8]; cat(buf, " CH"); u2s(n, v.chan); cat(buf, n); }
        c.text(6, 35, buf, bc);

        buf[0] = 0;
        char n[8];
        u2s(n, (uint32_t)(-v.rssi));
        cat(buf, "-"); cat(buf, n); cat(buf, "DBM");
        c.textRight(W - 6, 35, buf, pal::BRIGHT);

        if (v.band != BAND_BLE) {
            // Open networks are the interesting case, so they get the ink.
            const bool open = !(v.auth & (AUTH_WPA | AUTH_WPA2 | AUTH_WEP));
            c.text(6, 25 + 0, "", pal::DIM);
            c.textRight(W - 6, 25, authShort(v.auth), open ? pal::AMBER : pal::DIM);
        }

        if (v.threat != TH_NONE) {
            const uint16_t ec = v.conf == CONF_HIGH ? pal::RED
                              : v.conf == CONF_MED  ? pal::AMBER : pal::WITCH;
            c.fill(6, 45, 2, 13, ec);
            c.text(11, 47, threatName(v.threat), ec);
            c.textRight(W - 6, 47, confName(v.conf), ec);
            c.text(11 + Font::width(threatName(v.threat)) + 6, 47, v.why, pal::DIM);
        } else {
            c.text(6, 47, "NO SIGNATURE MATCH", pal::DIM);
        }

        drawChart(c, v);

        char a[12], b[12];
        ago(a, v.lastAgoS);
        ago(b, v.firstAgoS);
        buf[0] = 0; cat(buf, "SEEN "); cat(buf, a); cat(buf, " AGO");
        c.text(6, 94, buf, pal::DIM);
        buf[0] = 0; cat(buf, "FIRST "); cat(buf, b);
        c.textRight(W - 6, 94, buf, pal::DIM);
    }

    drawFooter(c, s);
    drawReticle(c, s);
    c.vignette();
    c.scanlines();

    if (d) {
        State& prev = prevState(SCR_DETAIL);   // PSRAM on the board
        static bool  first = true;
        d->clear();
        if (first) { d->add(0, 0, W, H); first = false; }
        else {
            // The chart and the age lines move on every sighting, so the body
            // goes as a unit when anything about the device changes.
            if (v.rssi != prev.detail.rssi || v.histN != prev.detail.histN ||
                v.lastAgoS != prev.detail.lastAgoS || v.valid != prev.detail.valid ||
                v.threat != prev.detail.threat)
                d->add(0, BODY_Y, W, BODY_H);
            dirtyChrome(*d, s, prev);
        }
        prev = s;
    }
}

} // namespace ui
