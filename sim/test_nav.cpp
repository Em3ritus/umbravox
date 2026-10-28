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
#include "../ui/signatures.h"
#include "../ui/themepack.h"
#include <stdio.h>
#include <string.h>

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
        check("aspect: - from RADAR wraps to the last sweep (AURORA)", st.sweep == SW_AURORA);

        // Theme row steps and wraps; stepping carries the sweep into range.
        st = Settings();
        tapBtn(st, SET_PLUS_X, 0);
        check("aspect: theme + steps to BAPH0MAU5", st.theme == pal::TH_BAPH0MAU5);
        tapBtn(st, SET_PLUS_X, 0);
        check("aspect: theme + steps to COSMIC MUFFIN", st.theme == pal::TH_COSMICMUFFIN);
        tapBtn(st, SET_PLUS_X, 0);
        check("aspect: theme + wraps to UMBRAVOX", st.theme == pal::TH_UMBRAVOX);
        tapBtn(st, SET_MINUS_X, 0);
        check("aspect: theme - wraps to COSMIC MUFFIN", st.theme == pal::TH_COSMICMUFFIN);

        // With packs loaded the theme row steps the built-ins, then the packs
        // in load order, then wraps -- in both directions.
        {
            themepack::Pack p;
            memset(&p, 0, sizeof p);
            p.dial = themepack::DIAL_LARGE;
            p.sweeps = (uint16_t)(1u << SW_STAR);   // no radar: stepping must re-clamp
            p.sweep = SW_STAR;
            for (int i = 0; i < themepack::N_COLORS; i++) p.color[i] = (uint16_t)(0x1111 * i);
            p.color[themepack::R_VOID] = 0;
            p.color[themepack::R_AMBER] = 0xFFE0;
            p.color[themepack::R_RED] = 0xF800;
            snprintf(p.name, sizeof p.name, "PACKA");
            snprintf(p.header, sizeof p.header, "PACKA");
            const int a = pal::addPack(p, nullptr);
            p.sweeps = 0x07FF; p.sweep = SW_RADAR;
            snprintf(p.name, sizeof p.name, "PACKB");
            const int b = pal::addPack(p, nullptr);
            check("aspect: two packs loaded", a == 3 && b == 4 && pal::themeCount() == 5);

            st = Settings();
            const uint8_t fwd[] = { 1, 2, 3, 4, 0 };
            bool order = true;
            for (uint8_t want : fwd) { tapBtn(st, SET_PLUS_X, 0); if (st.theme != want) order = false; }
            check("aspect: theme + visits built-ins, then packs, then wraps", order);
            const uint8_t back[] = { 4, 3, 2, 1, 0 };
            order = true;
            for (uint8_t want : back) { tapBtn(st, SET_MINUS_X, 0); if (st.theme != want) order = false; }
            check("aspect: theme - visits packs, then built-ins, and wraps", order);

            st = Settings();
            st.theme = pal::TH_COSMICMUFFIN; st.sweep = SW_RADAR;
            tapBtn(st, SET_PLUS_X, 0);
            check("aspect: stepping onto a radar-less pack takes its start sweep",
                  st.theme == 3 && st.sweep == SW_STAR);
        }

        // On BAPH0MAU5 the gaze is reachable.
        st.theme = pal::TH_BAPH0MAU5; st.sweep = SW_RADAR; pal::setTheme(pal::TH_BAPH0MAU5);
        tapBtn(st, SET_PLUS_X, 1);
        check("aspect: + from RADAR reaches GAZE on BAPH0MAU5", st.sweep == SW_GAZE);

        // Leaving BAPH0MAU5 while on the gaze must not strand the sweep: the
        // next theme is COSMIC MUFFIN, which also has no eyes, so gaze resets.
        st.theme = pal::TH_BAPH0MAU5; st.sweep = SW_GAZE;
        tapBtn(st, SET_PLUS_X, 0);
        check("aspect: theme change off BAPH0MAU5 resets an unavailable gaze",
              st.theme == pal::TH_COSMICMUFFIN && st.sweep == SW_RADAR);

        // The ALERT row is per-theme: a choice on COSMIC MUFFIN, inert elsewhere.
        st = Settings(); st.theme = pal::TH_COSMICMUFFIN;
        tapBtn(st, SET_PLUS_X, 2);
        check("aspect: alert + on COSMIC MUFFIN steps to VORTEX", st.alertStyle == AL_VORTEX);
        tapBtn(st, SET_MINUS_X, 2); tapBtn(st, SET_MINUS_X, 2);
        check("aspect: alert - wraps to LOCK-ON", st.alertStyle == AL_LOCKON);
        st = Settings(); st.theme = pal::TH_UMBRAVOX;
        tapBtn(st, SET_PLUS_X, 2);
        check("aspect: alert row is inert on UMBRAVOX", st.alertStyle == AL_SUPERNOVA);

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

    printf("cosmic alerts:\n");
    {
        // Each of COSMIC MUFFIN's four alerts stays inside the card, and the
        // style setting actually changes what is drawn.
        pal::setTheme(pal::TH_COSMICMUFFIN);
        State st;
        st.set.theme = pal::TH_COSMICMUFFIN;
        st.alertActive = true;
        st.alertThreat = TH_DRONE;
        st.alertConf   = CONF_HIGH;
        const int X = COL_R_X, Y = DIAL_Y, Wd = COL_R_W, Hd = DIAL_H;
        static Canvas shot[ALERT_STYLE_COUNT];
        const int ages[] = {0, 60, 150, 260, 420, 900, 2500};
        for (int sty = 0; sty < ALERT_STYLE_COUNT; sty++) {
            st.set.alertStyle = (uint8_t)sty;
            bool inside = true;
            for (int age : ages) {
                Canvas c; c.clear(0x1234);            // sentinel off the card
                st.alertAge = (uint16_t)age;
                drawAlert(c, st);
                for (int y = 0; y < H; y++)
                    for (int x = 0; x < W; x++) {
                        const bool onCard = x >= X && x < X + Wd && y >= Y && y < Y + Hd;
                        if (!onCard && c.px[y * W + x] != 0x1234) inside = false;
                    }
            }
            char buf[64];
            snprintf(buf, sizeof buf, "alert style %d stays inside the card", sty);
            check(buf, inside);
            shot[sty].clear(0);
            st.alertAge = 420;
            drawAlert(shot[sty], st);
        }
        bool distinct = true;
        for (int a2 = 0; a2 < ALERT_STYLE_COUNT; a2++)
            for (int b2 = a2 + 1; b2 < ALERT_STYLE_COUNT; b2++)
                if (memcmp(shot[a2].px, shot[b2].px, sizeof shot[a2].px) == 0) distinct = false;
        check("the four alert styles draw four different rites", distinct);
    }

    int heptId[5] = {-1, -1, -1, -1, -1};   // kept for the ASPECT checks below
    printf("pack rites on a heptagram centrepiece:\n");
    {
        // The emblem-centrepiece half of this lives in test_motif; the pack
        // registry holds only MAX_PACKS, so the heptagram half is here.
        const char* rn[5] = {"ward", "supernova", "vortex", "comet", "lockon"};
        static Canvas shot[5];
        const int ages[] = {0, 60, 150, 260, 420, 900, 2500};
        for (int r = 0; r < 5; r++) {
            themepack::Pack p;
            memset(&p, 0, sizeof p);
            snprintf(p.name, sizeof p.name, "HEPT%d", r);
            snprintf(p.header, sizeof p.header, "HEPT%d", r);
            p.dial = themepack::DIAL_COMPACT;
            p.centre = themepack::CENTRE_HEPTAGRAM;
            p.rite = (themepack::Rite)r;
            p.sweeps = 0x07FF; p.sweep = SW_RADAR;
            for (int i = 0; i < themepack::N_COLORS; i++) p.color[i] = (uint16_t)(0x2104 * (i + 1));
            p.color[themepack::R_VOID] = 0;
            p.color[themepack::R_AMBER] = 0xFFE0;
            p.color[themepack::R_RED] = 0xF800;
            const int id = pal::addPack(p, nullptr);
            heptId[r] = id;
            pal::setTheme((uint8_t)id);
            State st;
            st.set.theme = (uint8_t)id;
            st.alertActive = true; st.alertThreat = TH_DRONE; st.alertConf = CONF_HIGH;
            const int X = COL_R_X, Y = DIAL_Y, Wd = COL_R_W, Hd = DIAL_H;
            bool inside = true, drew = false;
            for (int age : ages) {
                // The pre-existing UMBRAVOX ward is not clipped to its card
                // (oversized strike for 90 ms, runes straddling the border by
                // up to 3 px): held to card + 4 px after the strike. The
                // cosmic rites are strict.
                const int slack = r == 0 ? 4 : 0;
                if (r == 0 && age < 90) continue;
                Canvas c; c.clear(0x1234);
                st.alertAge = (uint16_t)age;
                drawAlert(c, st);
                for (int y = 0; y < H; y++)
                    for (int x = 0; x < W; x++) {
                        const bool onCard = x >= X && x < X + Wd && y >= Y && y < Y + Hd;
                        const bool near = x >= X - slack && x < X + Wd + slack &&
                                          y >= Y - slack && y < Y + Hd + slack;
                        if (!near && c.px[y * W + x] != 0x1234) inside = false;
                        if (onCard && c.px[y * W + x] != 0x1234) drew = true;
                    }
            }
            char buf[64];
            snprintf(buf, sizeof buf, "heptagram pack, %s: draws in the card", rn[r]);
            check(buf, drew);
            snprintf(buf, sizeof buf, "heptagram pack, %s: nothing outside it", rn[r]);
            check(buf, inside);
            shot[r].clear(0);
            st.alertAge = 420;
            drawAlert(shot[r], st);
        }
        bool distinct = true;
        for (int a2 = 0; a2 < 5; a2++)
            for (int b2 = a2 + 1; b2 < 5; b2++)
                if (memcmp(shot[a2].px, shot[b2].px, sizeof shot[a2].px) == 0) distinct = false;
        check("the five pack rites draw five different pictures", distinct);
    }

    printf("aspect with theme packs:\n");
    {
        // The skipped-packs line: drawn only when a pack was skipped, only in
        // its own cells, and those cells clear the preview and the rows.
        pal::setTheme(pal::TH_UMBRAVOX);
        State a;
        a.screen = SCR_ASPECT;
        static Canvas c0, cn;
        a.packsSkipped = 0;
        drawAspect(c0, a);
        auto inCell = [](int x, int y) {
            if (y < ASP_SKIP_Y || y >= ASP_SKIP_Y + ASP_SKIP_H) return false;
            return (x >= ASP_SKIP_LX && x < ASP_SKIP_LX + ASP_SKIP_W) ||
                   (x >= ASP_SKIP_RX && x < ASP_SKIP_RX + ASP_SKIP_W);
        };
        const uint8_t counts[] = {2, 9, 10, 99, 100, 255};
        for (uint8_t n : counts) {
            a.packsSkipped = n;
            drawAspect(cn, a);
            int inL = 0, inR = 0, out = 0;
            for (int y = 0; y < H; y++)
                for (int x = 0; x < W; x++) {
                    if (c0.px[y * W + x] == cn.px[y * W + x]) continue;
                    if (!inCell(x, y)) out++;
                    else if (x < W / 2) inL++;
                    else inR++;
                }
            char buf[80];
            snprintf(buf, sizeof buf, "aspect: %u skipped draws in both cells of the skipped line", n);
            check(buf, inL > 0 && inR > 0);
            snprintf(buf, sizeof buf, "aspect: %u skipped draws nothing outside them", n);
            check(buf, out == 0);
        }
        // Geometry: each cell is inside the body, above the first row's
        // divider, and wholly outside the preview's disc.
        auto clearOfDisc = [](int x, int w) {
            const int cx = W / 2, cy = ASP_CY;
            const int nx = cx < x ? x : cx > x + w - 1 ? x + w - 1 : cx;
            const int ny = cy < ASP_SKIP_Y ? ASP_SKIP_Y
                         : cy > ASP_SKIP_Y + ASP_SKIP_H - 1 ? ASP_SKIP_Y + ASP_SKIP_H - 1 : cy;
            return (nx - cx) * (nx - cx) + (ny - cy) * (ny - cy) > ASP_R * ASP_R;
        };
        check("aspect: skipped cells are clear of the preview disc",
              clearOfDisc(ASP_SKIP_LX, ASP_SKIP_W) && clearOfDisc(ASP_SKIP_RX, ASP_SKIP_W));
        check("aspect: skipped line sits inside the body, above the rows",
              ASP_SKIP_Y > BODY_Y && ASP_SKIP_Y + ASP_SKIP_H <= ASP_ROW_Y - 2 &&
              ASP_SKIP_LX >= 2 && ASP_SKIP_RX + ASP_SKIP_W <= 158 &&
              ASP_SKIP_LX + ASP_SKIP_W <= ASP_SKIP_RX);

        // The THEME row names a pack by its own name; a built-in as before.
        char nm[32];
        themeRowName(pal::TH_UMBRAVOX, nm, sizeof nm);
        check("aspect: THEME row names UMBRAVOX", !strcmp(nm, "UMBRAVOX"));
        themeRowName(pal::TH_COSMICMUFFIN, nm, sizeof nm);
        check("aspect: THEME row shortens COSMIC MUFFIN to COSMIC", !strcmp(nm, "COSMIC"));
        const int packA = pal::findPack("PACKA");
        themeRowName((uint8_t)packA, nm, sizeof nm);
        check("aspect: THEME row for a pack id is the pack's name", packA >= 0 && !strcmp(nm, "PACKA"));
        themeRowName((uint8_t)heptId[3], nm, sizeof nm);
        check("aspect: THEME row for another pack is its name", !strcmp(nm, "HEPT3"));

        // A 20-character name is cut to what fits beside the label, and the
        // label is drawn exactly as for a short-named twin.
        themepack::Pack p;
        memset(&p, 0, sizeof p);
        snprintf(p.name, sizeof p.name, "ABCDEFGHIJKLMNOPQRST");
        snprintf(p.header, sizeof p.header, "LONG");
        p.dial = themepack::DIAL_COMPACT;
        p.centre = themepack::CENTRE_HEPTAGRAM;
        p.rite = themepack::RITE_WARD;
        p.sweeps = 0x07FF; p.sweep = SW_RADAR;
        for (int i = 0; i < themepack::N_COLORS; i++) p.color[i] = (uint16_t)(0x2104 * (i + 1));
        p.color[themepack::R_VOID] = 0;
        p.color[themepack::R_AMBER] = 0xFFE0;
        p.color[themepack::R_RED] = 0xF800;
        const int longId = pal::addPack(p, nullptr);   // HEPT0's twin but for the name
        check("aspect: long-named pack registered", longId >= 0);
        themeRowName((uint8_t)longId, nm, sizeof nm);
        check("aspect: a long pack name is cut to ASP_NAME_MAX",
              (int)strlen(nm) == ASP_NAME_MAX && !strncmp(nm, "ABCDEFGHIJKLMNOPQRST", ASP_NAME_MAX));
        themeRowName((uint8_t)longId, nm, 4);
        check("aspect: themeRowName respects a small buffer", !strcmp(nm, "ABC"));
        {
            static Canvas ca, cb;
            State s1; s1.screen = SCR_ASPECT;
            s1.set.theme = (uint8_t)heptId[0]; pal::setTheme(s1.set.theme); drawAspect(ca, s1);
            s1.set.theme = (uint8_t)longId;    pal::setTheme(s1.set.theme); drawAspect(cb, s1);
            bool same = true, differs = false;
            for (int y = ASP_ROW_Y; y < ASP_ROW_Y + ASP_ROW_H; y++)
                for (int x = 0; x < W; x++) {
                    const bool eq = ca.px[y * W + x] == cb.px[y * W + x];
                    if (x < 6 + 6 * 5 + 1 && !eq) same = false;   // "THEME" and a gap
                    if (!eq) differs = true;
                }
            check("aspect: a long pack name leaves the THEME label untouched", same);
            check("aspect: ...and is drawn", differs);
        }

        // The ALERT row names a pack's rite, five rites five names, and
        // its buttons stay inert.
        const char* want[5] = {"WARD", "SUPERNOVA", "VORTEX", "COMET", "LOCK-ON"};
        bool named = true, styleBlind = true;
        for (int r = 0; r < 5; r++) {
            if (strcmp(alertName((uint8_t)heptId[r], AL_SUPERNOVA), want[r])) named = false;
            if (strcmp(alertName((uint8_t)heptId[r], AL_LOCKON), want[r])) styleBlind = false;
        }
        check("aspect: ALERT row shows each pack's rite name", named);
        check("aspect: a pack's ALERT row ignores the cosmic style setting", styleBlind);
        bool distinct = true;
        for (int i = 0; i < 5; i++)
            for (int j = i + 1; j < 5; j++)
                if (!strcmp(alertName((uint8_t)heptId[i], 0), alertName((uint8_t)heptId[j], 0)))
                    distinct = false;
        check("aspect: the five rites show five different names", distinct);
        {
            Settings st; st.theme = (uint8_t)heptId[2];
            Nav n; n.screen = SCR_ASPECT;
            const int x = SET_PLUS_X + SET_BTN_W / 2;
            const int y = ASP_ROW_Y + 2 * ASP_ROW_H + 1 + SET_BTN_H / 2;
            navTouch(n, st, true,  (int16_t)x, (int16_t)y);
            navTouch(n, st, false, (int16_t)x, (int16_t)y);
            check("aspect: ALERT + is inert on a pack", st.alertStyle == AL_SUPERNOVA);
        }

        // Every theme, with the skipped line up, draws only inside the canvas.
        struct Guarded { uint16_t pre[512]; Canvas c; uint16_t post[512]; };
        static Guarded g;
        bool guarded = true;
        for (int t = 0; t < pal::themeCount(); t++) {
            for (int i = 0; i < 512; i++) g.pre[i] = g.post[i] = 0xA5A5;
            State s1; s1.screen = SCR_ASPECT; s1.packsSkipped = 255;
            s1.set.theme = (uint8_t)t; pal::setTheme((uint8_t)t);
            drawAspect(g.c, s1);
            for (int i = 0; i < 512; i++)
                if (g.pre[i] != 0xA5A5 || g.post[i] != 0xA5A5) guarded = false;
        }
        check("aspect: every theme stays inside the canvas", guarded);
        pal::setTheme(pal::TH_UMBRAVOX);
    }

    printf("settings pages:\n");
    {
        auto tap = [](Nav& n, Settings& s, int x, int y) {
            navTouch(n, s, true,  (int16_t)x, (int16_t)y);
            return navTouch(n, s, false, (int16_t)x, (int16_t)y);
        };
        const int lastY = SET_Y + (SET_ROWS - 1) * SET_H;           // the RESET row
        const int pgX = SET_PAGE_X + SET_PAGE_W / 2, pgY = lastY + 1 + SET_BTN_H / 2;
        auto rowY = [](int r) { return SET_Y + r * SET_H + 1 + SET_BTN_H / 2; };

        Nav n; n.screen = SCR_ATTUNE;
        Settings s;
        const Settings before = s;
        check("pages: settings opens on page 1", n.setPage == 0);
        const bool full = tap(n, s, pgX, pgY);
        check("pages: PAGE flips 1 -> 2", n.setPage == 1);
        check("pages: a flip asks for a full repaint", full);
        tap(n, s, pgX, pgY);
        check("pages: PAGE wraps 2 -> 1", n.setPage == 0);
        check("pages: flipping touches no setting and no reset",
              !memcmp(&s, &before, sizeof s) && !n.resetRequested);

        // The hit zone's edges, both axes.
        struct { int x, y; bool flips; const char* what; } edge[] = {
            {SET_PAGE_X - 1,              pgY, false, "pages: x just left of PAGE is inert"},
            {SET_PAGE_X,                  pgY, true,  "pages: PAGE's left column flips"},
            {SET_PAGE_X + SET_PAGE_W - 1, pgY, true,  "pages: PAGE's right column flips"},
            {SET_PAGE_X + SET_PAGE_W,     pgY, false, "pages: x just right of PAGE is inert"},
            {pgX, lastY,                       false, "pages: row above PAGE is inert"},
            {pgX, lastY + 1,                   true,  "pages: PAGE's top row flips"},
            {pgX, lastY + SET_BTN_H,           true,  "pages: PAGE's bottom row flips"},
            {pgX, lastY + 1 + SET_BTN_H,       false, "pages: row below PAGE is inert"},
        };
        for (auto& e : edge) {
            Nav m; m.screen = SCR_ATTUNE; Settings t;
            tap(m, t, e.x, e.y);
            check(e.what, (m.setPage == 1) == e.flips && !m.resetRequested);
        }
        {
            Nav m; m.screen = SCR_ASPECT; Settings t;
            tap(m, t, pgX, pgY);
            check("pages: the same spot on ASPECT is not a pager", m.setPage == 0);
        }

        // Page 2: AUTO LOG toggles on either button; every other knob row inert.
        n.setPage = 1; s = Settings();
        tap(n, s, SET_PLUS_X + 2, rowY(SET_AUTOLOG_ROW));
        check("page 2: AUTO LOG + turns it ON", s.autoLog == 1);
        tap(n, s, SET_PLUS_X + 2, rowY(SET_AUTOLOG_ROW));
        check("page 2: AUTO LOG + again wraps to OFF", s.autoLog == 0);
        tap(n, s, SET_MINUS_X + 2, rowY(SET_AUTOLOG_ROW));
        check("page 2: AUTO LOG - turns it ON", s.autoLog == 1);
        tap(n, s, SET_MINUS_X + 2, rowY(SET_AUTOLOG_ROW));
        check("page 2: AUTO LOG - again wraps to OFF", s.autoLog == 0);
        tap(n, s, 20, rowY(SET_AUTOLOG_ROW));
        tap(n, s, 70, rowY(SET_AUTOLOG_ROW));
        check("page 2: AUTO LOG label and value are inert", s.autoLog == 0);
        {
            const Settings ref = s;
            for (int r = 0; r < SET_ROWS - 1; r++) {
                // FILES' OPEN opens a screen (sim/test_files.cpp covers it).
                if (r == SET_AUTOLOG_ROW || r == SET_FILES_ROW) continue;
                tap(n, s, SET_PLUS_X + 2, rowY(r));     // one-sided: a - then + would cancel
                tap(n, s, SET_PLUS_X + 2, rowY(r));
                tap(n, s, SET_MINUS_X + 2, rowY(r));
            }
            check("page 2: the other knob rows are inert",
                  !memcmp(&s, &ref, sizeof s) && n.setPage == 1);
        }
        {
            Nav m; m.screen = SCR_ATTUNE; m.setPage = 1; Settings t;
            tap(m, t, SET_MINUS_X + 20, lastY + 1 + SET_BTN_H / 2);
            check("page 2: CLEAR still requests the reset", m.resetRequested);
        }
        // Page 1: row 0 is still ALERT, and nothing on page 1 reaches AUTO LOG.
        {
            Nav m; m.screen = SCR_ATTUNE; Settings t;
            const uint8_t a0 = t.alertMin;
            tap(m, t, SET_PLUS_X + 2, rowY(0));
            check("page 1: row 0 + still steps ALERT", t.alertMin == a0 + 1 && t.autoLog == 0);
            for (int r = 0; r < SET_ROWS - 1; r++)
                for (int k = 0; k < 3; k++) {
                    tap(m, t, SET_MINUS_X + 2, rowY(r));
                    tap(m, t, SET_PLUS_X + 2, rowY(r));
                    tap(m, t, SET_PLUS_X + 2, rowY(r));
                }
            check("page 1: no knob reaches AUTO LOG", t.autoLog == 0);
        }
        // Leaving the screen puts it back on page 1 for the next visit.
        n.setPage = 1;
        tap(n, s, 4, 4);
        check("pages: back leaves settings and resets to page 1",
              n.screen == SCR_SCAN && n.setPage == 0);
    }

    printf("settings pages render:\n");
    {
        pal::setTheme(pal::TH_UMBRAVOX);
        static Canvas p1, p2, p2on;
        State st; st.screen = SCR_ATTUNE;
        drawAttune(p1, st);
        st.setPage = 1; drawAttune(p2, st);
        st.set.autoLog = 1; drawAttune(p2on, st);
        const int lastY = SET_Y + (SET_ROWS - 1) * SET_H;
        bool lastRowSameButPager = true, page2Differs = false;
        for (int y = 0; y < H; y++)
            for (int x = 0; x < W; x++) {
                const bool diff = p1.px[y * W + x] != p2.px[y * W + x];
                if (y >= SET_Y - 2 && y < lastY) { if (diff) page2Differs = true; continue; }   // knob rows (row 5's buttons reach y 84)
                const bool inPager = x >= SET_PAGE_X && x < SET_PAGE_X + SET_PAGE_W &&
                                     y > lastY && y <= lastY + SET_BTN_H;
                if (diff && !inPager) lastRowSameButPager = false;
            }
        check("render: page 2 has different knob rows", page2Differs);
        check("render: header, RESET row and footer match except the pager", lastRowSameButPager);
        bool onOffOnlyInRow = true, onOffDiffers = false;
        for (int y = 0; y < H; y++)
            for (int x = 0; x < W; x++)
                if (p2.px[y * W + x] != p2on.px[y * W + x]) {
                    onOffDiffers = true;
                    if (y < SET_Y || y >= SET_Y + SET_H || x >= SET_MINUS_X) onOffOnlyInRow = false;
                }
        check("render: AUTO LOG ON/OFF changes only its value text", onOffDiffers && onOffOnlyInRow);

        // The body is dirtied by a page flip and by the setting.
        Dirty d;
        State a; a.screen = SCR_ATTUNE;
        drawAttune(p1, a, &d);                  // first frame primes prev
        State b = a; b.setPage = 1;
        drawAttune(p1, b, &d);
        bool body = false;
        for (int i = 0; i < d.n; i++) if (d.r[i].y <= SET_Y && d.r[i].h >= BODY_H) body = true;
        check("dirty: a page flip repaints the body", body);
        State c2 = b; c2.set.autoLog = 1;
        drawAttune(p1, c2, &d);
        body = false;
        for (int i = 0; i < d.n; i++) if (d.r[i].y <= SET_Y && d.r[i].h >= BODY_H) body = true;
        check("dirty: AUTO LOG change repaints the body", body);
    }

    printf(fails ? "\n%d FAILED\n" : "\nall passed\n", fails);
    return fails ? 1 : 0;
}
