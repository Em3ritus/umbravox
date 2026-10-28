// ECHOES -- probe requests: who is looking for what.
//
// Every other screen here is about infrastructure. This one is about people:
// a client calling out for a named network has joined that network before,
// so a phone probing for a hotel SSID has stayed there. That is a different
// kind of finding, and the reason the randomised-address column matters --
// an un-randomised probe is a far stronger identifier than a rotating one.
#include "screens.h"
#include "prevstate.h"
#include "nav.h"
#include "palette.h"
#include "font5x7.h"
#include "layout.h"

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
    if (s < 60)        { u2s(n, s);      cat(out, n); cat(out, "S"); }
    else if (s < 3600) { u2s(n, s / 60); cat(out, n); cat(out, "M"); }
    else               { u2s(n, s/3600); cat(out, n); cat(out, "H"); }
}

} // namespace

void drawEchoes(Canvas& c, const State& s, Dirty* d) {
    c.clear(pal::VOID);

    char title[16];
    const char* base = screenName(SCR_ECHOES, s.set.labels);  // ECHOES / PROBES
    int k = 0; for (; base[k] && k < 10; k++) title[k] = base[k];
    if (s.echoTotal > MAX_ECHO_ROWS) {
        title[k++] = ' ';
        char n[8]; u2s(n, s.echoTotal);
        for (int i = 0; n[i] && k < 15; i++) title[k++] = n[i];
    }
    title[k] = 0;
    drawHeader(c, s, title);

    if (s.echoClients) {
        char cl[16]; cl[0] = 0;
        char n[12]; u2s(n, s.echoClients);
        cat(cl, n); cat(cl, " CLIENTS");
        c.textRight(137, 2, cl, pal::DIM);
    }

    c.fill(1, BODY_Y, 158, BODY_H, pal::PANEL);
    c.rect(1, BODY_Y, 158, BODY_H, pal::EDGE);

    c.text(8,  HEAD_Y, "SEEKING", pal::EDGE_LIT);
    c.textRight(130, HEAD_Y, "WHO", pal::EDGE_LIT);
    c.textRight(155, HEAD_Y, "AGE", pal::EDGE_LIT);
    c.hline(5, RULE_Y, 148, pal::EDGE);

    if (s.nEchoes == 0) {
        c.textCenter(W / 2, ROW_Y + 20, "NO PROBES HEARD", pal::DIM);
        c.textCenter(W / 2, ROW_Y + 32, "CLIENTS ARE QUIET OR ABSENT", pal::EDGE_LIT);
    }

    char buf[24];
    for (int i = 0; i < s.nEchoes; i++) {
        const Echo& e = s.echoes[i];
        const int y = ROW_Y + i * ROW_HT;

        // The network being sought is the finding; it gets the ink.
        char nm[14];
        int k = 0;
        for (; k < 13 && e.ssid[k]; k++) nm[k] = e.ssid[k];
        nm[k] = 0;
        c.text(8, y + 2, nm, pal::BRIGHT);

        // Last two octets of the asker, dimmed when the address is
        // randomised -- it identifies a session, not a device.
        static const char* H = "0123456789ABCDEF";
        char who[8];
        who[0] = H[(e.mac[4] >> 4) & 0xF]; who[1] = H[e.mac[4] & 0xF];
        who[2] = ':';
        who[3] = H[(e.mac[5] >> 4) & 0xF]; who[4] = H[e.mac[5] & 0xF];
        who[5] = 0;
        c.textRight(130, y + 2, who, e.randomised ? pal::EDGE_LIT : pal::ICE);

        ago(buf, e.agoS);
        c.textRight(155, y + 2, buf, pal::DIM);
        if (i) c.hline(5, y, 148, Canvas::mix(pal::PANEL, pal::EDGE, 1, 2));
    }

    drawFooter(c, s);
    drawReticle(c, s);
    c.vignette();
    c.scanlines();

    if (d) {
        State& prev = prevState(SCR_ECHOES);   // PSRAM on the board
        static bool first = true;
        d->clear();
        if (first) { d->add(0, 0, W, H); first = false; }
        else {
            bool moved = s.nEchoes != prev.nEchoes || s.echoTotal != prev.echoTotal;
            for (int i = 0; !moved && i < s.nEchoes; i++)
                moved = s.echoes[i].agoS != prev.echoes[i].agoS;
            if (moved) d->add(0, BODY_Y, W, BODY_H);
            dirtyChrome(*d, s, prev);
        }
        prev = s;
    }
}

} // namespace ui
