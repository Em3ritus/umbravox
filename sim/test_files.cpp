// Host tests for the FILES screen: the pure UI state machine (ui/files.h,
// driven through navTouch exactly as the panel drives it), its rendering,
// and the device's list/delete rules (device/filescan.h), which must agree
// with the serial SDDEL's policy (device/sdcmd.h) file for file.
#include "../ui/files.h"
#include "../ui/nav.h"
#include "../ui/layout.h"
#include "../ui/palette.h"
#include "../ui/screens.h"
#include "../ui/canvas.h"
#include "../ui/font5x7.h"
#include "../device/filescan.h"
#include <stdio.h>
#include <string.h>

static int fails = 0;
static void check(const char* what, bool ok) {
    printf(ok ? "  ok   %s\n" : "  FAIL %s\n", what);
    if (!ok) fails++;
}

using namespace ui;

static Settings gSet;

// A tap: press, then release, at (x, y), at the given time.
static bool tap(Nav& n, int x, int y, uint32_t now) {
    filesTick(n, now);
    navTouch(n, gSet, true, (int16_t)x, (int16_t)y);
    return navTouch(n, gSet, false, (int16_t)x, (int16_t)y);
}

static void drag(Nav& n, int x, int y0, int y1, uint32_t now) {
    filesTick(n, now);
    navTouch(n, gSet, true, (int16_t)x, (int16_t)y0);
    const int step = y1 > y0 ? 2 : -2;
    for (int y = y0; y1 > y0 ? y <= y1 : y >= y1; y += step) navTouch(n, gSet, true, (int16_t)x, (int16_t)y);
    navTouch(n, gSet, false, (int16_t)x, (int16_t)y1);
}

// A list of `k` dated logs, newest first: WD-20260127-1530SS... descending.
static FileList makeList(int k) {
    FileList L;
    L.card = true;
    for (int i = 0; i < k && i < MAX_FILE_ROWS; i++) {
        snprintf(L.rows[i].name, sizeof L.rows[i].name, "WD-2026%02d%02d-120000.csv",
                 12 - i / 28, 28 - i % 28);
        L.rows[i].size = 1000u * (uint32_t)(i + 1);
    }
    L.n = (uint8_t)(k < MAX_FILE_ROWS ? k : MAX_FILE_ROWS);
    L.found = (uint16_t)k;
    return L;
}

// FILES opened with `k` rows, as the device would set it up.
static Nav openWith(int k, const FileList& L, uint32_t now = 1000) {
    Nav n;
    n.screen = SCR_ATTUNE;
    n.setPage = 1;
    filesTick(n, now);
    const int y0 = SET_Y + SET_FILES_ROW * SET_H;
    navTouch(n, gSet, true, SET_MINUS_X + 5, (int16_t)(y0 + 4));
    navTouch(n, gSet, false, SET_MINUS_X + 5, (int16_t)(y0 + 4));
    (void)k;
    filesRescanned(n, L, nullptr);
    n.filesScanRequested = false;
    n.maxScroll = filesMaxScroll(n);
    return n;
}

static int rowY(int i) { return FILE_ROW_Y + i * FILE_ROW_H + FILE_ROW_H / 2; }
static const int ROWX = 40;
static const int DELX = FILE_DEL_X + FILE_DEL_W / 2, DELY = FILE_BAR_Y + FILE_BAR_H / 2;
static const int YESX = FILE_YES_X + FILE_YES_W / 2, YESY = FILE_YES_Y + FILE_YES_H / 2;
static const int NOX = FILE_NO_X + FILE_NO_W / 2, NOY = FILE_NO_Y + FILE_NO_H / 2;

static bool sameRegion(const Canvas& a, const Canvas& b, int x0, int y0, int w, int h) {
    for (int y = y0; y < y0 + h; y++)
        for (int x = x0; x < x0 + w; x++)
            if (a.px[y * W + x] != b.px[y * W + x]) return false;
    return true;
}
// Equal everywhere outside the rect.
static bool sameOutside(const Canvas& a, const Canvas& b, int rx, int ry, int rw, int rh) {
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            if (x >= rx && x < rx + rw && y >= ry && y < ry + rh) continue;
            if (a.px[y * W + x] != b.px[y * W + x]) return false;
        }
    return true;
}
static bool differs(const Canvas& a, const Canvas& b, int x0, int y0, int w, int h) {
    return !sameRegion(a, b, x0, y0, w, h);
}

static int gRemoved = 0;
static bool fEx(const char* p) { return strcmp(p, "/WD-0404.csv") != 0; }
static bool fDir(const char* p) { return !strcmp(p, "/WD-0007.csv"); }
static bool fRm(const char* p) { gRemoved++; return strcmp(p, "/WD-0500.csv") != 0; }

