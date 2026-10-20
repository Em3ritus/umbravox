// Host tests for tap routing on the scan screen under each theme.
//
// The dial and spectrum geometry is per-theme and runtime, and nav.cpp reads
// it by the same names the drawing code does. A theme with no spectrum
// (SPEC_H == 0) must not route a tap in the old strip to MESH.
#include "../ui/nav.h"
#include "../ui/layout.h"
#include "../ui/palette.h"
#include "../ui/sweep.h"
#include "../ui/screens.h"
#include <stdio.h>

static int fails = 0;

static void check(const char* what, bool ok) {
    printf(ok ? "  ok   %s\n" : "  FAIL %s\n", what);
    if (!ok) fails++;
}

using namespace ui;

// A tap is a press then a release; the release carries the last position seen.
static Nav tapAt(int x, int y) {
    Nav n;
    Settings s;
    navTouch(n, s, true,  (int16_t)x, (int16_t)y);
    navTouch(n, s, false, (int16_t)x, (int16_t)y);
    return n;
}

int main() {
    const int specX = COL_R_X + COL_R_W / 2;

    pal::setTheme(pal::TH_UMBRAVOX);
    check("theme 0 has a spectrum strip", SPEC_H > 0);
    {
        Nav n = tapAt(specX, SPEC_Y + SPEC_H / 2);
        check("theme 0: spectrum tap opens MESH", n.screen == SCR_MESH);
    }
    {
        Nav n = tapAt(DIAL_CX, DIAL_CY);
        check("theme 0: dial tap opens DOMAIN", n.screen == SCR_DOMAIN);
    }
    const int oldSpecY = SPEC_Y + SPEC_H / 2;   // theme 0's strip, for theme 1

    pal::setTheme(pal::TH_BAPH0MAU5);
    check("theme 1 has no spectrum strip", SPEC_H == 0);
    {
        Nav n = tapAt(specX, oldSpecY);
        check("theme 1: old spectrum coordinates do not open MESH", n.screen != SCR_MESH);
    }
    {
        Nav n = tapAt(DIAL_CX, DIAL_CY);
        check("theme 1: dial tap opens DOMAIN", n.screen == SCR_DOMAIN);
    }

    const uint8_t want[3] = {F_WIFI, F_5G, F_BLE};
    for (int t = 0; t < 2; t++) {
        pal::setTheme((uint8_t)t);
        for (int i = 0; i < 3; i++) {
            Nav n = tapAt(COL_L_X + COL_L_W / 2, TILE_Y[i] + TILE_H / 2);
            char buf[64];
            snprintf(buf, sizeof buf, "theme %d: tile %d opens CODEX with its filter", t, i);
            check(buf, n.screen == SCR_CODEX && n.filter == want[i]);
        }
    }

    // The pulse must find a contact exactly when the drawn ring is at that
    // contact's radius, at ANY dial radius -- not only the active theme's.
    // Byte-identical renders cannot catch this: on the scan screen the two
    // radii are equal. 14 is neither theme's R_FIELD.
    {
        const int rf = 14;
        bool differs = true, agree = true;
        for (int t = 0; t < 2; t++) {
            pal::setTheme((uint8_t)t);
            if (R_FIELD == rf) differs = false;
        }
        check("test radius differs from both themes' R_FIELD", differs);
        for (uint32_t f = 0; f < 200 && agree; f++)
            for (int dist = 0; dist <= 100; dist++) {
                const int head = pulseRing(f, rf);
                const int dp   = contactRadius(dist, rf);
                const bool want = (head - dp) >= 0 && (head - dp) < 5;
                if (searchTouches(SW_PULSE, f, 0, dist, rf) != want) { agree = false; break; }
            }
        check("pulse detects at the drawn ring radius for rField 14", agree);
    }

    // ASPECT: tap the real buttons and watch the settings step. The sweep
    // control must SKIP an unavailable id, not step onto it and be clamped
    // back -- a control that bounces reads as broken.
    {
        auto tapBtn = [](Settings& st, int bx, int row) {
            Nav n; n.screen = SCR_ASPECT;
            const int x = bx + SET_BTN_W / 2;
            const int y = ASP_ROW_Y + row * ASP_ROW_H + 1 + SET_BTN_H / 2;
            navTouch(n, st, true,  (int16_t)x, (int16_t)y);
            navTouch(n, st, false, (int16_t)x, (int16_t)y);
        };
        pal::setTheme(pal::TH_UMBRAVOX);
        Settings st;
        check("aspect: defaults are UMBRAVOX / RADAR",
              st.theme == pal::TH_UMBRAVOX && st.sweep == SW_RADAR);
        check("aspect: UMBRAVOX offers no gaze", !(pal::sweepMask(pal::TH_UMBRAVOX) & (1 << SW_GAZE)));

        // Walk the sweep forward and back on UMBRAVOX; never land on gaze,
        // and every press must actually change the value.
        bool skips = true, moves = true;
        for (int dir = 0; dir < 2; dir++) {
            st.sweep = SW_RADAR;
            for (int i = 0; i < 2 * SW_COUNT; i++) {
                const uint8_t before = st.sweep;
                tapBtn(st, dir ? SET_MINUS_X : SET_PLUS_X, 1);
                if (st.sweep == before) moves = false;
                if (st.sweep == SW_GAZE) skips = false;
            }
        }
        check("aspect: every sweep tap changes the value on UMBRAVOX", moves);
        check("aspect: sweep never lands on the gaze on UMBRAVOX", skips);
        st.sweep = SW_RADAR; tapBtn(st, SET_PLUS_X, 1);
        check("aspect: + from RADAR steps to PULSE, skipping GAZE", st.sweep == SW_PULSE);
        tapBtn(st, SET_MINUS_X, 1);
        check("aspect: - from PULSE returns to RADAR, skipping GAZE", st.sweep == SW_RADAR);
        tapBtn(st, SET_MINUS_X, 1);
        check("aspect: - from RADAR wraps to CANDLE", st.sweep == SW_CANDLE);

        // Theme row steps and wraps; stepping carries the sweep into range.
        st = Settings();
        tapBtn(st, SET_PLUS_X, 0);
        check("aspect: theme + steps to BAPH0MAU5", st.theme == pal::TH_BAPH0MAU5);
        tapBtn(st, SET_PLUS_X, 0);
        check("aspect: theme + wraps to UMBRAVOX", st.theme == pal::TH_UMBRAVOX);
        tapBtn(st, SET_MINUS_X, 0);
        check("aspect: theme - wraps to BAPH0MAU5", st.theme == pal::TH_BAPH0MAU5);

        // On BAPH0MAU5 the gaze is reachable.
        st.sweep = SW_RADAR; pal::setTheme(pal::TH_BAPH0MAU5);
        tapBtn(st, SET_PLUS_X, 1);
        check("aspect: + from RADAR reaches GAZE on BAPH0MAU5", st.sweep == SW_GAZE);

        // Leaving BAPH0MAU5 while on the gaze must not strand the sweep.
        st.theme = pal::TH_BAPH0MAU5; st.sweep = SW_GAZE;
        tapBtn(st, SET_PLUS_X, 0);
        check("aspect: theme change off BAPH0MAU5 resets an unavailable gaze",
              st.theme == pal::TH_UMBRAVOX && st.sweep == SW_RADAR);

        // The tile opens the screen.
        Nav g; g.screen = SCR_GRIMOIRE; Settings gs;
        int ax, ay, aw, ah; grimoireCell(GRIMOIRE_N - 1, ax, ay, aw, ah);
        navTouch(g, gs, true,  (int16_t)(ax + aw / 2), (int16_t)(ay + ah / 2));
        navTouch(g, gs, false, (int16_t)(ax + aw / 2), (int16_t)(ay + ah / 2));
        check("grimoire: eighth tile opens ASPECT", g.screen == SCR_ASPECT);
    }

    // The footer's SD status is the log control: a tap there raises the flag
    // and stays on the scan screen; taps elsewhere do not.
    {
        pal::setTheme(pal::TH_UMBRAVOX);
        Nav n = tapAt(LOG_X + LOG_W / 2, LOG_Y + LOG_H / 2);
        check("log: tap on the control sets logToggleRequested", n.logToggleRequested);
        check("log: tap on the control stays on SCAN", n.screen == SCR_SCAN);
        check("log: control does not set resetRequested", !n.resetRequested);

        Nav a = tapAt(LOG_X, LOG_Y);
        Nav b = tapAt(LOG_X + LOG_W - 1, LOG_Y + LOG_H - 1);
        check("log: region corners are inside the control",
              a.logToggleRequested && b.logToggleRequested);

        check("log: tap on the GPS side of the footer does not toggle",
              !tapAt(LOG_X - 4, LOG_Y + LOG_H / 2).logToggleRequested);
        check("log: tap on the clock side of the footer does not toggle",
              !tapAt(LOG_X + LOG_W + 2, LOG_Y + LOG_H / 2).logToggleRequested);
        check("log: tap on the tiles does not toggle",
              !tapAt(COL_L_X + COL_L_W / 2, TILE_Y[0] + TILE_H / 2).logToggleRequested);
        check("log: tap above the footer does not toggle",
              !tapAt(LOG_X + LOG_W / 2, LOG_Y - 3).logToggleRequested);

        Nav o; o.screen = SCR_OMENS; Settings os;
        navTouch(o, os, true,  (int16_t)(LOG_X + 5), (int16_t)(LOG_Y + 5));
        navTouch(o, os, false, (int16_t)(LOG_X + 5), (int16_t)(LOG_Y + 5));
        check("log: the same spot on another screen does not toggle", !o.logToggleRequested);
    }

    printf(fails ? "\n%d FAILED\n" : "\nall passed\n", fails);
    return fails ? 1 : 0;
}
