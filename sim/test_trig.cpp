// Host test: no trig call in the scan screen, the sweeps or the alerts may ever
// be handed an argument beyond the FPU-less fast path, however long the device
// has been running. See trig_audit.h for why.
#include "../ui/screens.h"
#include "../ui/demo.h"
#include "../ui/palette.h"
#include "../ui/nav.h"
#include "../ui/signatures.h"
#include "packload.h"
#include <stdio.h>
#include <string.h>
#include <initializer_list>
#undef sinf
#undef cosf

static float g_max = 0;
float audit_maxArg() { return g_max; }
void  audit_reset()  { g_max = 0; }
float audit_sinf(float x) { const float a = fabsf(x); if (a > g_max) g_max = a; return ::sinf(x); }
float audit_cosf(float x) { const float a = fabsf(x); if (a > g_max) g_max = a; return ::cosf(x); }

static int fails = 0;
static void check(const char* w, bool ok) {
    printf(ok ? "  ok   %s\n" : "  FAIL %s\n", w);
    if (!ok) fails++;
}

constexpr float FAST_PATH_RAD = 200.0f;      // newlib's limit is ~201

int main() {
    using namespace ui;
    static Canvas c;
    // The two fixture packs (sim/packs; the test runs from sim/) join the
    // themes audited below: a pack runs the same scan, sweeps and rites.
    for (const char* f : {"packs/umbravox-clone", "packs/cosmic-clone"}) {
        char why[96];
        char w[160];
        snprintf(w, sizeof w, "fixture %s loads", f);
        check(w, packload::load(f, why, sizeof why) >= 0);
    }
    // Frame counters from "a minute in" to "a week of uptime" (frame = ms/16).
    const uint32_t frames[] = {4000u, 60000u, 1000000u, 37800000u};

    for (int theme = 0; theme < pal::themeCount(); theme++) {
        pal::setTheme((uint8_t)theme);
        float worst = 0; int worstSweep = -1;
        for (int sweep = 0; sweep < SW_COUNT; sweep++) {
            if (!(pal::sweepMask((uint8_t)theme) & (uint16_t)(1u << sweep))) continue;
            for (uint32_t f : frames) {
                audit_reset();
                State s = demoState(f);
                s.set.theme = (uint8_t)theme;
                s.set.sweep = (uint8_t)sweep;
                s.frame = f;
                s.alertActive = false;
                drawScan(c, s, nullptr);
                if (audit_maxArg() > worst) { worst = audit_maxArg(); worstSweep = sweep; }
            }
        }
        char buf[96];
        snprintf(buf, sizeof buf, "theme %d scan+sweeps: largest trig argument %.0f rad (sweep %d)",
                 theme, worst, worstSweep);
        check(buf, worst <= FAST_PATH_RAD);
    }

    // The alert rites animate off the alert's age (ms, up to 65 s) and the frame.
    for (int theme = 0; theme < pal::themeCount(); theme++) {
        pal::setTheme((uint8_t)theme);
        float worst = 0; int worstStyle = -1;
        for (int style = 0; style < ALERT_STYLE_COUNT; style++) {
            if (theme != pal::TH_COSMICMUFFIN && style) continue;   // one rite elsewhere
            for (int age : {600, 3000, 20000, 65000}) {
                audit_reset();
                State s = demoState(1000000u);
                s.set.theme = (uint8_t)theme;
                s.set.alertStyle = (uint8_t)style;
                s.frame = 1000000u;
                s.alertActive = true;
                s.alertThreat = TH_DRONE;
                s.alertConf = CONF_HIGH;
                s.alertAge = (uint16_t)age;
                drawScan(c, s, nullptr);
                if (audit_maxArg() > worst) { worst = audit_maxArg(); worstStyle = style; }
            }
        }
        char buf[96];
        snprintf(buf, sizeof buf, "theme %d alerts: largest trig argument %.0f rad (style %d)",
                 theme, worst, worstStyle);
        check(buf, worst <= FAST_PATH_RAD);
    }

    printf(fails ? "\n%d FAILED\n" : "\nall passed\n", fails);
    return fails ? 1 : 0;
}
