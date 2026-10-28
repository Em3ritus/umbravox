// Host tests for the idle screen mode: DIM / OFF / ANIM. Pure decisions in
// device/idlemode.h (no Arduino), plus the animation's pixels against its
// dirty rect, so "blit only the changed region" is proven rather than hoped.
#include "../device/idlemode.h"
#include "../ui/nav.h"
#include "../ui/canvas.h"
#include "../ui/layout.h"
#include "../ui/screens.h"
#include "../ui/palette.h"
#include <stdio.h>
#include <string.h>

static int fails = 0;
static void check(const char* what, bool ok) {
    printf(ok ? "  ok   %s\n" : "  FAIL %s\n", what);
    if (!ok) fails++;
}

using namespace idlemode;

int main() {
    const uint8_t BR = 204, DIMP = 25;   // 80% bright; dim = bright/8

    printf("mode values and clamping:\n");
    check("default mode is DIM (0)", ui::Settings().idleMode == ui::IDLE_DIM);
    check("plan treats an unknown mode as DIM",
          plan(9, true, DIMP, BR, DIMP).pwm == DIMP && !plan(9, true, DIMP, BR, DIMP).anim);
    {
        ui::Settings s; s.idleMode = 77;
        ui::clampSettings(s);
        check("clampSettings pulls idleMode to DIM", s.idleMode == ui::IDLE_DIM);
        s.idleMode = ui::IDLE_ANIM; ui::clampSettings(s);
        check("clampSettings keeps a valid idleMode", s.idleMode == ui::IDLE_ANIM);
        ui::Settings a, b; b.idleMode = ui::IDLE_OFF;
        check("settingsEqual sees idleMode", !ui::settingsEqual(a, b));
    }

    printf("DIM is today's behaviour:\n");
    {
        Plan p = plan(ui::IDLE_DIM, false, DIMP, BR, DIMP);   // not dimmed
        check("awake: bright, drawing", p.pwm == BR && !p.suspend && !p.anim);
        p = plan(ui::IDLE_DIM, true, BR, BR, DIMP);               // dimmed, still ramping
        check("dimmed ramping: target dim, still drawing", p.pwm == DIMP && !p.suspend);
        p = plan(ui::IDLE_DIM, true, DIMP, BR, DIMP);
        check("dimmed settled: suspend, no anim", p.pwm == DIMP && p.suspend && !p.anim);
    }

    printf("OFF:\n");
    {
        Plan p = plan(ui::IDLE_OFF, true, BR, BR, DIMP);
        check("ramping: target 0, still drawing", p.pwm == 0 && !p.suspend);
        p = plan(ui::IDLE_OFF, true, DIMP, BR, DIMP);
        check("at the old dim level it is not finished yet", p.pwm == 0 && !p.suspend);
        p = plan(ui::IDLE_OFF, true, 0, BR, DIMP);
        check("settled at 0: suspend, no anim", p.pwm == 0 && p.suspend && !p.anim);
    }

    printf("ANIM:\n");
    {
        Plan p = plan(ui::IDLE_ANIM, true, BR, BR, DIMP);
        check("ramping: target dim, normal drawing continues", p.pwm == DIMP && !p.suspend && !p.anim);
        p = plan(ui::IDLE_ANIM, true, DIMP, BR, DIMP);
        check("settled: normal UI suspended, animation on", p.pwm == DIMP && p.suspend && p.anim);
    }

    printf("wake and enable:\n");
    for (uint8_t m = 0; m < ui::IDLE_MODE_N; m++) {
        Plan p = plan(m, false, 0, BR, DIMP);   // touch cleared dimmed while pwm is still low
        char l[64]; snprintf(l, sizeof l, "mode %u: woken -> bright target, drawing, no anim", m);
        check(l, p.pwm == BR && !p.suspend && !p.anim);
    }
    {
        // idle timeout disabled: the mode must not matter
        for (uint8_t m = 0; m < ui::IDLE_MODE_N; m++) {
            Plan p = plan(m, true, BR, BR, DIMP, /*idleEnabled=*/false);
            char l[64]; snprintf(l, sizeof l, "mode %u with timeout NEVER: never dims or suspends", m);
            check(l, p.pwm == BR && !p.suspend && !p.anim);
        }
    }

    printf("animation timing and pose:\n");
    check("<= 2 fps: frame period is at least 500 ms", ANIM_FRAME_MS >= 500);
    check("frame 0 at 0 ms", animFrame(0) == 0);
    check("same frame inside a period", animFrame(ANIM_FRAME_MS - 1) == 0);
    check("next frame after a period", animFrame(ANIM_FRAME_MS) == 1);
    check("frame advances once a period for a minute",
          animFrame(60000) == 60000 / ANIM_FRAME_MS);
    {
        bool allIn = true, sawZero = false, sawMax = false, moved = false;
        Pose first = pose(0);
        for (uint32_t f = 0; f < 200; f++) {
            Pose p = pose(f);
            if (p.r > MAX_R) allIn = false;
            if (p.r == 0) sawZero = true;
            if (p.r == MAX_R) sawMax = true;
            if (p.cx != first.cx || p.cy != first.cy) moved = true;
            // the whole box must sit on the canvas
            if (p.cx - BOX_HALF < 0 || p.cy - BOX_HALF < 0 ||
                p.cx + BOX_HALF >= ui::W || p.cy + BOX_HALF >= ui::H) allIn = false;
            // position only changes while nothing is drawn
            if (f > 0) {
                Pose q = pose(f - 1);
                if ((q.cx != p.cx || q.cy != p.cy) && q.r != 0) allIn = false;
            }
        }
        check("radius bounded, box on canvas, moves only while blank", allIn);
        check("it breathes: blank and full radius both occur", sawZero && sawMax);
        check("it drifts to another spot over time (no static image)", moved);
    }

    printf("dirty rect covers every changed pixel:\n");
    {
        static ui::Canvas c;
        const uint16_t BG = 0x0000, FG = 0xFFFF;
        c.clear(BG);
        bool ok = true, rectSmall = true;
        int maxPx = 0;
        uint32_t f = 0, prev = NO_FRAME;
        for (int i = 0; i < 80; i++) {
            static uint16_t before[ui::W * ui::H];
            memcpy(before, c.px, sizeof before);
            Dirty d = drawFrame(c, f, prev, BG, FG);
            for (int y = 0; y < ui::H; y++)
                for (int x = 0; x < ui::W; x++) {
                    bool in = false;
                    for (int k = 0; k < d.n; k++)
                        in |= x >= d.r[k].x && x < d.r[k].x + d.r[k].w && y >= d.r[k].y && y < d.r[k].y + d.r[k].h;
                    if (before[y * ui::W + x] != c.px[y * ui::W + x] && !in) ok = false;
                }
            for (int k = 0; k < d.n; k++) {
                if (d.r[k].w * d.r[k].h > maxPx) maxPx = d.r[k].w * d.r[k].h;
                if (d.r[k].w * d.r[k].h > 200) rectSmall = false;
            }
            // Only the current pose may be lit: stalls (skipped frames) leave no residue.
            const Pose pp = pose(f);
            for (int y = 0; y < ui::H; y++)
                for (int x = 0; x < ui::W; x++)
                    if (c.px[y * ui::W + x] != BG &&
                        (x < pp.cx - MAX_R || x > pp.cx + MAX_R || y < pp.cy - MAX_R || y > pp.cy + MAX_R))
                        ok = false;
            prev = f;
            f += 1 + (i % 5 == 3 ? 9 : i % 3);   // consecutive, small gaps and big jumps
        }
        check("no pixel changes outside the reported rect", ok);
        check("rect is tiny (<= 200 px of 19200)", rectSmall);
        printf("       largest rect %d px = %.2f%% of the screen\n", maxPx, 100.0 * maxPx / (ui::W * ui::H));
        // A blank frame leaves a clean canvas
        c.clear(BG);
        drawFrame(c, 7, NO_FRAME, BG, FG);
        int lit = 0;
        for (int i = 0; i < ui::W * ui::H; i++) lit += c.px[i] != BG;
        check("the blank frame (7) draws nothing", lit == 0);
        drawFrame(c, 3, NO_FRAME, BG, FG);   // radius MAX_R
        lit = 0;
        for (int i = 0; i < ui::W * ui::H; i++) lit += c.px[i] != BG;
        check("peak frame draws something small", lit > 0 && lit < 120);
    }

    printf("animation enter/exit sequence:\n");
    {
        AnimState as;
        uint32_t now = 1000;
        Step s = animStep(as, false, now);
        check("awake: nothing happens", !s.enter && !s.exit && !s.draw && !as.active);
        s = animStep(as, true, now);
        check("idle #1 enter: clear + full blit + first frame", s.enter && !s.exit && s.draw && s.frame == 0);
        now += 100; s = animStep(as, true, now);
        check("same frame is not redrawn", !s.enter && !s.draw);
        now += 450; s = animStep(as, true, now);
        check("next frame drawn after 500 ms, no re-enter", !s.enter && s.draw && s.frame == 1);
        s = animStep(as, false, now);
        check("wake #1: exit forces a full repaint", s.exit && !s.enter && !s.draw && !as.active);
        s = animStep(as, false, now + 10);
        check("exit is reported once", !s.exit);
        now += 20000;
        s = animStep(as, true, now);
        check("idle #2 enter again clears + full blits", s.enter && s.draw && s.frame == 0);
        now += 500; s = animStep(as, true, now);
        check("idle #2 animates", !s.enter && s.draw && s.frame == 1);
        s = animStep(as, false, now + 1);
        check("wake #2: exit forces a full repaint", s.exit);
        // mode switched away from ANIM while dark: animOn goes false
        s = animStep(as, true, now + 5000);
        check("idle #3 enter", s.enter);
        s = animStep(as, false, now + 5100);
        check("mode changed to DIM/OFF while dark: exit + repaint", s.exit && !as.active);
        // timeout changed to NEVER while dark (plan() gives anim=false)
        s = animStep(as, true, now + 9000);
        check("idle #4 enter", s.enter);
        Plan p = plan(ui::IDLE_ANIM, true, DIMP, BR, DIMP, /*idleEnabled=*/false);
        s = animStep(as, p.anim, now + 9100);
        check("timeout set to NEVER while animating: exit + repaint", s.exit && !as.active);
    }

    printf("combined idle position (NEVER, then 15S..5M x DIM/OFF/ANIM):\n");
    {
        using namespace ui;
        check("16 states", IDLE_COMBINED_N == 16);
        // forward cycle visits all 16 once and wraps
        Settings s; s.idleIdx = 0; s.idleMode = IDLE_DIM;
        bool seen[16] = {}; int dupes = 0; bool order = true;
        for (int i = 0; i < 16; i++) {
            const int c = idleCombinedIndex(s.idleIdx, s.idleMode);
            if (c != i) order = false;
            if (seen[c]) dupes++;
            seen[c] = true;
            idleStep(s, +1);
        }
        check("forward: visits all 16 in order, once each", order && !dupes);
        check("forward: wraps back to NEVER", s.idleIdx == 0);
        bool back = true;
        for (int i = 0; i < 16; i++) {
            const int before = idleCombinedIndex(s.idleIdx, s.idleMode);
            idleStep(s, -1);
            if (idleCombinedIndex(s.idleIdx, s.idleMode) != (before + 15) % 16) back = false;
        }
        check("backward: full cycle, each step one back", back && s.idleIdx == 0);
        // spec'd edges
        Settings e; e.idleIdx = 0; e.idleMode = IDLE_ANIM;
        idleStep(e, +1);
        check("NEVER + -> 15S DIM (regardless of stored mode)", e.idleIdx == 1 && e.idleMode == IDLE_DIM);
        e.idleIdx = 0; e.idleMode = IDLE_DIM;
        idleStep(e, -1);
        check("NEVER - -> 5M ANIM", e.idleIdx == 5 && e.idleMode == IDLE_ANIM);
        e.idleIdx = 1; e.idleMode = IDLE_DIM; idleStep(e, -1);
        check("15S DIM - -> NEVER", e.idleIdx == 0);
        e.idleIdx = 5; e.idleMode = IDLE_ANIM; idleStep(e, +1);
        check("5M ANIM + -> NEVER", e.idleIdx == 0);
        check("reaching NEVER keeps the previous mode stored", e.idleMode == IDLE_ANIM);
        // every saved state, old and new, steps consistently
        bool consistent = true;
        for (int i = 0; i < IDLE_N; i++)
            for (int m = 0; m < IDLE_MODE_N; m++) {
                Settings x; x.idleIdx = (uint8_t)i; x.idleMode = (uint8_t)m;
                const int c = idleCombinedIndex(x.idleIdx, x.idleMode);
                Settings f = x, b = x;
                idleStep(f, +1); idleStep(b, -1);
                if (idleCombinedIndex(f.idleIdx, f.idleMode) != (c + 1) % 16) consistent = false;
                if (idleCombinedIndex(b.idleIdx, b.idleMode) != (c + 15) % 16) consistent = false;
                if (i == 0 && c != 0) consistent = false;   // NEVER is position 0 whatever the mode
            }
        check("every saved (idleIdx, idleMode) steps +1/-1 consistently", consistent);
        // old stores: mode 0 -> DIM positions 1..5
        check("old saved 30S (mode 0) is 30S DIM", idleCombinedIndex(2, 0) == 2);
        // garbage is clamped like clampSettings does
        Settings g; g.idleIdx = 9; g.idleMode = 77;
        Settings h = g; clampSettings(h);
        check("garbage idleMode reads as DIM, bad idleIdx as the clamp's 30S",
              idleCombinedIndex(g.idleIdx, g.idleMode) == idleCombinedIndex(h.idleIdx, h.idleMode) &&
              idleCombinedIndex(g.idleIdx, g.idleMode) == 2);
        idleStep(g, +1);
        check("stepping from garbage lands on a valid state", g.idleIdx < IDLE_N && g.idleMode < IDLE_MODE_N);
    }

    printf("settings screen: the IDLE row carries the mode:\n");
    {
        auto tap = [](ui::Nav& n, ui::Settings& s, int x, int y) {
            ui::navTouch(n, s, true, (int16_t)x, (int16_t)y);
            ui::navTouch(n, s, false, (int16_t)x, (int16_t)y);
        };
        const int rowY = ui::SET_Y + ui::SET_IDLE_ROW * ui::SET_H + ui::SET_H / 2;
        const int valX = 70;     // the value text: no longer a control
        ui::Nav n; n.screen = ui::SCR_ATTUNE;
        ui::Settings s;                       // idleIdx 2 = 30 s, DIM
        tap(n, s, ui::SET_PLUS_X + 2, rowY);
        check("+ : 30S DIM -> 1M DIM", s.idleIdx == 3 && s.idleMode == ui::IDLE_DIM);
        tap(n, s, ui::SET_MINUS_X + 2, rowY);
        check("- : back to 30S DIM", s.idleIdx == 2 && s.idleMode == ui::IDLE_DIM);
        s.idleIdx = 5; s.idleMode = ui::IDLE_DIM;
        tap(n, s, ui::SET_PLUS_X + 2, rowY);
        check("+ from 5M DIM rolls on to 15S OFF", s.idleIdx == 1 && s.idleMode == ui::IDLE_OFF);
        tap(n, s, ui::SET_MINUS_X + 2, rowY);
        check("- from 15S OFF returns to 5M DIM", s.idleIdx == 5 && s.idleMode == ui::IDLE_DIM);
        s.idleMode = ui::IDLE_MODE_N - 1; s.idleIdx = 2;
        tap(n, s, valX, rowY);
        check("tapping the value text is inert", s.idleIdx == 2 && s.idleMode == ui::IDLE_ANIM);
        tap(n, s, 20, rowY);
        check("tapping the label is inert", s.idleIdx == 2 && s.idleMode == ui::IDLE_ANIM);
        tap(n, s, ui::SET_PLUS_X + 2, ui::SET_Y + 4 * ui::SET_H + 4);
        check("another row's + does not touch idle", s.idleIdx == 2 && s.idleMode == ui::IDLE_ANIM);

        // render: default shows DIM, the other two differ from it and each other
        pal::setTheme(pal::TH_UMBRAVOX);
        static ui::Canvas a, b, d;
        ui::State st;
        st.screen = ui::SCR_ATTUNE;
        ui::drawAttune(a, st, nullptr);
        st.set.idleMode = ui::IDLE_OFF;  ui::drawAttune(b, st, nullptr);
        st.set.idleMode = ui::IDLE_ANIM; ui::drawAttune(d, st, nullptr);
        check("render differs per mode",
              memcmp(a.px, b.px, sizeof a.px) && memcmp(b.px, d.px, sizeof b.px) &&
              memcmp(a.px, d.px, sizeof a.px));
        // ...and only inside the IDLE row
        bool inRow = true;
        for (int y = 0; y < ui::H; y++)
            for (int x = 0; x < ui::W; x++)
                if ((a.px[y * ui::W + x] != d.px[y * ui::W + x]) &&
                    !(y >= ui::SET_Y + ui::SET_IDLE_ROW * ui::SET_H &&
                      y < ui::SET_Y + (ui::SET_IDLE_ROW + 1) * ui::SET_H))
                    inRow = false;
        check("the change is confined to the IDLE row", inRow);
        // The longest value ("5M ANIM") must stay inside the value zone and
        // clear of the minus button. Pixels that differ from the NEVER render
        // (shorter, same right edge) are the text's extent.
        ui::State sx; sx.set.idleIdx = 5; sx.set.idleMode = ui::IDLE_ANIM;
        ui::State sn; sn.set.idleIdx = 0;
        static ui::Canvas e, f0;
        ui::drawAttune(e, sx, nullptr); ui::drawAttune(f0, sn, nullptr);
        const int y0 = ui::SET_Y + ui::SET_IDLE_ROW * ui::SET_H, y1 = y0 + ui::SET_H;
        const int ZX = 44, ZW = 66;   // the space between the label and the buttons
        int minX = 999, maxX = -1; bool touchesMinus = false;
        for (int y = y0; y < y1; y++)
            for (int x = 0; x < ui::W; x++)
                if (e.px[y * ui::W + x] != f0.px[y * ui::W + x]) {
                    if (x < minX) minX = x;
                    if (x > maxX) maxX = x;
                    if (x >= ui::SET_MINUS_X - 1) touchesMinus = true;
                }
        printf("       value text spans x %d..%d (zone %d..%d, minus at %d)\n",
               minX, maxX, ZX, ZX + ZW - 1, ui::SET_MINUS_X);
        check("longest value renders inside the value zone",
              maxX >= 0 && minX >= ZX && maxX < ZX + ZW);
        check("longest value does not touch the minus button", !touchesMinus);
    }

    printf("relative cost:\n");
    {
        // normal frame ~13 ms at ~40 fps; animation: <=2 fps of a tiny rect.
        const int animPxPerSec = 2 * (2 * BOX_HALF + 1) * (2 * BOX_HALF + 1);
        const int normalPxPerSec = 20 * ui::W * ui::H;   // even a modest 20 fps full blit
        check("blit pixels per second < 1% of a 20 fps full-screen blit",
              animPxPerSec * 100 < normalPxPerSec);
    }

    printf("portrait (the tall 120x160 canvas):\n");
    {
        bool inside = true, moves = true;
        for (uint32_t f = 0; f < 64; f++) {
            const Pose p = pose(f, true);
            const Rect r = boxOf(p);
            if (r.x < 0 || r.y < 0 || r.x + r.w > 120 || r.y + r.h > 160) inside = false;
            const Pose l = pose(f);
            if (p.cx != l.cy || p.cy != l.cx || p.r != l.r) moves = false;
        }
        check("every tall pose's box is inside 120x160", inside);
        check("the tall poses are the landscape ones transposed", moves);
        static ui::Canvas c;
        c.setSize(120, 160);
        c.clear(0);
        const Dirty d = drawFrame(c, 11, 10, 0, 0xFFFF, true);
        const Pose p = pose(11, true);
        check("drawFrame(tall) draws the dot at the tall pose",
              c.px[p.cy * 120 + p.cx] == 0xFFFF && d.n >= 1);
    }

    printf("%s\n", fails ? "FAILED" : "all passed");
    return fails ? 1 : 0;
}
