// Host test for the scan footer: GPS reads FIXED / NO GPS, and the record
// control reads START / STOP / NO CARD with no elapsed timer. Each label is
// checked by rendering the same string into a scratch canvas and comparing the
// footer's text rows pixel for pixel, so extra or stale glyphs also fail.
#include "../ui/canvas.h"
#include "../ui/layout.h"
#include "../ui/model.h"
#include "../ui/palette.h"
#include "../ui/screens.h"
#include "../ui/font5x7.h"
#include "../ui/nav.h"
#include <cstdio>

using namespace ui;

static int fails = 0;
static void check(const char* what, bool ok) {
    printf(ok ? "  ok   %s\n" : "  FAIL %s\n", what);
    if (!ok) fails++;
}

static Canvas fc, ref;

// True when footer rows [ty, ty+8) over [x0, x1) equal the label drawn alone
// on the panel colour at (tx, ty).
static bool region(const char* label, int tx, uint16_t col, int x0, int x1) {
    const int ty = FOOT_Y + 5;
    ref.clear(pal::PANEL);
    ref.text(tx, ty, label, col);
    for (int y = ty; y < ty + 8; y++)
        for (int x = x0; x < x1; x++)
            if (fc.px[y * W + x] != ref.px[y * W + x]) return false;
    return true;
}

int main() {
    pal::setTheme(pal::TH_UMBRAVOX);
    const int rx = LOG_X, tx = rx + 12, x1 = rx + LOG_W - 2;

    State s;
    s.gpsFix = true; s.sats = 9; s.sdOk = true; s.logState = 0;
    drawFooter(fc, s);
    check("fix reads FIXED", region("FIXED", 11, pal::GREEN, 11, NODE_X));
    check("idle reads START", region("START", tx, pal::WITCH_HI, tx, x1));

    s.gpsFix = false; s.logState = 1;
    drawFooter(fc, s);
    check("no fix reads NO GPS", region("NO GPS", 11, pal::AMBER, 11, NODE_X));
    check("recording reads STOP (no timer)", region("STOP", tx, pal::BRIGHT, tx, x1));

    s.logState = 0; s.sdOk = false;
    drawFooter(fc, s);
    check("no card reads NO CARD", region("NO CARD", tx, pal::RED, tx, x1));

    // ---- the node count, on the scan screen's footer only ----------------
    // Between the GPS label and the record button. On-brand: a mesh glyph in
    // [NODE_X, NODE_X+5) then the digits at NODE_X+7; plain labels: "N" and
    // the digits from NODE_X. DIM at 0, GREEN (the mesh colour) above. The
    // rest of the footer must be pixel-identical to a footer with no count
    // (rendered on a sub-screen, which does not show it), so the count can
    // never draw over FIXED / NO GPS or START / STOP / NO CARD.
    check("node zone sits clear of the widest GPS label",
          11 + Font::width("NO GPS") < NODE_X);
    check("node zone sits clear of the record button", NODE_X + NODE_W <= LOG_X);
    {
        static Canvas bare;
        const uint8_t themes[] = {pal::TH_UMBRAVOX, pal::TH_BAPH0MAU5, pal::TH_COSMICMUFFIN};
        const int counts[] = {0, 1, 9, 10, 99, 150};
        const char* want[] = {"0", "1", "9", "10", "99", "99"};
        for (uint8_t th : themes) {
            pal::setTheme(th);
            for (int plain = 0; plain < 2; plain++)
                for (int k = 0; k < 6; k++)
                    for (int gl = 0; gl < 4; gl++) {   // every GPS x record state
                        State t;
                        t.gpsFix = gl & 1; t.sdOk = true;
                        t.logState = (uint8_t)(gl >> 1);    // 0 START, 1 STOP
                        if (gl == 3) t.logState = 2;        // NO CARD
                        t.set.labels = (uint8_t)plain;
                        t.nNodes = (uint8_t)counts[k];
                        t.screen = SCR_CODEX;
                        drawFooter(bare, t);
                        t.screen = SCR_SCAN;
                        drawFooter(fc, t);
                        const uint16_t col = counts[k] ? pal::GREEN : pal::DIM;
                        const int ty = FOOT_Y + 5;

                        bool outside = true;    // everything off the node zone is untouched
                        for (int y = 0; y < H && outside; y++)
                            for (int x = 0; x < W; x++) {
                                const bool in = x >= NODE_X && x < NODE_X + NODE_W &&
                                                y >= ty && y < ty + 8;
                                if (!in && fc.px[y * W + x] != bare.px[y * W + x]) { outside = false; break; }
                            }
                        bool zoneBare = true;   // ...and on the sub-screen the zone is plain panel
                        for (int y = ty; y < ty + 8; y++)
                            for (int x = NODE_X; x < NODE_X + NODE_W; x++)
                                if (bare.px[y * W + x] != pal::PANEL) zoneBare = false;

                        bool digits, glyph = true;
                        if (plain) {
                            char lab[4] = {'N', 0, 0, 0};
                            for (int i = 0; want[k][i]; i++) lab[1 + i] = want[k][i];
                            digits = region(lab, NODE_X, col, NODE_X, NODE_X + NODE_W);
                        } else {
                            digits = region(want[k], NODE_X + 7, col, NODE_X + 6, NODE_X + NODE_W);
                            int lit = 0;
                            for (int y = ty; y < ty + 8; y++)
                                for (int x = NODE_X; x < NODE_X + 6; x++)
                                    if (fc.px[y * W + x] == col) lit++;
                                    else if (fc.px[y * W + x] != pal::PANEL) glyph = false;
                            if (lit < 8) glyph = false;
                        }
                        if (!(outside && zoneBare && digits && glyph)) {
                            char what[96];
                            snprintf(what, sizeof what, "theme %u %s nodes %d gps/log %d: %s%s%s%s",
                                     th, plain ? "plain" : "brand", counts[k], gl,
                                     outside ? "" : "[overlaps] ", zoneBare ? "" : "[sub-screen shows it] ",
                                     digits ? "" : "[digits] ", glyph ? "" : "[glyph]");
                            check(what, false);
                        }
                    }
        }
        check("node count: 3 themes x 2 label modes x 6 counts x 4 footer states", true);
        pal::setTheme(pal::TH_UMBRAVOX);
    }

    // A node arriving or leaving must repaint the footer, or the glass keeps
    // the old count while the canvas has the new one.
    {
        auto footerDirty = [](const Dirty& d) {
            for (int i = 0; i < d.n; i++)
                if (d.r[i].y <= FOOT_Y + 5 && d.r[i].y + d.r[i].h >= H &&
                    d.r[i].x <= NODE_X && d.r[i].x + d.r[i].w >= NODE_X + NODE_W) return true;
            return false;
        };
        State a, b;
        Dirty d;
        b = a; b.nNodes = 3;
        dirtyChrome(d, b, a);
        check("dirty: 0 -> 3 nodes repaints the footer", footerDirty(d));
        d.clear(); dirtyChrome(d, a, a);
        check("dirty: no change leaves the footer alone", !footerDirty(d));
        d.clear(); b = a; b.set.labels = 1; dirtyChrome(d, b, a);
        check("dirty: label mode change repaints the footer", footerDirty(d));
    }

    printf(fails ? "\n%d FAILED\n" : "\nall passed\n", fails);
    return fails ? 1 : 0;
}
