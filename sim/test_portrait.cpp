// Host tests for portrait mode (ORIENT_PORTRAIT, the 120x160 canvas).
//
//   - layout: every tappable zone of every core screen inside 120x160, at
//     least 14 px each way, and clear of the other zones on its screen;
//   - hit zones: a tap at each zone does what the drawing says it does;
//   - the placeholder's BACK, from every screen without a portrait layout;
//   - the ORIENT row on ATTUNE page 2, in both orientations;
//   - bounds: every portrait screen drawn into a canvas with guard bands
//     either side writes nothing outside the buffer;
//   - an orientation switch resets every screen's prev State and the frame
//     shadow; the pixel diff covers every changed pixel;
//   - drawScreen in landscape is the landscape screen, byte for byte.
#include "../ui/draw.h"
#include "../ui/portrait.h"
#include "../ui/portrait_layout.h"
#include "../ui/framediff.h"
#include "../ui/prevstate.h"
#include "../ui/nav.h"
#include "../ui/files.h"
#include "../ui/layout.h"
#include "../ui/palette.h"
#include "../ui/demo.h"
#include "../ui/signatures.h"
#include "../ui/themepack.h"
#include "packload.h"
#include <stdio.h>
#include <string.h>

static int fails = 0;

static void check(const char* what, bool ok) {
    printf(ok ? "  ok   %s\n" : "  FAIL %s\n", what);
    if (!ok) fails++;
}

using namespace ui;

struct Z { const char* name; int x, y, w, h; };

static bool inside(const Z& z) {
    return z.x >= 0 && z.y >= 0 && z.x + z.w <= pl::PW && z.y + z.h <= pl::PH;
}
static bool overlap(const Z& a, const Z& b) {
    return a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h;
}

// Every zone inside, >= 14 px, and no two overlapping.
static void checkZones(const char* screen, const Z* z, int n) {
    bool in = true, big = true, apart = true;
    for (int i = 0; i < n; i++) {
        if (!inside(z[i])) { in = false; printf("    %s: %s outside\n", screen, z[i].name); }
        if (z[i].w < 14 || z[i].h < 14) { big = false; printf("    %s: %s %dx%d\n", screen, z[i].name, z[i].w, z[i].h); }
        for (int j = i + 1; j < n; j++)
            if (overlap(z[i], z[j])) { apart = false; printf("    %s: %s overlaps %s\n", screen, z[i].name, z[j].name); }
    }
    char m[96];
    snprintf(m, sizeof m, "%s: zones inside 120x160", screen);   check(m, in);
    snprintf(m, sizeof m, "%s: every zone >= 14x14", screen);    check(m, big);
    snprintf(m, sizeof m, "%s: no two zones overlap", screen);   check(m, apart);
}

static Settings portraitSet() { Settings s; s.orient = ORIENT_PORTRAIT; return s; }

// A tap: press, release (the release carries the last position seen).
static bool tap(Nav& n, Settings& s, int x, int y) {
    navTouch(n, s, true, (int16_t)x, (int16_t)y);
    return navTouch(n, s, false, (int16_t)x, (int16_t)y);
}

// The canvas with guard bands, so a write outside the 19,200 pixels shows.
struct Guarded {
    uint16_t pre[512];
    Canvas c;
    uint16_t post[512];
};
static constexpr uint16_t SENT = 0x5AA5;
static void arm(Guarded& g) { for (int i = 0; i < 512; i++) g.pre[i] = g.post[i] = SENT; }
static bool intact(const Guarded& g) {
    for (int i = 0; i < 512; i++) if (g.pre[i] != SENT || g.post[i] != SENT) return false;
    return true;
}

static const char* NAMES[SCR_COUNT] = {"scan", "codex", "detail", "domain", "mesh", "omens",
                                       "echoes", "grimoire", "attune", "aspect", "files"};

