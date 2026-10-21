// Host tests for the alert-edge contrast rule: a custom theme must not be able
// to hide an alert by setting its alert colours near the background.
#include "../ui/contrast.h"
#include "../ui/palette.h"
#include "../ui/screens.h"
#include "../ui/layout.h"
#include "../ui/themepack.h"
#include "../ui/signatures.h"
#include <stdio.h>
#include <string.h>

static int fails = 0;
static void check(const char* what, bool ok) {
    printf(ok ? "  ok   %s\n" : "  FAIL %s\n", what);
    if (!ok) fails++;
}

using namespace ui;

int main() {
    const uint16_t BLACK = 0x0000, WHITE = 0xFFFF;

    printf("contrastX10:\n");
    check("black on white is 21.0", contrastX10(BLACK, WHITE) == 210);
    check("white on black is 21.0 (order does not matter)", contrastX10(WHITE, BLACK) == 210);
    check("identical colours are 1.0", contrastX10(0x1234, 0x1234) == 10);
    // vs black, ratio = (L + 0.05) / 0.05. 0x5ACB (r5=11,g6=22,b5=11) is 3.011:1
    // -> 30; 0x50BF (r5=10,g6=5,b5=31) is 2.903:1 -> 29. The threshold must
    // separate exactly these two.
    check("0x5ACB vs black is 30", contrastX10(0x5ACB, BLACK) == 30);
    check("0x50BF vs black is 29", contrastX10(0x50BF, BLACK) == 29);
    check("minimum is 3:1", MIN_EDGE_CONTRAST_X10 == 30);

    printf("safeEdge:\n");
    check("a bright colour on dark backgrounds is kept",
          safeEdge(0xF800, SAFE_HIGH, BLACK, 0x0841) == 0xF800);
    check("a colour equal to the background is swapped",
          safeEdge(BLACK, SAFE_HIGH, BLACK, 0x0841) == SAFE_HIGH);
    check("exactly 3:1 against both is kept",
          safeEdge(0x5ACB, SAFE_MED, BLACK, BLACK) == 0x5ACB);
    check("one step below 3:1 is swapped",
          safeEdge(0x50BF, SAFE_MED, BLACK, BLACK) == SAFE_MED);
    check("passing one background but failing the other is swapped",
          safeEdge(0x5ACB, SAFE_LOW, BLACK, 0x5ACB) == SAFE_LOW);
    check("safe colours are the specified RGB565",
          SAFE_HIGH == pal::rgb(0xEF, 0x44, 0x44) && SAFE_MED == pal::rgb(0xF5, 0x9E, 0x0B)
          && SAFE_LOW == pal::rgb(0xD6, 0xC2, 0xFF));
    check("safe colours pass against black",
          contrastX10(SAFE_HIGH, BLACK) >= 30 && contrastX10(SAFE_MED, BLACK) >= 30
          && contrastX10(SAFE_LOW, BLACK) >= 30);

    printf("sanitizeStatus:\n");
    {
        themepack::Pack p; memset(&p, 0, sizeof p);
        p.color[themepack::R_VOID] = BLACK;
        p.color[themepack::R_PANEL] = 0x0841;
        p.color[themepack::R_AMBER] = 0x0841;    // near the background
        p.color[themepack::R_RED] = 0xF800;      // fine
        check("reports a change", themepack::sanitizeStatus(p));
        check("failing amber becomes #F59E0B", p.color[themepack::R_AMBER] == pal::rgb(0xF5, 0x9E, 0x0B));
        check("passing red is untouched", p.color[themepack::R_RED] == 0xF800);
        check("a second pass changes nothing", !themepack::sanitizeStatus(p));
        p.color[themepack::R_RED] = BLACK;
        check("failing red reported", themepack::sanitizeStatus(p));
        check("failing red becomes #EF4444", p.color[themepack::R_RED] == pal::rgb(0xEF, 0x44, 0x44));
        check("void and panel are never touched",
              p.color[themepack::R_VOID] == BLACK && p.color[themepack::R_PANEL] == 0x0841);
        p.color[themepack::R_AMBER] = 0xFFFF; p.color[themepack::R_RED] = 0xF800;
        p.color[themepack::R_PANEL] = 0xFFFF;    // red/amber fail against a white panel
        check("fails if either background is near", themepack::sanitizeStatus(p));
    }

    printf("drawAlert with an all-black palette:\n");
    {
        const int X = COL_R_X, Y = DIAL_Y, Wd = COL_R_W, Hd = DIAL_H;
        const int ages[] = {0, 60, 150, 260, 420, 700, 900, 1500, 2500};
        struct Rite { uint8_t theme, style; const char* name; };
        const Rite rites[] = {
            {pal::TH_UMBRAVOX, 0, "UMBRAVOX ward"},
            {pal::TH_BAPH0MAU5, 0, "BAPH0MAU5 rite"},
            {pal::TH_COSMICMUFFIN, AL_VORTEX, "COSMIC vortex"},
            {pal::TH_COSMICMUFFIN, AL_COMET, "COSMIC comet"},
            {pal::TH_COSMICMUFFIN, AL_LOCKON, "COSMIC lock-on"},
            {pal::TH_COSMICMUFFIN, AL_SUPERNOVA, "COSMIC supernova"},
        };
        const uint8_t confs[] = {CONF_LOW, CONF_MED, CONF_HIGH};
        const uint16_t safe[] = {SAFE_LOW, SAFE_MED, SAFE_HIGH};
        static Canvas c;
        for (const Rite& r : rites)
            for (int ci = 0; ci < 3; ci++) {
                pal::setTheme(r.theme);
                uint16_t* all[] = {&pal::VOID, &pal::PANEL, &pal::GRID, &pal::EDGE, &pal::EDGE_LIT,
                    &pal::DIM, &pal::TEXT, &pal::BRIGHT, &pal::WITCH, &pal::WITCH_HI, &pal::BONE,
                    &pal::SWEEP, &pal::SWEEP_HI, &pal::LIVE, &pal::LIVE_HI, &pal::MAGENTA,
                    &pal::ICE, &pal::GREEN, &pal::AMBER, &pal::RED};
                for (uint16_t* p : all) *p = BLACK;
                for (uint16_t& p : pal::RAMP) p = BLACK;
                State st;
                st.set.theme = r.theme;
                st.set.alertStyle = r.style;
                st.alertActive = true;
                st.alertThreat = TH_DRONE;
                st.alertConf = confs[ci];
                bool anyEveryAge = true, sawSafe = false;
                for (int age : ages) {
                    c.clear(BLACK);
                    st.alertAge = (uint16_t)age;
                    drawAlert(c, st);
                    int lit = 0;
                    for (int y = Y; y < Y + Hd; y++)
                        for (int x = X; x < X + Wd; x++) {
                            const uint16_t v = c.px[y * W + x];
                            if (v != BLACK) lit++;
                            if (v == safe[ci]) sawSafe = true;
                        }
                    if (lit == 0) anyEveryAge = false;
                }
                char buf[96];
                snprintf(buf, sizeof buf, "%s conf %d: something visible at every age", r.name, confs[ci]);
                check(buf, anyEveryAge);
                snprintf(buf, sizeof buf, "%s conf %d: the fixed safe colour is drawn", r.name, confs[ci]);
                check(buf, sawSafe);
            }
        pal::setTheme(pal::TH_UMBRAVOX);
    }

    printf(fails ? "\n%d FAILED\n" : "\nall passed\n", fails);
    return fails ? 1 : 0;
}