int main() {
    pal::setTheme(pal::TH_UMBRAVOX);

    // ---- formatting -------------------------------------------------------
    {
        char b[24];
        fileLabel("WD-20260127-153012.csv", b, sizeof b);
        check("label: dated name reads 2026-01-27 15:30", !strcmp(b, "2026-01-27 15:30"));
        fileLabel("wd-20260127-153012.CSV", b, sizeof b);
        check("label: any case", !strcmp(b, "2026-01-27 15:30"));
        fileLabel("WD-0012.csv", b, sizeof b);
        check("label: dateless name reads WD-0012", !strcmp(b, "WD-0012"));
        fileLabel("WD-0012.CSV", b, sizeof b);
        check("label: dateless .CSV reads WD-0012", !strcmp(b, "WD-0012"));
        fileLabel("WD-20260127-153012.csv", b, 5);
        check("label: cut to cap and terminated", !strcmp(b, "2026"));

        struct { uint32_t v; const char* want; } B[] = {
            {0, "0B"}, {999, "999B"}, {1000, "1.0K"}, {1536, "1.5K"}, {10240, "10K"},
            {1023u * 1024u, "1.0M"}, {5u * 1048576u + 524288u, "5.5M"}, {123u * 1048576u, "123M"},
            {4000000000u, "3.7G"},
        };
        bool ok = true;
        for (auto& t : B) { fmtBytes(t.v, b); if (strcmp(b, t.want)) { ok = false; printf("    %u -> %s (want %s)\n", t.v, b, t.want); } }
        check("fmtBytes: B, K, M, G at <= 4 characters", ok);
        struct { uint32_t v; const char* want; } K[] = {
            {980, "980K"}, {29360128u, "28G"}, {3250585u, "3.1G"}, {1181116006u, "1.1T"}, {0, "0K"},
        };
        ok = true;
        for (auto& t : K) { fmtKB(t.v, b); if (strcmp(b, t.want)) { ok = false; printf("    %u -> %s (want %s)\n", t.v, b, t.want); } }
        check("fmtKB: card sizes", ok);
    }

    // ---- the way in, and back ----------------------------------------------
    {
        FileList L = makeList(5);
        Nav n = openWith(5, L);
        check("ATTUNE page 2, FILES row: OPEN opens FILES", n.screen == SCR_FILES);
        Nav m;
        m.screen = SCR_ATTUNE; m.setPage = 1;
        const int y0 = SET_Y + SET_FILES_ROW * SET_H;
        tap(m, SET_MINUS_X + 5, y0 + 4, 100);
        check("opening asks the caller for a scan", m.screen == SCR_FILES && m.filesScanRequested);
        check("opening starts with nothing selected, no confirm, top of list",
              m.fileSel == -1 && !m.fileConfirm && m.scroll == 0);
        Nav p;
        p.screen = SCR_ATTUNE; p.setPage = 0;
        const uint16_t before = gSet.dwellMs;
        tap(p, SET_MINUS_X + 5, y0 + 4, 100);
        check("page 1, same spot: still the DWELL knob, not FILES",
              p.screen == SCR_ATTUNE && gSet.dwellMs != before && !p.filesScanRequested);
        gSet = Settings();
        Nav q;
        q.screen = SCR_ATTUNE; q.setPage = 1;
        tap(q, 6, y0 + 4, 100);
        check("page 2, FILES label: inert", q.screen == SCR_ATTUNE);

        const bool sw = tap(n, 4, 4, 2000);
        check("back from FILES returns to ATTUNE page 2", sw && n.screen == SCR_ATTUNE && n.setPage == 1);
        tap(n, 4, 4, 2100);
        check("...and back again to SCAN", n.screen == SCR_SCAN && n.setPage == 0);
    }

    // ---- rows: hit zones and selection ----------------------------------------
    {
        FileList L0 = makeList(0);
        Nav n = openWith(0, L0);
        tap(n, ROWX, rowY(0), 2000);
        check("0 rows: a row tap selects nothing", n.fileSel == -1);
        tap(n, DELX, DELY, 2100);
        check("0 rows: DELETE opens nothing", !n.fileConfirm);

        FileList L1 = makeList(1);
        n = openWith(1, L1);
        tap(n, ROWX, rowY(2), 2000);
        check("1 row: tap under it selects nothing", n.fileSel == -1);
        tap(n, ROWX, rowY(0), 2100);
        check("1 row: tap selects row 0", n.fileSel == 0);
        tap(n, ROWX, rowY(0), 2200);
        check("tapping the selected row clears the selection", n.fileSel == -1);

        FileList L5 = makeList(5);
        n = openWith(5, L5);
        bool ok = true;
        for (int i = 0; i < FILE_VIS; i++) {
            n.fileSel = -1;
            tap(n, ROWX, rowY(i), 3000 + (uint32_t)i * 100);
            if (n.fileSel != i) ok = false;
        }
        check("5 rows: each row's middle selects it", ok);
        n.fileSel = -1;
        tap(n, ROWX, FILE_ROW_Y - 2, 4000);
        check("count line above the rows is inert", n.fileSel == -1);
        tap(n, ROWX, FILE_ROW_Y + FILE_VIS * FILE_ROW_H + 1, 4100);
        check("the gap under the rows is inert", n.fileSel == -1);
    }

    // ---- scrolling at 0 / 1 / 64 rows -----------------------------------------
    {
        FileList L;
        Nav n;
        L = makeList(0);  n = openWith(0, L);
        check("max scroll, 0 rows: 0", filesMaxScroll(n) == 0);
        L = makeList(1);  n = openWith(1, L);
        check("max scroll, 1 row: 0", filesMaxScroll(n) == 0);
        drag(n, ROWX, 30, 80, 2000);
        check("1 row: a drag does not scroll", n.scroll == 0);
        L = makeList(5);  n = openWith(5, L);
        check("max scroll, 5 rows: 0", filesMaxScroll(n) == 0);
        L = makeList(6);  n = openWith(6, L);
        check("max scroll, 6 rows: 1", filesMaxScroll(n) == 1);
        L = makeList(64); n = openWith(64, L);
        check("max scroll, 64 rows: 59", filesMaxScroll(n) == 59);
        for (int k = 0; k < 20; k++) drag(n, ROWX, 26, 98, 2000 + (uint32_t)k * 50);
        check("64 rows: dragging stops at 59", n.scroll == 59);
        tap(n, ROWX, rowY(4), 4000);
        check("64 rows scrolled: the last row selects index 63", n.fileSel == 63);
        for (int k = 0; k < 20; k++) drag(n, ROWX, 98, 26, 5000 + (uint32_t)k * 50);
        check("dragging back stops at 0", n.scroll == 0);
        check("scrolling keeps the selection", n.fileSel == 63);
    }

    // ---- DELETE and the confirmation ------------------------------------------
    {
        FileList L = makeList(5);
        Nav n = openWith(5, L);
        tap(n, DELX, DELY, 2000);
        check("DELETE with nothing selected: no confirm", !n.fileConfirm);
        tap(n, ROWX, rowY(2), 2100);
        tap(n, DELX, DELY, 3000);
        check("DELETE on a selected row opens the confirm", n.fileConfirm && n.fileConfirmAt == 3000);

        // A bounce of the DELETE tap lands where the finger was: on NO's
        // side. Inside the arm time nothing counts, YES or not.
        tap(n, DELX, DELY, 3150);
        check("a bounce on DELETE's spot inside 400 ms is ignored (still open)", n.fileConfirm);
        tap(n, YESX, YESY, 3399);
        check("YES inside 400 ms is ignored", n.fileConfirm && !n.fileDeleteRequested);
        tap(n, NOX, NOY, 3200);
        check("NO inside 400 ms is ignored too", n.fileConfirm);
        tap(n, YESX, YESY, 3400);
        check("YES at 400 ms asks to delete the selected row",
              !n.fileConfirm && n.fileDeleteRequested && n.fileDeleteIdx == 2);

        n = openWith(5, L);
        tap(n, ROWX, rowY(1), 2000);
        tap(n, DELX, DELY, 3000);
        tap(n, NOX, NOY, 3500);
        check("NO cancels: no request, selection kept", !n.fileConfirm && !n.fileDeleteRequested && n.fileSel == 1);
        tap(n, DELX, DELY, 4000);
        tap(n, 80, 30, 4500);
        check("a tap anywhere but YES cancels", !n.fileConfirm && !n.fileDeleteRequested);
        tap(n, DELX, DELY, 5000);
        tap(n, YESX + FILE_YES_W, YESY, 5500);
        check("just right of YES is not YES", !n.fileConfirm && !n.fileDeleteRequested);
        check("NO is the larger target", FILE_NO_W * FILE_NO_H > 2 * FILE_YES_W * FILE_YES_H);
        check("YES is clear of DELETE's spot", !(DELX >= FILE_YES_X && DELX < FILE_YES_X + FILE_YES_W));
        check("NO covers DELETE's spot", DELX >= FILE_NO_X && DELX < FILE_NO_X + FILE_NO_W &&
                                         DELY >= FILE_NO_Y && DELY < FILE_NO_Y + FILE_NO_H);

        // Timeout.
        tap(n, DELX, DELY, 6000);
        filesTick(n, 6000 + CONFIRM_MS - 1);
        check("confirm still up at 9.999 s", n.fileConfirm);
        filesTick(n, 6000 + CONFIRM_MS);
        check("confirm cancels itself at 10 s", !n.fileConfirm && !n.fileDeleteRequested);
        // ...across a millis() wrap.
        tap(n, DELX, DELY, 0xFFFFFF00u);
        filesTick(n, 0xFFFFFF00u + 500);
        check("wrap: still up 500 ms later", n.fileConfirm);
        filesTick(n, 0xFFFFFF00u + CONFIRM_MS);
        check("wrap: cancels at 10 s", !n.fileConfirm);

        // Leaving cancels.
        tap(n, DELX, DELY, 20000);
        tap(n, 4, 4, 20100);
        check("back while confirming leaves and cancels (inside the arm time too)",
              n.screen == SCR_ATTUNE && !n.fileConfirm && !n.fileDeleteRequested);

        // A drag on the card does not scroll the list under it.
        FileList L64 = makeList(64);
        n = openWith(64, L64);
        tap(n, ROWX, rowY(0), 2000);
        tap(n, DELX, DELY, 3000);
        drag(n, ROWX, 30, 90, 3600);
        check("a drag while confirming does not scroll", n.scroll == 0);
    }

    // ---- the active session -----------------------------------------------------
    {
        FileList L = makeList(5);
        Nav n = openWith(5, L);
        filesSetActive(n, 0);
        tap(n, ROWX, rowY(0), 2000);
        check("the active row can be selected", n.fileSel == 0);
        tap(n, DELX, DELY, 3000);
        check("DELETE is disabled on the active row: no confirm", !n.fileConfirm);
        // A file that becomes the session while its confirm is up.
        tap(n, ROWX, rowY(1), 4000);
        tap(n, DELX, DELY, 5000);
        check("(a non-active row confirms)", n.fileConfirm);
        filesSetActive(n, 1);
        check("it becoming the session cancels the confirm", !n.fileConfirm);
        // Even a YES that somehow lands after it became active is refused.
        filesSetActive(n, -1);
        tap(n, DELX, DELY, 6000);
        n.filesActive = 1;           // set behind filesSetActive's back
        tap(n, YESX, YESY, 7000);
        check("YES on the active row never requests a delete", !n.fileDeleteRequested);
    }

    // ---- after a delete: rescan, selection, result ------------------------------
    {
        FileList L = makeList(10);
        Nav n = openWith(10, L);
        drag(n, ROWX, 30, 98, 1500);       // scroll to the bottom (5)
        tap(n, ROWX, rowY(2), 2000);
        check("(a row is selected)", n.fileSel >= 0);
        const int sel = n.fileSel < 0 ? 0 : n.fileSel;
        char gone[FILE_NAME_MAX];
        strcpy(gone, L.rows[sel].name);
        char keep[FILE_NAME_MAX];
        strcpy(keep, L.rows[sel + 1].name);
        // The device deleted it; the list shrinks.
        FileList L2 = L;
        for (int i = sel; i < L2.n - 1; i++) L2.rows[i] = L2.rows[i + 1];
        L2.n--;
        filesRescanned(n, L2, gone);
        filesResult(n, true, 3000);
        check("deleted file: selection cleared", n.fileSel == -1);
        check("scroll clamped to the shorter list", n.scroll <= filesMaxScroll(n) && n.filesN == 9);
        check("result DELETED", n.fileResult == FR_DELETED);
        filesTick(n, 3000 + RESULT_MS - 1);
        check("result up at 1.999 s", n.fileResult == FR_DELETED);
        filesTick(n, 3000 + RESULT_MS);
        check("result gone at 2 s", n.fileResult == FR_NONE);

        // A failed delete keeps the selection, by name, even if rows moved.
        const int ki = fileIndexOf(L2, keep) < 0 ? 0 : fileIndexOf(L2, keep);
        n.fileSel = (int8_t)ki;
        FileList L3 = L2;
        // a new, newer log arrived at the top: everything moves down one
        for (int i = L3.n; i > 0; i--) L3.rows[i] = L3.rows[i - 1];
        strcpy(L3.rows[0].name, "WD-20270101-000000.csv");
        L3.n++;
        filesRescanned(n, L3, keep);
        filesResult(n, false, 4000);
        check("failed delete: selection follows the same file", n.fileSel == ki + 1 &&
              n.fileSel >= 0 && !strcmp(L3.rows[n.fileSel].name, keep));
        check("result FAILED", n.fileResult == FR_FAILED);
        check("fileIndexOf is case-insensitive", fileIndexOf(L3, "wd-20270101-000000.CSV") == 0);

        // Rescans close an open confirm.
        tap(n, DELX, DELY, 9000);
        check("(confirm up)", n.fileConfirm);
        filesRescanned(n, L3, keep);
        check("a rescan closes the confirm", !n.fileConfirm);

        // Down to nothing.
        FileList E; E.card = true;
        filesRescanned(n, E, keep);
        check("empty list: no selection, scroll 0", n.fileSel == -1 && n.scroll == 0 && n.filesN == 0);
    }

    // ---- publish --------------------------------------------------------------------
    {
        FileList L = makeList(64);
        L.found = 80; L.space = true; L.usedKB = 3250585; L.totalKB = 31250000;
        Nav n = openWith(64, L);
        n.scroll = 59;
        n.fileSel = 61;
        filesSetActive(n, 62);
        State s;
        filesPublish(s, n, L);
        check("publish: window of 5 from scroll 59", s.nFileRows == 5 && s.fileScroll == 59 &&
              !strcmp(s.fileRows[0].name, L.rows[59].name) && !strcmp(s.fileRows[4].name, L.rows[63].name));
        check("publish: counts, selection, active, space", s.fileTotal == 64 && s.fileFound == 80 &&
              s.fileSel == 61 && s.fileActive == 62 && !strcmp(s.fileSelName, L.rows[61].name) &&
              s.fileSelSize == L.rows[61].size && s.fileCard && s.fileSpace && s.cardUsedKB == 3250585);
        FileList E;
        Nav m;
        State t;
        filesPublish(t, m, E);
        check("publish: no card", !t.fileCard && t.nFileRows == 0 && t.fileSelName[0] == 0);
    }

    // ---- rendering ----------------------------------------------------------------------
    {
        static Canvas a, b;
        FileList L = makeList(5);
        Nav n = openWith(5, L);
        State s;
        s.screen = SCR_FILES;
        filesPublish(s, n, L);
        drawFiles(a, s);
        State t = s;
        t.set.labels = 1;
        drawFiles(b, t);
        check("plain labels change only the header", sameOutside(a, b, 0, 0, W, HDR_H));
        check("...and do change it (FILES vs the on-brand name)", differs(a, b, 0, 0, W, HDR_H));

        n.fileSel = 1;
        filesPublish(t = State(), n, L);
        t.screen = SCR_FILES;
        drawFiles(b, t);
        check("selecting a row changes only the rows and the bar",
              sameOutside(a, b, 0, FILE_ROW_Y, W, FILE_BAR_Y + FILE_BAR_H - FILE_ROW_Y));

        State c1 = t;
        static Canvas cc;
        c1.fileConfirm = true;
        drawFiles(cc, c1);
        check("the confirm draws only inside its card", sameOutside(b, cc, FILE_CARD_X, FILE_CARD_Y, FILE_CARD_W, FILE_CARD_H));

        // The active file selected: DELETE drawn disabled, so the bar differs
        // from the same row selected while not active.
        State ac = t;
        ac.fileActive = 1;
        static Canvas ca;
        drawFiles(ca, ac);
        check("active selected: DELETE looks different (disabled)",
              differs(b, ca, FILE_DEL_X, FILE_BAR_Y, FILE_DEL_W, FILE_BAR_H));

        // No card and no logs say different things in the same place.
        State nc; nc.screen = SCR_FILES;
        State nl; nl.screen = SCR_FILES; nl.fileCard = true;
        static Canvas c2, c3;
        drawFiles(c2, nc);
        drawFiles(c3, nl);
        check("NO CARD and NO LOGS render differently", differs(c2, c3, 0, FILE_ROW_Y, W, FILE_VIS * FILE_ROW_H));

        // Every built-in theme draws it (no crash, and themes differ).
        bool themesDiffer = true;
        static Canvas t0, t1;
        for (int th = 0; th < 3; th++) {
            pal::setTheme((uint8_t)th);
            s.set.theme = (uint8_t)th;
            drawFiles(th == 0 ? t0 : t1, s);
            if (th > 0 && !differs(t0, t1, 0, 0, W, H)) themesDiffer = false;
        }
        pal::setTheme(pal::TH_UMBRAVOX);
        check("each built-in theme renders FILES in its own colours", themesDiffer);

        // Dirty tracking: the body repaints when the selection moves.
        Dirty d;
        State p = s; p.set.theme = 0;
        drawFiles(a, p, &d);          // first: full
        drawFiles(a, p, &d);
        bool bodyNow = false;
        for (int i = 0; i < d.n; i++) if (d.r[i].y <= FILE_ROW_Y && d.r[i].h >= FILE_VIS * FILE_ROW_H) bodyNow = true;
        check("dirty: nothing changed, body not repainted", !bodyNow);
        p.fileSel = 3;
        drawFiles(a, p, &d);
        bool body = false;
        for (int i = 0; i < d.n; i++) if (d.r[i].y <= FILE_ROW_Y && d.r[i].y + d.r[i].h >= FILE_BAR_Y + FILE_BAR_H) body = true;
        check("dirty: a selection change repaints the body", body);
    }

    // ---- filescan: which files, what order, and the SDDEL policy ------------------------
    {
        check("log: /WD-0001.csv", filescan::isLog("/WD-0001.csv"));
        check("log: /WD-0001.CSV (older firmware)", filescan::isLog("/WD-0001.CSV"));
        check("log: /wd-20260127-153012.csv", filescan::isLog("/wd-20260127-153012.csv"));
        check("not a log: /diag.log", !filescan::isLog("/diag.log"));
        check("not a log: /perf.csv", !filescan::isLog("/perf.csv"));
        check("not a log: /themes", !filescan::isLog("/themes"));
        check("not a log: /themes/x/theme.json", !filescan::isLog("/themes/x/theme.json"));
        check("not a log: /WD-.csv", !filescan::isLog("/WD-.csv"));
        check("not a log: /WD-12a.csv", !filescan::isLog("/WD-12a.csv"));

        const char* act = "/WD-20260127-153012.csv";
        check("active: same name", filescan::isActive("/WD-20260127-153012.csv", act));
        check("active: any case", filescan::isActive("/wd-20260127-153012.CSV", act));
        check("active: another log is not", !filescan::isActive("/WD-0001.csv", act));
        check("active: nothing when idle", !filescan::isActive("/WD-0001.csv", ""));
        check("delete: refuses the active session", !filescan::deletable("/WD-20260127-153012.csv", act));
        check("delete: allows another log", filescan::deletable("/WD-0001.csv", act));
        check("delete: refuses a theme file (SDDEL would allow it)", !filescan::deletable("/themes/x/theme.json", ""));
        check("delete: refuses diag.log", !filescan::deletable("/diag.log", ""));

        // The same verdict as SDDEL for every log: refused exactly when SDDEL
        // refuses it, with SDDEL's own reason.
        const char* paths[] = {"/WD-0001.csv", "/WD-20260127-153012.csv", "/wd-20260127-153012.CSV",
                               "/WD-9999.CSV", "/WD-1-2-3.csv", "/WD-0001.txt", "/x/WD-0001.csv"};
        bool agree = true;
        for (const char* p : paths) {
            sdcmd::Path q;
            const char* a = sdcmd::delPolicy(p, act, q);
            const char* b = filescan::deleteCheck(p, act);
            if (filescan::isLog(p) && a != b) agree = false;
            if (!filescan::isLog(p) && b == nullptr) agree = false;
        }
        check("deleteCheck agrees with sdcmd::delPolicy on every log", agree);

        // Order.
        check("order: later stamp is newer", filescan::newer("WD-20260128-000000.csv", "WD-20260127-235959.csv"));
        check("order: dated above dateless", filescan::newer("WD-20200101-000000.csv", "WD-9999.csv"));
        check("order: dateless by number", filescan::newer("WD-0010.csv", "WD-0009.CSV"));
        check("order: dateless numbers, not text", filescan::newer("WD-10000.csv", "WD-9999.csv"));

        // The walk.
        FileList L;
        filescan::Walk w;
        filescan::begin(w, L, 0);
        filescan::visit(w, "themes", true, 0, 1);
        filescan::visit(w, "diag.log", false, 50, 1);
        filescan::visit(w, "perf.csv", false, 50, 1);
        filescan::visit(w, "WD-0002.csv", false, 20, 2);
        filescan::visit(w, "WD-20260127-153012.csv", false, 30, 3);
        filescan::visit(w, "WD-0001.CSV", false, 10, 4);
        filescan::visit(w, "WD-20260127-153012-1234567.csv", false, 10, 4);   // 30 chars: too long for a row
        check("walk: only logs, newest first", L.n == 3 && L.found == 3 &&
              !strcmp(L.rows[0].name, "WD-20260127-153012.csv") && !strcmp(L.rows[1].name, "WD-0002.csv") &&
              !strcmp(L.rows[2].name, "WD-0001.CSV") && L.rows[0].size == 30 && L.card && !L.partial);
        check("walk: active index", filescan::activeIndex(L, "/WD-0002.csv") == 1 &&
              filescan::activeIndex(L, "") == -1 && filescan::activeIndex(L, "/WD-7777.csv") == -1);

        // 100 logs in scrambled order: the newest 64 kept, in order.
        filescan::begin(w, L, 0);
        for (int i = 0; i < 100; i++) {
            const int k = (i * 37) % 100;          // a permutation of 0..99
            char nm[FILE_NAME_MAX];
            snprintf(nm, sizeof nm, "WD-%04d.csv", k);
            filescan::visit(w, nm, false, (uint32_t)k, 5);
        }
        bool sorted = L.n == 64 && L.found == 100;
        for (int i = 0; i < L.n && sorted; i++) {
            char nm[FILE_NAME_MAX];
            snprintf(nm, sizeof nm, "WD-%04d.csv", 99 - i);
            if (strcmp(L.rows[i].name, nm) || L.rows[i].size != (uint32_t)(99 - i)) sorted = false;
        }
        check("walk: 100 logs keep the newest 64, newest first", sorted);

        // Bounds: the 513th entry stops it; so does the time budget.
        filescan::begin(w, L, 0);
        bool went = true;
        uint32_t n = 0;
        for (; n < 600 && went; n++) went = filescan::visit(w, "x.bin", false, 1, 10);
        check("walk: stops at 512 entries, partial", n == filescan::MAX_ENTRIES + 1 && L.partial);
        filescan::begin(w, L, 100);
        const bool a1 = filescan::visit(w, "WD-0001.csv", false, 1, 100 + filescan::BUDGET_MS - 1);
        const bool a2 = filescan::visit(w, "WD-0002.csv", false, 1, 100 + filescan::BUDGET_MS);
        check("walk: stops at the time budget, partial", a1 && !a2 && L.partial && L.n == 1);
        filescan::begin(w, L, 0xFFFFFF00u);
        check("walk: budget is wrap-safe", filescan::visit(w, "WD-0001.csv", false, 1, 0x10u));
    }


    // ---- fix round 1 --------------------------------------------------------------------
    // Card space: queried once at boot, refreshed after a delete only while
    // refreshes are cheap; "--" until known, "~" once it is an estimate.
    {
        filescan::SpaceCache c;
        FileList L;
        filescan::spaceInto(c, L);
        check("space: unknown before the boot query", !L.space && !L.spaceApprox);
        check("space: no refresh before the boot query", !filescan::spaceWantRefresh(c));
        filescan::spaceBoot(c, false, 0, 0, 20);
        filescan::spaceInto(c, L);
        check("space: a failed boot query stays unknown, never refreshed",
              !L.space && !filescan::spaceWantRefresh(c));
        c = filescan::SpaceCache();
        filescan::spaceBoot(c, true, 3000000, 31000000, 4200);   // a slow FAT scan at boot
        filescan::spaceInto(c, L);
        check("space: boot query known and exact even when slow", L.space && !L.spaceApprox &&
              L.usedKB == 3000000 && L.totalKB == 31000000);
        check("space: a slow BOOT query does not stop refreshes (FatFs caches it)", filescan::spaceWantRefresh(c));
        filescan::spaceRefreshed(c, true, 2999000, 31000000, 12);
        filescan::spaceInto(c, L);
        check("space: a fast refresh updates and stays exact", L.usedKB == 2999000 && !L.spaceApprox &&
              filescan::spaceWantRefresh(c));
        filescan::spaceRefreshed(c, true, 2998000, 31000000, filescan::SPACE_SLOW_MS);
        check("space: a refresh of exactly 500 ms is still fine", filescan::spaceWantRefresh(c));
        filescan::spaceRefreshed(c, true, 2997000, 31000000, filescan::SPACE_SLOW_MS + 1);
        filescan::spaceInto(c, L);
        check("space: a refresh over 500 ms stops refreshing, numbers marked ~",
              !filescan::spaceWantRefresh(c) && L.space && L.spaceApprox && L.usedKB == 2997000);
        filescan::spaceDeleted(c, 10 * 1024 + 1);
        filescan::spaceInto(c, L);
        check("space: without refresh a delete is estimated (used - 11 KiB)", L.usedKB == 2997000 - 11 && L.spaceApprox);
        filescan::spaceDeleted(c, 0xFFFFFFFFu);
        filescan::spaceInto(c, L);
        check("space: the estimate never goes below 0", L.usedKB == 0);
        filescan::SpaceCache f;
        filescan::spaceBoot(f, true, 100, 1000, 5);
        filescan::spaceRefreshed(f, false, 0, 0, 5);
        filescan::spaceInto(f, L);
        check("space: a failed refresh keeps the cache, marks ~, stops refreshing",
              L.space && L.spaceApprox && L.usedKB == 100 && !filescan::spaceWantRefresh(f));

        State st;
        st.fileCard = true;
        char b[20];
        filesSpaceText(st, false, b);
        check("space text: -- until known", !strcmp(b, "-- FREE"));
        filesSpaceText(st, true, b);
        check("space text: -- USED until known", !strcmp(b, "-- USED"));
        st.fileSpace = true; st.cardUsedKB = 3250585; st.cardTotalKB = 31166976;
        filesSpaceText(st, false, b);
        check("space text: free exact", !strcmp(b, "27G FREE"));
        st.fileSpaceApprox = true;
        filesSpaceText(st, true, b);
        check("space text: ~ when estimated", !strcmp(b, "~3.1G USED"));
        st.fileCard = false;
        filesSpaceText(st, true, b);
        check("space text: nothing with no card", b[0] == 0);
        char tl[2] = {'~', 0};
        check("font: '~' has a glyph", Font::row(tl[0], 1) != 0 || Font::row(tl[0], 2) != 0 || Font::row(tl[0], 3) != 0);
    }
    // In-place reset: no 1.8 KB temporary on the loop stack, and every field zero.
    {
        FileList L = makeList(64);
        L.partial = L.space = L.spaceApprox = true; L.usedKB = 5; L.totalKB = 9; L.found = 99;
        filescan::reset(L);
        bool zero = L.n == 0 && L.found == 0 && !L.card && !L.partial && !L.space && !L.spaceApprox &&
                    L.usedKB == 0 && L.totalKB == 0;
        for (int i = 0; i < MAX_FILE_ROWS && zero; i++) zero = L.rows[i].name[0] == 0 && L.rows[i].size == 0;
        check("reset() zeroes the list in place", zero);
        filescan::Walk w;
        L = makeList(3);
        filescan::begin(w, L, 0);
        check("begin() empties the list and marks the card reached", L.n == 0 && L.card && !L.rows[0].name[0]);
    }
    // Removal goes through SDDEL's sequence: policy, exists, not a directory, remove.
    {
        gRemoved = 0;
        const filescan::RemoveIo io{fEx, fDir, fRm};
        check("remove: a plain log", filescan::removeLog("/WD-0001.csv", "", io) == filescan::RM_OK && gRemoved == 1);
        check("remove: never the active session", filescan::removeLog("/WD-0002.csv", "/WD-0002.csv", io) == filescan::RM_REFUSED);
        check("remove: never a non-log", filescan::removeLog("/diag.log", "", io) == filescan::RM_REFUSED);
        check("remove: missing file", filescan::removeLog("/WD-0404.csv", "", io) == filescan::RM_MISSING);
        check("remove: a directory with a log's name is never removed",
              filescan::removeLog("/WD-0007.csv", "", io) == filescan::RM_DIR);
        check("remove: the card refused", filescan::removeLog("/WD-0500.csv", "", io) == filescan::RM_FAILED);
        check("remove: only the plain log and the refused one reached remove()", gRemoved == 2);
    }
    // The footer's log control is inert on FILES: drawn DIM, and a tap there
    // neither toggles logging nor does anything else.
    {
        static Canvas fa, fb, ref;
        State st; st.sdOk = true; st.logState = 0;
        st.screen = SCR_FILES; drawFooter(fa, st);
        st.screen = SCR_ATTUNE; drawFooter(fb, st);
        const int ty = FOOT_Y + 5, tx = LOG_X + 12;
        ref.clear(pal::PANEL);
        ref.text(tx, ty, "START", pal::DIM);
        check("footer on FILES: START drawn DIM", sameRegion(fa, ref, tx, ty, 30, 7));
        check("footer elsewhere unchanged (not DIM)", !sameRegion(fb, ref, tx, ty, 30, 7));
        FileList L = makeList(5);
        Nav n = openWith(5, L);
        tap(n, LOG_X + LOG_W / 2, LOG_Y + LOG_H / 2, 2000);
        check("tap on the footer control on FILES: no log toggle, still FILES",
              !n.logToggleRequested && n.screen == SCR_FILES && n.fileSel == -1);
    }
    // A bounded walk says so in words.
    {
        State st; st.fileCard = true; st.fileTotal = 64; st.fileFound = 80;
        char b[24];
        filesCountText(st, b);
        check("count: 64/80 LOGS", !strcmp(b, "64/80 LOGS"));
        st.filePartial = true;
        filesCountText(st, b);
        check("count: a bounded walk reads LIST INCOMPLETE", !strcmp(b, "LIST INCOMPLETE"));
        st.filePartial = false; st.fileTotal = 1; st.fileFound = 1;
        filesCountText(st, b);
        check("count: 1 LOG", !strcmp(b, "1 LOG"));
        st.fileTotal = 0; st.fileFound = 0;
        filesCountText(st, b);
        check("count: 0 LOGS", !strcmp(b, "0 LOGS"));
    }
    // The confirm names the log to the second; row labels never reach REC.
    {
        char b[FILE_NAME_MAX + 4];
        fileLabelLong("WD-20260127-153012.csv", b, sizeof b);
        check("confirm label: seconds shown", !strcmp(b, "2026-01-27 15:30:12"));
        fileLabelLong("WD-0012.CSV", b, sizeof b);
        check("confirm label: dateless is the name", !strcmp(b, "WD-0012"));
        check("row label fits before REC", FILE_ROW_X + 2 + FILE_LABEL_MAX * Font::ADV <= FILE_REC_X);

        static Canvas ra, rb;
        FileList L = makeList(1);
        strcpy(L.rows[0].name, "WD-1-2-3-4-5-6-7-8.csv");   // an 18-character label
        FileList S = makeList(1);
        strcpy(S.rows[0].name, "WD-1.csv");
        Nav n = openWith(1, L);
        State a, b2;
        a.screen = b2.screen = SCR_FILES;
        filesPublish(a, n, L);
        filesPublish(b2, n, S);
        drawFiles(ra, a);
        drawFiles(rb, b2);
        check("a long dateless label stops short of the REC column",
              sameRegion(ra, rb, FILE_REC_X - 2, FILE_ROW_Y, 22, FILE_ROW_H - 1));
    }

    printf(fails ? "\n%d FAILED\n" : "\nall passed\n", fails);
    return fails ? 1 : 0;
}
