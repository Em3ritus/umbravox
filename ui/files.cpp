// FILES -- the interaction state machine (see files.h). Pure: no clock of its
// own (filesTick hands it the caller's), no card (nav raises requests).
#include "files.h"
#include "layout.h"
#include <string.h>

namespace ui {
namespace {

bool hit(int x, int y, int rx, int ry, int rw, int rh) {
    return x >= rx && x < rx + rw && y >= ry && y < ry + rh;
}

char lc(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c; }

bool ieq(const char* a, const char* b) {
    for (;; a++, b++) {
        if (lc(*a) != lc(*b)) return false;
        if (!*a) return true;
    }
}

void cancelConfirm(Nav& n) { n.fileConfirm = false; }

void clampScroll(Nav& n) {
    const int16_t m = filesMaxScroll(n);
    n.maxScroll = m;
    if (n.scroll > m) n.scroll = m;
    if (n.scroll < 0) n.scroll = 0;
}

void putU(char* out, int& i, uint32_t v) {
    char t[12];
    int k = 0;
    do { t[k++] = (char)('0' + v % 10); v /= 10; } while (v);
    while (k) out[i++] = t[--k];
}

// The smallest unit whose value is under 1000; one decimal under 10.
void fmtUnits(uint64_t bytes, char* out) {
    static const char U[] = {'B', 'K', 'M', 'G', 'T', 'P'};
    int i = 0;
    if (bytes < 1000) {
        putU(out, i, (uint32_t)bytes);
        out[i++] = 'B';
        out[i] = 0;
        return;
    }
    uint64_t div = 1024;
    for (int k = 1; k < 6; k++, div *= 1024) {
        const uint64_t tenths = (bytes * 10 + div / 2) / div;
        if (tenths < 100) {
            putU(out, i, (uint32_t)(tenths / 10));
            out[i++] = '.';
            out[i++] = (char)('0' + tenths % 10);
            out[i++] = U[k];
            out[i] = 0;
            return;
        }
        const uint64_t whole = (tenths + 5) / 10;
        if (whole < 1000 || k == 5) {
            putU(out, i, (uint32_t)whole);
            out[i++] = U[k];
            out[i] = 0;
            return;
        }
    }
}

} // namespace

void filesTick(Nav& n, uint32_t nowMs) {
    n.nowMs = nowMs;
    if (n.fileConfirm && nowMs - n.fileConfirmAt >= CONFIRM_MS) cancelConfirm(n);
    if (n.fileResult != FR_NONE && nowMs - n.fileResultAt >= RESULT_MS) n.fileResult = FR_NONE;
}

void filesEnter(Nav& n) {
    n.screen = SCR_FILES;
    n.fileSel = -1;
    n.fileConfirm = false;
    n.fileResult = FR_NONE;
    n.fileDeleteRequested = false;
    n.fileDeleteIdx = -1;
    n.scroll = 0;
    n.dragAccum = 0;
    n.filesScanRequested = true;
}

void filesLeave(Nav& n) {
    cancelConfirm(n);
    n.fileSel = -1;
    n.screen = SCR_ATTUNE;
    n.setPage = 1;          // back one step: the page FILES was opened from
    n.scroll = 0;
    n.dragAccum = 0;
}

int16_t filesMaxScroll(const Nav& n) {
    return (int16_t)(n.filesN > FILE_VIS ? n.filesN - FILE_VIS : 0);
}

bool filesTap(Nav& n, int x, int y) {
    if (n.fileConfirm) {
        // Inside the arm time a tap means nothing: a bounce of DELETE must
        // neither confirm nor cancel.
        if (n.nowMs - n.fileConfirmAt < CONFIRM_ARM_MS) return false;
        cancelConfirm(n);
        if (!hit(x, y, FILE_YES_X, FILE_YES_Y, FILE_YES_W, FILE_YES_H)) return false;   // NO
        if (n.fileSel < 0 || n.fileSel >= n.filesN || n.fileSel == n.filesActive) return false;
        n.fileDeleteRequested = true;
        n.fileDeleteIdx = n.fileSel;
        return false;
    }
    if (hit(x, y, FILE_DEL_X, FILE_BAR_Y, FILE_DEL_W, FILE_BAR_H)) {
        if (n.fileSel >= 0 && n.fileSel < n.filesN && n.fileSel != n.filesActive) {
            n.fileConfirm = true;
            n.fileConfirmAt = n.nowMs;
        }
        return false;
    }
    if (hit(x, y, FILE_ROW_X, FILE_ROW_Y, FILE_ROW_W, FILE_VIS * FILE_ROW_H)) {
        const int idx = n.scroll + (y - FILE_ROW_Y) / FILE_ROW_H;
        if (idx >= n.filesN) return false;
        n.fileSel = (int8_t)(n.fileSel == idx ? -1 : idx);
    }
    return false;
}

void filesRescanned(Nav& n, const FileList& L, const char* prevSel) {
    n.filesN = L.n;
    cancelConfirm(n);
    n.fileSel = (int8_t)((prevSel && prevSel[0]) ? fileIndexOf(L, prevSel) : -1);
    if (n.filesActive >= n.filesN) n.filesActive = -1;
    clampScroll(n);
}

void filesSetActive(Nav& n, int idx) {
    n.filesActive = (int8_t)(idx < 0 || idx >= n.filesN ? -1 : idx);
    if (n.fileConfirm && n.fileSel == n.filesActive && n.filesActive >= 0) cancelConfirm(n);
}

void filesResult(Nav& n, bool ok, uint32_t nowMs) {
    n.fileResult = ok ? FR_DELETED : FR_FAILED;
    n.fileResultAt = nowMs;
}

void fileLabel(const char* name, char* out, int cap) {
    if (cap <= 0) return;
    char tmp[FILE_NAME_MAX + 8];
    if (name[0] == '/') name++;
    const size_t len = strlen(name);
    bool dated = len == 22 && lc(name[0]) == 'w' && lc(name[1]) == 'd' && name[2] == '-' &&
                 name[11] == '-' && ieq(name + 18, ".csv");
    for (int i = 3; dated && i < 18; i++)
        if (i != 11 && (name[i] < '0' || name[i] > '9')) dated = false;
    int k = 0;
    if (dated) {
        // YYYY-MM-DD HH:MM from WD-YYYYMMDD-HHMMSS.csv
        const char* d = name + 3;
        const char* t = name + 12;
        for (int i = 0; i < 4; i++) tmp[k++] = d[i];
        tmp[k++] = '-'; tmp[k++] = d[4]; tmp[k++] = d[5];
        tmp[k++] = '-'; tmp[k++] = d[6]; tmp[k++] = d[7];
        tmp[k++] = ' '; tmp[k++] = t[0]; tmp[k++] = t[1];
        tmp[k++] = ':'; tmp[k++] = t[2]; tmp[k++] = t[3];
    } else {
        size_t n = len;
        if (n > 4 && ieq(name + n - 4, ".csv")) n -= 4;
        for (size_t i = 0; i < n && k < (int)sizeof tmp - 1; i++) tmp[k++] = name[i];
    }
    tmp[k] = 0;
    int i = 0;
    for (; i < cap - 1 && tmp[i]; i++) out[i] = tmp[i];
    out[i] = 0;
}

void fileLabelLong(const char* name, char* out, int cap) {
    if (cap <= 0) return;
    char tmp[FILE_NAME_MAX + 8];
    fileLabel(name, tmp, sizeof tmp);
    const char* n = name[0] == '/' ? name + 1 : name;
    // A dated label is 16 characters; its seconds are the name's 17th..18th.
    if (strlen(tmp) == 16 && tmp[13] == ':' && strlen(n) == 22) {
        tmp[16] = ':'; tmp[17] = n[16]; tmp[18] = n[17]; tmp[19] = 0;
    }
    int i = 0;
    for (; i < cap - 1 && tmp[i]; i++) out[i] = tmp[i];
    out[i] = 0;
}

void filesCardText(const State& s, char* out) {
    out[0] = 0;
    if (!s.fileCard || !s.cardMB) return;
    fmtUnits((uint64_t)s.cardMB << 20, out);
    const char* w = " CARD";
    int i = (int)strlen(out);
    while (*w) out[i++] = *w++;
    out[i] = 0;
}

void filesLogsText(const State& s, char* out) {
    out[0] = 0;
    if (!s.fileCard || !s.fileFound) return;
    fmtKB(s.fileLogKB, out);
    if (s.filePartial) { const int i = (int)strlen(out); out[i] = '+'; out[i + 1] = 0; }
}

void filesCountText(const State& s, char* out) {
    int i = 0;
    if (s.filePartial) {
        const char* m = "LIST INCOMPLETE";
        while (*m) out[i++] = *m++;
        out[i] = 0;
        return;
    }
    putU(out, i, s.fileTotal);
    if (s.fileFound > s.fileTotal) { out[i++] = '/'; putU(out, i, s.fileFound); }
    const char* w = (s.fileTotal == 1 && s.fileFound <= 1) ? " LOG" : " LOGS";
    while (*w) out[i++] = *w++;
    out[i] = 0;
}

void fmtBytes(uint32_t b, char* out) { fmtUnits(b, out); }
void fmtKB(uint32_t kb, char* out) {
    if (kb == 0) { out[0] = '0'; out[1] = 'K'; out[2] = 0; return; }
    fmtUnits((uint64_t)kb * 1024, out);
}

int fileIndexOf(const FileList& L, const char* name) {
    if (!name || !name[0]) return -1;
    if (name[0] == '/') name++;
    for (int i = 0; i < L.n; i++) if (ieq(L.rows[i].name, name)) return i;
    return -1;
}

void filesPublish(State& s, const Nav& n, const FileList& L) {
    s.fileTotal   = L.n;
    s.fileFound   = L.found;
    s.fileCard    = L.card;
    s.filePartial = L.partial;
    s.cardMB      = L.cardMB;
    s.fileLogKB   = L.logKB;
    int sc = n.scroll;
    const int m = L.n > FILE_VIS ? L.n - FILE_VIS : 0;
    if (sc > m) sc = m;
    if (sc < 0) sc = 0;
    s.fileScroll = (int16_t)sc;
    s.nFileRows = 0;
    for (int i = 0; i < FILE_VIS && sc + i < L.n; i++) {
        s.fileRows[i] = L.rows[sc + i];
        s.nFileRows++;
    }
    s.fileSel    = (int8_t)(n.fileSel >= 0 && n.fileSel < L.n ? n.fileSel : -1);
    s.fileActive = (int8_t)(n.filesActive >= 0 && n.filesActive < L.n ? n.filesActive : -1);
    memset(s.fileSelName, 0, sizeof s.fileSelName);
    s.fileSelSize = 0;
    if (s.fileSel >= 0) {
        memcpy(s.fileSelName, L.rows[s.fileSel].name, sizeof s.fileSelName - 1);
        s.fileSelSize = L.rows[s.fileSel].size;
    }
    s.fileConfirm = n.fileConfirm && s.fileSel >= 0;
    s.fileResult  = n.fileResult;
}

} // namespace ui