int main() {
    // The two packs from sim/packs, so the pack rites and a pack emblem are
    // drawn too (run from sim/, as make does).
    char why[128];
    const int packEmblem = packload::load("packs/cosmic-clone", why, sizeof why);
    const int packPlain  = packload::load("packs/umbravox-clone", why, sizeof why);
    check("both sim packs load", packEmblem >= pal::TH_COUNT && packPlain >= pal::TH_COUNT);

    printf("layout (every theme's dial):\n");
    for (int th = 0; th < pal::themeCount(); th++) {
        pal::setTheme((uint8_t)th);
        char tag[48];
        snprintf(tag, sizeof tag, "theme %d", th);
        // The panels themselves sit between the tiles and the footer.
        const bool dialFits = pl::DIAL_Y > pl::TILE_Y + pl::TILE_H &&
                              pl::DIAL_Y + pl::dialH() < pl::FOOT_Y &&
                              (SPEC_H == 0 || (pl::specY() >= pl::DIAL_Y + pl::dialH() &&
                                               pl::specY() + SPEC_H < pl::FOOT_Y));
        char m[96];
        snprintf(m, sizeof m, "%s: dial panel and spectrum fit above the footer", tag);
        check(m, dialFits);
        snprintf(m, sizeof m, "%s: the dial circle fits its panel", tag);
        check(m, pl::dialCX() - R_BOUND >= pl::DIAL_X && pl::dialCX() + R_BOUND < pl::DIAL_X + pl::DIAL_W &&
                 pl::dialCY() - R_BOUND >= pl::DIAL_Y && pl::dialCY() + R_BOUND < pl::DIAL_Y + pl::dialH());
        snprintf(m, sizeof m, "%s: the alert card holds the name and its detail", tag);
        check(m, pl::dialH() >= 66);

        Z z[8];
        int n = 0;
        z[n++] = {"menu", 0, 0, pl::BACK_W, pl::HDR_H};
        for (int i = 0; i < 3; i++) {
            int x, y, w, h; pl::tileRect(i, x, y, w, h);
            z[n++] = {i == 0 ? "tile wifi" : i == 1 ? "tile 5g" : "tile ble", x, y, w, h};
        }
        z[n++] = {"dial", pl::DIAL_X, pl::DIAL_Y, pl::DIAL_W, pl::dialH()};
        if (SPEC_H > 0) z[n++] = {"spectrum", pl::DIAL_X, pl::specY(), pl::DIAL_W, SPEC_H};
        z[n++] = {"start/stop", pl::LOG_X, pl::LOG_Y, pl::LOG_W, pl::LOG_H};
        snprintf(tag, sizeof tag, "scan, theme %d", th);
        checkZones(tag, z, n);
    }
    pal::setTheme(pal::TH_UMBRAVOX);
    {
        Z z[12];
        int n = 0;
        z[n++] = {"back", 0, 0, pl::BACK_W, pl::HDR_H};
        for (int i = 0; i <= GRIMOIRE_N; i++) {
            int x, y, w, h; pl::grimCell(i, x, y, w, h);
            z[n++] = {i == GRIMOIRE_N ? "labels" : GRIMOIRE[i].label, x, y, w, h};
        }
        checkZones("grimoire", z, n);
        bool aboveFooter = true;
        for (int i = 0; i < n; i++) if (z[i].y + z[i].h > pl::FOOT_Y) aboveFooter = false;
        check("grimoire: every cell above the footer", aboveFooter);
    }
    {
        Z z[16];
        int n = 0;
        z[n++] = {"back", 0, 0, pl::BACK_W, pl::HDR_H};
        for (int r = 0; r < SET_ROWS - 1; r++) {
            z[n++] = {"minus", pl::SET_MINUS_X, pl::setRowY(r) + 1, pl::SET_BTN_W, pl::SET_BTN_H};
            z[n++] = {"plus",  pl::SET_PLUS_X,  pl::setRowY(r) + 1, pl::SET_BTN_W, pl::SET_BTN_H};
        }
        const int yl = pl::setRowY(SET_ROWS - 1) + 1;
        z[n++] = {"pager", pl::SET_PAGE_X, yl, pl::SET_PAGE_W, pl::SET_BTN_H};
        z[n++] = {"clear", pl::SET_WIDE_X, yl, pl::SET_WIDE_W, pl::SET_BTN_H};
        checkZones("attune", z, n);
        check("attune: the last row ends above the footer", pl::setRowY(SET_ROWS) <= pl::FOOT_Y);
        check("attune: pager and CLEAR are >= 14 px apart",
              pl::SET_WIDE_X - (pl::SET_PAGE_X + pl::SET_PAGE_W) >= 14);
        // The FILES row's OPEN covers the -/+ pair: it is one zone, alone in its row.
        Z o[2] = {{"back", 0, 0, pl::BACK_W, pl::HDR_H},
                  {"open", pl::SET_WIDE_X, pl::setRowY(SET_FILES_ROW) + 1, pl::SET_WIDE_W, pl::SET_BTN_H}};
        checkZones("attune page 2 OPEN", o, 2);
    }
    {
        Z z[8];
        int n = 0;
        z[n++] = {"back", 0, 0, pl::BACK_W, pl::HDR_H};
        for (int i = 0; i < FILE_VIS; i++)
            z[n++] = {"row", pl::FILE_ROW_X, pl::FILE_ROW_Y + i * pl::FILE_ROW_H, pl::FILE_ROW_W, pl::FILE_ROW_H};
        z[n++] = {"delete", pl::FILE_DEL_X, pl::FILE_BAR_Y, pl::FILE_DEL_W, pl::FILE_BAR_H};
        checkZones("files", z, n);
        Z k[3] = {{"back", 0, 0, pl::BACK_W, pl::HDR_H},
                  {"yes", pl::FILE_YES_X, pl::FILE_YES_Y, pl::FILE_YES_W, pl::FILE_YES_H},
                  {"no",  pl::FILE_NO_X,  pl::FILE_NO_Y,  pl::FILE_NO_W,  pl::FILE_NO_H}};
        checkZones("files confirm", k, 3);
        const Z card = {"card", pl::FILE_CARD_X, pl::FILE_CARD_Y, pl::FILE_CARD_W, pl::FILE_CARD_H};
        bool in = true;
        for (int i = 1; i < 3; i++)
            if (k[i].x < card.x || k[i].y < card.y || k[i].x + k[i].w > card.x + card.w ||
                k[i].y + k[i].h > card.y + card.h) in = false;
        check("files confirm: YES and NO are on the card", in);
        const Z del = z[n - 1];
        check("files confirm: NO, not YES, is under the DELETE tap", overlap(del, k[2]) && !overlap(del, k[1]));
        check("files: rows and bar end above the footer", pl::FILE_BAR_Y + pl::FILE_BAR_H <= pl::FOOT_Y &&
              pl::FILE_CARD_Y + pl::FILE_CARD_H <= pl::FOOT_Y);
    }
    {
        Z z[2] = {{"back", 0, 0, pl::BACK_W, pl::HDR_H},
                  {"BACK button", pl::PH_BACK_X, pl::PH_BACK_Y, pl::PH_BACK_W, pl::PH_BACK_H}};
        checkZones("placeholder", z, 2);
    }

    printf("scan hit zones:\n");
    for (int th = 0; th < 2; th++) {
        pal::setTheme((uint8_t)th);
        const uint8_t want[3] = {F_WIFI, F_5G, F_BLE};
        for (int i = 0; i < 3; i++) {
            Nav n; Settings s = portraitSet();
            int x, y, w, h; pl::tileRect(i, x, y, w, h);
            tap(n, s, x + w / 2, y + h / 2);
            char m[64]; snprintf(m, sizeof m, "theme %d: tile %d opens CODEX filtered", th, i);
            check(m, n.screen == SCR_CODEX && n.filter == want[i] && n.backTo == SCR_SCAN);
        }
        { Nav n; Settings s = portraitSet(); tap(n, s, pl::dialCX(), pl::dialCY());
          check(th ? "theme 1: dial opens DOMAIN" : "theme 0: dial opens DOMAIN", n.screen == SCR_DOMAIN); }
        { Nav n; Settings s = portraitSet(); tap(n, s, 60, pl::DIAL_Y + 90 + 9);   // theme 0's strip area
          if (th == 0) { Nav m; Settings t = portraitSet(); tap(m, t, 60, pl::specY() + SPEC_H / 2);
                         check("theme 0: spectrum opens MESH", m.screen == SCR_MESH); }
          else check("theme 1 (no strip): no MESH below the dial", n.screen != SCR_MESH); }
        { Nav n; Settings s = portraitSet();
          const bool ch = tap(n, s, pl::LOG_X + pl::LOG_W / 2, pl::LOG_Y + pl::LOG_H / 2);
          check("START/STOP raises the log toggle, stays on SCAN",
                n.logToggleRequested && !ch && n.screen == SCR_SCAN); }
        { Nav n; Settings s = portraitSet(); tap(n, s, 4, 4);
          check("menu chip opens GRIMOIRE", n.screen == SCR_GRIMOIRE); }
        { Nav n; Settings s = portraitSet(); tap(n, s, 60, 36);   // the gap between tiles and dial
          check("a tap in a gap does nothing", n.screen == SCR_SCAN && !n.logToggleRequested); }
    }
    pal::setTheme(pal::TH_UMBRAVOX);
    {
        // The landscape zones are not the portrait ones: landscape's DIAL at
        // (109,48) is off the portrait canvas, and its tile column is not
        // portrait's dial.
        Nav n; Settings s = portraitSet();
        tap(n, s, COL_L_X + 5, TILE_Y[1] + 5);     // landscape 5 GHZ tile: portrait's dial
        check("portrait routes by portrait zones (landscape tile point -> DOMAIN)", n.screen == SCR_DOMAIN);
        Nav m; Settings l;
        tap(m, l, COL_L_X + 5, TILE_Y[1] + 5);
        check("...and landscape still by landscape ones (-> CODEX 5G)", m.screen == SCR_CODEX && m.filter == F_5G);
    }

    printf("grimoire hit zones:\n");
    {
        bool all = true;
        for (int i = 0; i < GRIMOIRE_N; i++) {
            Nav n; n.screen = SCR_GRIMOIRE; Settings s = portraitSet();
            int x, y, w, h; pl::grimCell(i, x, y, w, h);
            tap(n, s, x + w / 2, y + h / 2);
            if (n.screen != GRIMOIRE[i].screen) { all = false; printf("    cell %d -> %d\n", i, n.screen); }
            if (n.screen != SCR_SCAN && n.screen != SCR_ATTUNE && n.backTo != SCR_GRIMOIRE) all = false;
        }
        check("every cell opens its screen (BACK comes back to GRIMOIRE)", all);
        Nav n; n.screen = SCR_GRIMOIRE; Settings s = portraitSet();
        int x, y, w, h; pl::grimCell(GRIMOIRE_N, x, y, w, h);
        tap(n, s, x + w / 2, y + h / 2);
        check("the LABELS row toggles plain labels", s.labels == 1 && n.screen == SCR_GRIMOIRE);
        tap(n, s, 4, 4);
        check("header back returns to SCAN", n.screen == SCR_SCAN);
    }

    printf("attune hit zones:\n");
    {
        Nav n; n.screen = SCR_ATTUNE; Settings s = portraitSet();
        const uint8_t b0 = s.bright;
        tap(n, s, pl::SET_PLUS_X + 5, pl::setRowY(4) + 5);
        check("page 1: BRIGHT + steps up", s.bright > b0);
        tap(n, s, pl::SET_MINUS_X + 5, pl::setRowY(4) + 5);
        check("page 1: BRIGHT - steps back", s.bright == b0);
        const uint16_t d0 = s.dwellMs;
        tap(n, s, pl::SET_PLUS_X + 5, pl::setRowY(1) + 5);
        check("page 1: DWELL + steps", s.dwellMs == d0 + 40);
        const int i0 = idleCombinedIndex(s.idleIdx, s.idleMode);
        tap(n, s, pl::SET_PLUS_X + 5, pl::setRowY(SET_IDLE_ROW) + 5);
        check("page 1: IDLE + walks the round robin", idleCombinedIndex(s.idleIdx, s.idleMode) == i0 + 1);
        tap(n, s, pl::SET_WIDE_X + 10, pl::setRowY(SET_ROWS - 1) + 5);
        check("CLEAR raises the reset request", n.resetRequested);
        tap(n, s, pl::SET_PAGE_X + 10, pl::setRowY(SET_ROWS - 1) + 5);
        check("the pager turns to page 2", n.setPage == 1 && n.screen == SCR_ATTUNE);
        tap(n, s, pl::SET_PLUS_X + 5, pl::setRowY(SET_AUTOLOG_ROW) + 5);
        check("page 2: AUTO LOG + flips it", s.autoLog == 1);
        tap(n, s, pl::SET_MINUS_X + 5, pl::setRowY(SET_AUTOLOG_ROW) + 5);
        check("page 2: AUTO LOG - flips it back", s.autoLog == 0);
        tap(n, s, pl::SET_PLUS_X + 5, pl::setRowY(SET_ORIENT_ROW) + 5);
        check("page 2: ORIENT + goes round to LANDSCAPE", s.orient == ORIENT_LANDSCAPE);
        // Now landscape, and the same row (landscape geometry) brings it back.
        tap(n, s, SET_PLUS_X + 5, SET_Y + SET_ORIENT_ROW * SET_H + 5);
        check("landscape page 2: ORIENT + goes round to PORTRAIT", s.orient == ORIENT_PORTRAIT);
        tap(n, s, pl::SET_MINUS_X + 5, pl::setRowY(SET_ORIENT_ROW) + 5);
        check("page 2: ORIENT - also flips (two values)", s.orient == ORIENT_LANDSCAPE);
        tap(n, s, SET_MINUS_X + 5, SET_Y + SET_ORIENT_ROW * SET_H + 5);
        check("landscape page 2: ORIENT - flips too", s.orient == ORIENT_PORTRAIT);
        check("the switch keeps ATTUNE page 2 up", n.screen == SCR_ATTUNE && n.setPage == 1);
        setPortraitAllowed(false);
        Settings l;                       // landscape, on a landscape-only board
        Nav m; m.screen = SCR_ATTUNE; m.setPage = 1;
        tap(m, l, SET_PLUS_X + 5, SET_Y + SET_ORIENT_ROW * SET_H + 5);
        check("a landscape-only board: ORIENT stays LANDSCAPE", l.orient == ORIENT_LANDSCAPE);
        setPortraitAllowed(true);
        tap(n, s, pl::SET_WIDE_X + 10, pl::setRowY(SET_FILES_ROW) + 5);
        check("page 2: OPEN goes to FILES", n.screen == SCR_FILES && n.filesScanRequested);
        tap(n, s, 4, 4);
        check("FILES back returns to ATTUNE page 2", n.screen == SCR_ATTUNE && n.setPage == 1);
        tap(n, s, 4, 4);
        check("ATTUNE back returns to SCAN", n.screen == SCR_SCAN);
    }

    printf("files hit zones:\n");
    {
        static FileList L;
        L = FileList();
        L.card = true;
        L.n = 8;
        for (int i = 0; i < L.n; i++) {
            snprintf(L.rows[i].name, FILE_NAME_MAX, "WD-202610%02u-120000.csv", (unsigned)(20 - i) % 32u);
            L.rows[i].size = 1000u * (i + 1);
        }
        Nav n; Settings s = portraitSet();
        filesEnter(n);
        filesRescanned(n, L, nullptr);
        filesSetActive(n, 0);
        filesTick(n, 1000);
        tap(n, s, pl::FILE_ROW_X + 20, pl::FILE_ROW_Y + 2 * pl::FILE_ROW_H + 8);
        check("a row tap selects that row", n.fileSel == 2);
        tap(n, s, pl::FILE_DEL_X + 10, pl::FILE_BAR_Y + 8);
        check("DELETE opens the confirmation", n.fileConfirm);
        filesTick(n, 1000 + CONFIRM_ARM_MS + 1);
        tap(n, s, pl::FILE_NO_X + 10, pl::FILE_NO_Y + 10);
        check("NO cancels, nothing deleted", !n.fileConfirm && !n.fileDeleteRequested);
        tap(n, s, pl::FILE_DEL_X + 10, pl::FILE_BAR_Y + 8);
        filesTick(n, 1000 + 2 * CONFIRM_ARM_MS + 10);
        tap(n, s, pl::FILE_YES_X + 5, pl::FILE_YES_Y + 5);
        check("YES asks the caller to delete the selected row",
              n.fileDeleteRequested && n.fileDeleteIdx == 2);
        n.fileDeleteRequested = false;
        tap(n, s, pl::FILE_ROW_X + 20, pl::FILE_ROW_Y + 8);
        tap(n, s, pl::FILE_DEL_X + 10, pl::FILE_BAR_Y + 8);
        check("the logging row cannot be confirmed for delete", !n.fileConfirm && n.fileSel == 0);
        // A drag scrolls the list by portrait rows.
        n.maxScroll = filesMaxScroll(n);
        navTouch(n, s, true, 60, 40);
        // The drag starts once the finger has moved past DRAG_START (at 48),
        // so 40 -> 80 is 36 px of drag: two 16-px rows (three landscape ones).
        for (int y = 40; y <= 80; y += 4) navTouch(n, s, true, 60, (int16_t)y);
        navTouch(n, s, false, 60, 80);
        check("a drag scrolls by portrait rows (36 px -> 2)", n.scroll == 2);
    }

    printf("placeholder BACK:\n");
    {
        bool fromGrim = true, fromHeader = true, noOther = true;
        for (int scr = 0; scr < SCR_COUNT; scr++) {
            if (portraitCore((uint8_t)scr)) continue;
            Nav n; Settings s = portraitSet();
            n.screen = (uint8_t)scr; n.backTo = SCR_GRIMOIRE;
            tap(n, s, 60, 60);                       // the card: not a control
            if (n.screen != scr) noOther = false;
            tap(n, s, pl::PH_BACK_X + pl::PH_BACK_W / 2, pl::PH_BACK_Y + pl::PH_BACK_H / 2);
            if (n.screen != SCR_GRIMOIRE) fromGrim = false;
            Nav m; m.screen = (uint8_t)scr; m.backTo = SCR_SCAN;
            tap(m, s, 3, 3);
            if (m.screen != SCR_SCAN) fromHeader = false;
        }
        check("the BACK button returns to where it was opened from", fromGrim);
        check("the header back chip returns too", fromHeader);
        check("a tap on the card itself does nothing", noOther);
        Nav n; Settings s = portraitSet();
        n.screen = SCR_DETAIL; n.backTo = SCR_DETAIL;     // never a placeholder's target
        tap(n, s, pl::PH_BACK_X + 5, pl::PH_BACK_Y + 5);
        check("an unusable back target falls back to SCAN", n.screen == SCR_SCAN);
        // The whole path: scan tile -> CODEX placeholder -> BACK -> scan.
        Nav p; p.screen = SCR_SCAN;
        int x, y, w, h; pl::tileRect(2, x, y, w, h);
        tap(p, s, x + 5, y + 5);
        tap(p, s, pl::PH_BACK_X + 5, pl::PH_BACK_Y + 5);
        check("scan -> placeholder -> BACK is scan again", p.screen == SCR_SCAN);
        // A placeholder does not scroll (no list is drawn).
        Nav q; q.screen = SCR_CODEX; q.maxScroll = 10;
        navTouch(q, s, true, 60, 40);
        for (int yy = 40; yy < 100; yy += 5) navTouch(q, s, true, 60, (int16_t)yy);
        navTouch(q, s, false, 60, 100);
        check("a drag on a placeholder scrolls nothing", q.scroll == 0 && q.screen == SCR_CODEX);
    }

    printf("bounds (guard bands, every portrait screen):\n");
    {
        static Guarded g;
        bool ok = true, sized = true;
        int frames = 0;
        const int themes[5] = {pal::TH_UMBRAVOX, pal::TH_BAPH0MAU5, pal::TH_COSMICMUFFIN, packEmblem, packPlain};
        for (int t = 0; t < 5; t++) {
            pal::setTheme((uint8_t)themes[t]);
            for (int scr = 0; scr < SCR_COUNT; scr++) {
                for (int variant = 0; variant < 4; variant++) {
                    State s = demoState((uint32_t)(variant * 37));
                    s.set.theme = (uint8_t)themes[t];
                    s.set.orient = ORIENT_PORTRAIT;
                    s.set.labels = (uint8_t)(variant & 1);
                    s.screen = (uint8_t)scr;
                    s.setPage = (uint8_t)(variant & 1);
                    s.nNodes = (uint8_t)(variant * 40);
                    s.touchDown = variant == 2; s.touchX = 118; s.touchY = 158;
                    s.logState = (uint8_t)(variant % 3);
                    if (scr == SCR_FILES) {
                        s.fileCard = variant != 3;
                        s.nFileRows = s.fileCard ? FILE_VIS : 0;
                        s.fileTotal = 40; s.fileFound = 41; s.filePartial = variant == 1;
                        for (int i = 0; i < FILE_VIS; i++) {
                            snprintf(s.fileRows[i].name, FILE_NAME_MAX, "WD-20261027-1530%02u.csv", (unsigned)(i % 60));
                            s.fileRows[i].size = 123456789u * (uint32_t)(i + 1);
                        }
                        s.fileSel = 1; s.fileActive = (int8_t)(variant == 2 ? 1 : 0);
                        snprintf(s.fileSelName, FILE_NAME_MAX, "%s", s.fileRows[1].name);
                        s.fileSelSize = 4000000000u;
                        s.fileConfirm = variant == 0; s.fileResult = (uint8_t)(variant == 2 ? 2 : 0);
                        s.cardMB = 1907000; s.fileLogKB = 4000000000u;
                    }
                    for (int al = 0; al < (scr == SCR_SCAN ? ALERT_STYLE_COUNT : 1); al++)
                        for (int age = 0; age <= 1800; age += (scr == SCR_SCAN ? 30 : 1800)) {
                            s.alertActive = scr == SCR_SCAN;
                            s.alertThreat = TH_DRONE;
                            s.alertConf = (uint8_t)(age / 30 % 3);
                            s.set.alertStyle = (uint8_t)al;
                            s.alertAge = (uint16_t)age;
                            snprintf(s.alertWhy, sizeof s.alertWhy, "DRONEID");
                            snprintf(s.alertName, sizeof s.alertName, "60:60:1F:A2:4C:19");
                            arm(g);
                            g.c.clear(0);
                            drawScreen(g.c, s, nullptr);
                            frames++;
                            if (!intact(g)) { ok = false; printf("    %s theme %d variant %d\n", NAMES[scr], themes[t], variant); }
                            if (g.c.w != pl::PW || g.c.h != pl::PH) sized = false;
                        }
                }
            }
        }
        char m[80];
        snprintf(m, sizeof m, "%d portrait frames: nothing written outside the buffer", frames);
        check(m, ok);
        check("drawScreen sized every portrait frame 120x160", sized);
        pal::setTheme(pal::TH_UMBRAVOX);
    }

    printf("orientation switch and the pixel diff:\n");
    {
        check("the frame shadow is available (host: static)", frameDiffReady());
        prevState(SCR_SCAN).nets24 = 99;
        prevState(SCR_FILES).fileTotal = 7;
        orientationChanged();
        check("a switch resets every screen's prev State",
              prevState(SCR_SCAN).nets24 == 0 && prevState(SCR_FILES).fileTotal == 0);
        static Canvas c;
        c.setSize(pl::PW, pl::PH);
        c.clear(1);
        Dirty d;
        frameDiff(c, d);
        check("the first diff after a switch is the whole canvas",
              d.n == 1 && d.r[0].x == 0 && d.r[0].y == 0 && d.r[0].w == pl::PW && d.r[0].h == pl::PH);
        frameDiff(c, d);
        check("an unchanged frame reports nothing", d.n == 0);
        c.pixel(77, 101, 5);
        frameDiff(c, d);
        check("one changed pixel is one rect containing it",
              d.n == 1 && d.r[0].x <= 77 && d.r[0].x + d.r[0].w > 77 && d.r[0].y <= 101 && d.r[0].y + d.r[0].h > 101);
        // Random scribbles: every changed pixel covered, never more than MAX.
        static uint16_t before[W * H];
        bool covered = true, bounded = true;
        uint32_t r = 12345;
        for (int round = 0; round < 200; round++) {
            memcpy(before, c.px, sizeof before);
            const int k = (int)(r % 40); r = r * 1103515245u + 12345u;
            for (int i = 0; i < k; i++) {
                const int x = (int)((r >> 8) % pl::PW); r = r * 1103515245u + 12345u;
                const int y = (int)((r >> 8) % pl::PH); r = r * 1103515245u + 12345u;
                c.fill(x, y, (int)(r >> 28) + 1, (int)((r >> 24) & 7) + 1, (uint16_t)r);
                r = r * 1103515245u + 12345u;
            }
            frameDiff(c, d);
            if (d.n > Dirty::MAX) bounded = false;
            for (int y = 0; y < pl::PH; y++)
                for (int x = 0; x < pl::PW; x++) {
                    if (c.px[y * pl::PW + x] == before[y * pl::PW + x]) continue;
                    bool in = false;
                    for (int i = 0; i < d.n && !in; i++)
                        in = x >= d.r[i].x && x < d.r[i].x + d.r[i].w && y >= d.r[i].y && y < d.r[i].y + d.r[i].h;
                    if (!in) covered = false;
                }
        }
        check("200 random frames: every changed pixel is in a dirty rect", covered);
        check("...and never more than Dirty::MAX rects", bounded);
        c.setSize(W, H);
        frameDiff(c, d);
        check("a change of canvas size reports the whole canvas",
              d.n == 1 && d.r[0].w == W && d.r[0].h == H);
        // A portrait frame through drawScreen fills its dirty list from the diff.
        orientationChanged();
        State s = demoState(0);
        s.set.orient = ORIENT_PORTRAIT;
        static Canvas p;
        drawScreen(p, s, &d);
        check("drawScreen's first portrait frame is all dirty",
              d.n == 1 && d.r[0].w == pl::PW && d.r[0].h == pl::PH);
        drawScreen(p, s, &d);
        check("...and the same State again is clean", d.n == 0);
        s.frame = 5;                 // the dial moves
        drawScreen(p, s, &d);
        bool inDial = d.n > 0;
        for (int i = 0; i < d.n; i++)
            if (d.r[i].y < pl::DIAL_Y || d.r[i].y + d.r[i].h > pl::DIAL_Y + pl::dialH() + DIFF_BAND) inDial = false;
        check("a moving sweep dirties only the dial's bands", inDial);
    }

    printf("landscape is untouched:\n");
    {
        static Canvas a, b;
        bool same = true;
        for (int scr = 0; scr < SCR_COUNT; scr++) {
            State s = demoState(11);
            s.screen = (uint8_t)scr;
            drawScreen(a, s, nullptr);
            switch (scr) {
                case SCR_CODEX: drawCodex(b, s); break;
                case SCR_DETAIL: drawDetail(b, s); break;
                case SCR_DOMAIN: drawDomain(b, s); break;
                case SCR_MESH: drawMesh(b, s); break;
                case SCR_OMENS: drawOmens(b, s); break;
                case SCR_ECHOES: drawEchoes(b, s); break;
                case SCR_GRIMOIRE: drawGrimoire(b, s); break;
                case SCR_ATTUNE: drawAttune(b, s); break;
                case SCR_ASPECT: drawAspect(b, s); break;
                case SCR_FILES: drawFiles(b, s); break;
                default: drawScan(b, s); break;
            }
            if (a.w != W || a.h != H || memcmp(a.px, b.px, sizeof a.px) != 0) same = false;
        }
        check("a default State draws every screen in landscape, byte for byte", same);
        // Back from portrait: the canvas is landscape again.
        State s = demoState(0);
        s.set.orient = ORIENT_PORTRAIT;
        drawScreen(a, s, nullptr);
        s.set.orient = ORIENT_LANDSCAPE;
        drawScreen(a, s, nullptr);
        drawScan(b, s);
        check("portrait then landscape draws the landscape frame",
              a.w == W && a.h == H && memcmp(a.px, b.px, sizeof a.px) == 0);
    }

    printf(fails ? "\n%d failed\n" : "\nall passed\n", fails);
    return fails ? 1 : 0;
}
