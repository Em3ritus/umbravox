// Bounded recursive-descent parser for layout.json. See themelayout.h.
// Follows ui/themepack.cpp's reader on purpose (same limits and failure style)
// but keeps its own copy, so the shipped theme.json parser is left untouched.
#include "themelayout.h"
#include <string.h>
#include <stdio.h>

namespace themelayout {

const char* const PIECE_NAME[P_N] = {
    "menu", "wordmark", "status.gps", "status.nodes", "status.power", "bridge", "sd",
    "nav.scan", "nav.domain", "nav.codex", "nav.poi", "stop",
    "count.wifi", "count.5g", "count.ble"};

namespace {

constexpr int MAX_DEPTH = 3;   // the top object, a board object, a rect array

struct P {
    const char* s;
    size_t n;
    size_t i;
    const char* err;
    char msg[64];
};

bool fail(P& p, const char* why) {
    if (!p.err) p.err = why;
    return false;
}

bool failPiece(P& p, const char* piece, const char* what) {
    if (p.err) return false;
    snprintf(p.msg, sizeof p.msg, "%s: %s", piece, what);
    p.err = p.msg;
    return false;
}

void ws(P& p) {
    while (p.i < p.n && (p.s[p.i] == ' ' || p.s[p.i] == '\t' || p.s[p.i] == '\n' || p.s[p.i] == '\r')) p.i++;
}

bool eat(P& p, char c) {
    ws(p);
    if (p.i < p.n && p.s[p.i] == c) { p.i++; return true; }
    return false;
}

bool peek(P& p, char c) {
    ws(p);
    return p.i < p.n && p.s[p.i] == c;
}

// A string with no escapes and no control bytes; up to cap chars into buf.
// *len receives the full length, so a longer string is detectable.
bool str(P& p, char* buf, size_t cap, size_t* len) {
    ws(p);
    if (p.i >= p.n || p.s[p.i] != '"') return fail(p, "expected string");
    p.i++;
    size_t k = 0;
    while (p.i < p.n) {
        unsigned char c = (unsigned char)p.s[p.i];
        if (c == '"') { p.i++; if (len) *len = k; if (buf) buf[k <= cap ? k : cap] = 0; return true; }
        if (c == '\\') return fail(p, "string escapes not supported");
        if (c < 32 || c > 126) return fail(p, "bad character in string");
        if (buf && k < cap) buf[k] = (char)c;
        k++;
        p.i++;
    }
    return fail(p, "unterminated string");
}

bool isScript(const char* k, size_t kl) { return kl == 6 && memcmp(k, "script", 6) == 0; }

bool skipValue(P& p, int depth);

bool skipContainer(P& p, int depth, char close) {
    if (depth >= MAX_DEPTH) return fail(p, "nested too deep");
    const bool object = (close == '}');
    p.i++;  // the opener
    if (eat(p, close)) return true;
    for (;;) {
        if (object) {
            char k[8]; size_t kl = 0;
            if (!str(p, k, 7, &kl)) return false;
            if (isScript(k, kl)) return fail(p, "script pieces are not supported");
            if (!eat(p, ':')) return fail(p, "expected ':'");
        }
        if (!skipValue(p, depth + 1)) return false;
        if (eat(p, ',')) continue;
        if (eat(p, close)) return true;
        return fail(p, "expected ',' or end");
    }
}

bool skipValue(P& p, int depth) {
    ws(p);
    if (p.i >= p.n) return fail(p, "unexpected end");
    char c = p.s[p.i];
    if (c == '"') return str(p, nullptr, 0, nullptr);
    if (c == '{') return skipContainer(p, depth, '}');
    if (c == '[') return skipContainer(p, depth, ']');
    if (c == '-' || (c >= '0' && c <= '9')) {
        while (p.i < p.n && (p.s[p.i] == '-' || p.s[p.i] == '+' || p.s[p.i] == '.' || p.s[p.i] == 'e' ||
                             p.s[p.i] == 'E' || (p.s[p.i] >= '0' && p.s[p.i] <= '9')))
            p.i++;
        return true;
    }
    static const char* const lits[] = {"true", "false", "null"};
    for (const char* l : lits) {
        size_t m = strlen(l);
        if (p.n - p.i >= m && memcmp(p.s + p.i, l, m) == 0) { p.i += m; return true; }
    }
    return fail(p, "bad value");
}

// A signed integer in int16 range. No fraction or exponent.
bool int16v(P& p, int16_t& out) {
    ws(p);
    bool neg = false;
    if (p.i < p.n && p.s[p.i] == '-') { neg = true; p.i++; }
    size_t st = p.i;
    long v = 0;
    while (p.i < p.n && p.s[p.i] >= '0' && p.s[p.i] <= '9') {
        if (v < 1000000) v = v * 10 + (p.s[p.i] - '0');
        p.i++;
    }
    if (p.i == st) return fail(p, "expected integer");
    if (p.i < p.n && (p.s[p.i] == '.' || p.s[p.i] == 'e' || p.s[p.i] == 'E')) return fail(p, "expected integer");
    if (neg) v = -v;
    if (v < -32768 || v > 32767) return fail(p, "number out of range");
    out = (int16_t)v;
    return true;
}

bool slotValue(P& p, const char* piece, Slot& out) {
    ws(p);
    if (peek(p, '"')) {
        char v[8]; size_t vl = 0;
        if (!str(p, v, 7, &vl)) return false;
        if (vl == 6 && memcmp(v, "hidden", 6) == 0) { out.how = SLOT_HIDDEN; return true; }
        return failPiece(p, piece, "expected [x, y, w, h] or \"hidden\"");
    }
    if (!eat(p, '[')) return failPiece(p, piece, "expected [x, y, w, h] or \"hidden\"");
    int16_t v[4];
    for (int k = 0; k < 4; k++) {
        if (k && !eat(p, ',')) return failPiece(p, piece, "expected four numbers");
        if (!int16v(p, v[k])) return false;
    }
    if (!eat(p, ']')) return failPiece(p, piece, "expected four numbers");
    if (v[2] <= 0 || v[3] <= 0) return failPiece(p, piece, "width and height must be positive");
    out.how = SLOT_PLACED; out.x = v[0]; out.y = v[1]; out.w = v[2]; out.h = v[3];
    return true;
}

// A board object: piece name -> slot. Unknown pieces are skipped (later formats add some).
bool board(P& p, Slot (&slots)[P_N], int depth) {
    if (!peek(p, '{')) return fail(p, "a board must be an object");
    if (depth >= MAX_DEPTH) return fail(p, "nested too deep");
    p.i++;
    bool seen[P_N] = {};
    if (eat(p, '}')) return true;
    for (;;) {
        char k[16]; size_t kl = 0;
        if (!str(p, k, 15, &kl)) return false;
        if (!eat(p, ':')) return fail(p, "expected ':'");
        if (isScript(k, kl)) return fail(p, "script pieces are not supported");
        int piece = -1;
        if (kl <= 15)
            for (int j = 0; j < P_N; j++)
                if (strcmp(k, PIECE_NAME[j]) == 0) { piece = j; break; }
        if (piece >= 0) {
            if (seen[piece]) return fail(p, "duplicate piece");
            seen[piece] = true;
            if (!slotValue(p, PIECE_NAME[piece], slots[piece])) return false;
        } else if (!skipValue(p, depth + 1)) {
            return false;
        }
        if (eat(p, ',')) continue;
        if (eat(p, '}')) return true;
        return fail(p, "expected ',' or end");
    }
}

bool parseObject(P& p, Layout& out) {
    memset(&out, 0, sizeof out);
    bool seenFormat = false;
    if (!eat(p, '{')) return fail(p, "not a JSON object");
    if (!eat(p, '}')) {
        for (;;) {
            char k[16]; size_t kl = 0;
            if (!str(p, k, 15, &kl)) return false;
            if (!eat(p, ':')) return fail(p, "expected ':'");
            if (isScript(k, kl)) return fail(p, "script pieces are not supported");
            int b = -1;
            if (kl == 9 && memcmp(k, "waveshare", 9) == 0) b = B_WAVESHARE;
            else if (kl == 3 && memcmp(k, "cyd", 3) == 0) b = B_CYD;
            if (kl == 6 && memcmp(k, "format", 6) == 0) {
                if (seenFormat) return fail(p, "duplicate key");
                seenFormat = true;
                int16_t v;
                if (!int16v(p, v)) return false;
                if (v > FORMAT) return fail(p, "layout format is newer than this firmware");
                if (v < FORMAT) return fail(p, "bad layout format");
            } else if (b >= 0) {
                if (out.has[b]) return fail(p, "duplicate board");
                out.has[b] = true;
                if (!board(p, out.slot[b], 1)) return false;
            } else if (!skipValue(p, 1)) {
                return false;
            }
            if (eat(p, ',')) continue;
            if (eat(p, '}')) break;
            return fail(p, "expected ',' or end");
        }
    }
    if (!seenFormat) return fail(p, "missing layout format");
    return true;
}

}  // namespace

bool parse(const char* json, size_t n, Layout& out, char* why, size_t whyCap) {
    P p{json, n, 0, nullptr, {0}};
    bool ok = false;
    if (!json || n == 0) fail(p, "empty file");
    else if (n > MAX_JSON) fail(p, "layout.json too large");
    else if (parseObject(p, out)) {
        ws(p);
        if (p.i != p.n) fail(p, "trailing data after JSON");
        else ok = true;
    }
    if (!ok && !p.err) p.err = "invalid layout.json";
    if (why && whyCap) {
        if (ok) why[0] = 0;
        else { strncpy(why, p.err, whyCap - 1); why[whyCap - 1] = 0; }
    }
    return ok;
}

}  // namespace themelayout

// ---------- resolve + check ----------------------------------------------------
namespace themelayout {
namespace {

using uieng::Rect;

constexpr int MIN_TAP = 44;
const int BOARD_W[B_N] = {480, 320}, BOARD_H[B_N] = {320, 240};

bool tappable(int p) { return p != P_WORDMARK && p != P_SD; }
bool hideable(int p) { return p == P_WORDMARK || p == P_SD || p >= P_COUNT_WIFI; }
bool isTile(int p) { return p >= P_COUNT_WIFI; }
bool visible(const Rect& r) { return r.w > 0 && r.h > 0; }
bool overlaps(const Rect& a, const Rect& b) {
    return visible(a) && visible(b) && a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h;
}

bool say(char* why, size_t cap, const char* fmt, const char* a = "", const char* b = "") {
    if (why && cap) snprintf(why, cap, fmt, a, b);
    return false;
}

// The largest rectangle inside W x H that no visible rect overlaps. For each pair of
// candidate rows, the rects crossing that band block x intervals; the widest gap wins.
// Ties keep the first found: smallest top, then smallest x.
Rect largestFree(const Rect* r, int n, int W, int H) {
    int ys[2 + 2 * P_N]; int ny = 0;
    ys[ny++] = 0; ys[ny++] = H;
    for (int i = 0; i < n; i++) if (visible(r[i])) { ys[ny++] = r[i].y; ys[ny++] = r[i].y + r[i].h; }
    for (int i = 1; i < ny; i++) for (int j = i; j > 0 && ys[j - 1] > ys[j]; j--) { int t = ys[j]; ys[j] = ys[j - 1]; ys[j - 1] = t; }
    Rect best{0, 0, 0, 0}; long bestArea = 0;
    for (int a = 0; a < ny; a++) {
        if (ys[a] < 0 || (a && ys[a] == ys[a - 1])) continue;
        for (int c = a + 1; c < ny; c++) {
            const int y0 = ys[a], y1 = ys[c];
            if (y1 <= y0 || y1 > H) continue;
            int lo[P_N], hi[P_N], m = 0;           // blocked x intervals, sorted by lo
            for (int i = 0; i < n; i++)
                if (visible(r[i]) && r[i].y < y1 && y0 < r[i].y + r[i].h) {
                    int k = m++; lo[k] = r[i].x; hi[k] = r[i].x + r[i].w;
                    for (; k > 0 && lo[k - 1] > lo[k]; k--) { int t = lo[k]; lo[k] = lo[k - 1]; lo[k - 1] = t; t = hi[k]; hi[k] = hi[k - 1]; hi[k - 1] = t; }
                }
            int x = 0;
            for (int k = 0; k <= m; k++) {
                const int end = (k < m) ? lo[k] : W;
                if (end > x) {
                    const long area = (long)(end - x) * (y1 - y0);
                    if (area > bestArea) { bestArea = area; best = {x, y0, end - x, y1 - y0}; }
                }
                if (k < m && hi[k] > x) x = hi[k];
            }
        }
    }
    return best;
}

}  // namespace

Board boardFor(int W) { return W >= 400 ? B_WAVESHARE : B_CYD; }

Rect defaultRect(Board b, Piece p) {
    const int W = BOARD_W[b], H = BOARD_H[b];
    const uieng::Chrome c = uieng::builtinChrome(W, H);
    switch (p) {
    case P_MENU: return c.menu;
    case P_WORDMARK: return c.wordmark;
    case P_GPS: return c.statusCells[0];
    case P_NODES: return c.statusCells[1];
    case P_POWER: return c.statusCells[2];
    case P_BRIDGE: return c.bridge;
    case P_SD: return c.sd;
    case P_NAV_SCAN: return c.navCells[0];
    case P_NAV_DOMAIN: return c.navCells[1];
    case P_NAV_CODEX: return c.navCells[2];
    case P_NAV_POI: return c.navCells[3];
    case P_STOP: return c.stop;
    default: break;
    }
    return uieng::builtinScan(W, H).tiles[p - P_COUNT_WIFI];
}

bool resolve(const Layout& L, Board b, uieng::ChromeOverride& out, char* why, size_t whyCap) {
    if (b >= B_N) return say(why, whyCap, "bad board");
    const int W = BOARD_W[b], H = BOARD_H[b];
    Rect r[P_N];
    for (int p = 0; p < P_N; p++) {
        const Slot& s = L.slot[b][p];
        const char* name = PIECE_NAME[p];
        if (s.how == SLOT_HIDDEN) {
            if (!hideable(p)) return say(why, whyCap, "%s cannot be hidden", name);
            r[p] = {0, 0, 0, 0};
            continue;
        }
        if (s.how != SLOT_PLACED) { r[p] = defaultRect(b, (Piece)p); continue; }
        r[p] = {s.x, s.y, s.w, s.h};
        if (s.w <= 0 || s.h <= 0) return say(why, whyCap, "%s: width and height must be positive", name);
        if (s.x < 0 || s.y < 0 || s.x + s.w > W || s.y + s.h > H) return say(why, whyCap, "%s is off screen", name);
        if (tappable(p) && (s.w < MIN_TAP || s.h < MIN_TAP))
            return say(why, whyCap, "%s must be at least 44 px each way", name);
        if (!tappable(p)) {   // wordmark and sd draw at fixed offsets: anything smaller would spill outside the piece
            const Rect d = defaultRect(b, (Piece)p);
            if (s.w < d.w || s.h < d.h) return say(why, whyCap, "%s must be at least its built-in size", name);
        }
    }

    // The body: today's size, centred in the largest area the header and footer leave.
    const uieng::Chrome bc = uieng::builtinChrome(W, H);
    const Rect free = largestFree(r, P_COUNT_WIFI, W, H);   // tiles are Scan-only: they do not shape the body
    const int bw = bc.body.w, bh = bc.body.h;
    if (free.w < bw || free.h < bh) return say(why, whyCap, "header and footer leave too little room for the screens");
    const Rect body{free.x + (free.w - bw) / 2, free.y + (free.h - bh) / 2, bw, bh};
    const int dx = body.x - bc.body.x, dy = body.y - bc.body.y;
    const uieng::ScanLayout bs = uieng::builtinScan(W, H);
    auto shifted = [&](Rect q) { return Rect{q.x + dx, q.y + dy, q.w, q.h}; };
    for (int p = P_COUNT_WIFI; p < P_N; p++)
        if (L.slot[b][p].how == SLOT_DEFAULT) r[p] = shifted(bs.tiles[p - P_COUNT_WIFI]);

    const Rect hero = shifted(bs.hero);
    for (int p = 0; p < P_N; p++) {
        for (int q = p + 1; q < P_N; q++)
            if (overlaps(r[p], r[q])) return say(why, whyCap, "%s overlaps %s", PIECE_NAME[p], PIECE_NAME[q]);
        if (isTile(p) && overlaps(r[p], hero)) return say(why, whyCap, "%s overlaps the radar", PIECE_NAME[p]);
    }

    uieng::Chrome& c = out.c;
    c.menu = r[P_MENU]; c.wordmark = r[P_WORDMARK];
    c.statusCells[0] = r[P_GPS]; c.statusCells[1] = r[P_NODES]; c.statusCells[2] = r[P_POWER];
    c.statusCells[3] = c.wordmark;   // the draw-only alias builtinChrome keeps
    c.bridge = r[P_BRIDGE]; c.sd = r[P_SD];
    for (int i = 0; i < 4; i++) c.navCells[i] = r[P_NAV_SCAN + i];
    c.stop = r[P_STOP];
    c.body = body;
    // The bands: the bounding box of the visible header pieces and of the footer pieces.
    auto bbox = [](const Rect* q, const int* idx, int n) {
        int x0 = 1 << 30, y0 = 1 << 30, x1 = -(1 << 30), y1 = -(1 << 30);
        for (int i = 0; i < n; i++) {
            const Rect& e = q[idx[i]];
            if (!visible(e)) continue;
            x0 = e.x < x0 ? e.x : x0;
            y0 = e.y < y0 ? e.y : y0;
            x1 = e.x + e.w > x1 ? e.x + e.w : x1;
            y1 = e.y + e.h > y1 ? e.y + e.h : y1;
        }
        return x1 > x0 ? Rect{x0, y0, x1 - x0, y1 - y0} : Rect{0, 0, 0, 0};
    };
    static const int head[] = {P_MENU, P_WORDMARK, P_GPS, P_NODES, P_POWER, P_BRIDGE, P_SD};
    static const int foot[] = {P_NAV_SCAN, P_NAV_DOMAIN, P_NAV_CODEX, P_NAV_POI, P_STOP};
    c.status = bbox(r, head, 7);
    c.nav = bbox(r, foot, 5);
    for (int i = 0; i < 3; i++) out.tiles[i] = r[P_COUNT_WIFI + i];
    out.hero = hero;
    out.counts = shifted(bs.counts);
    out.scanBody = shifted(bs.body);
    if (why && whyCap) why[0] = 0;
    return true;
}

bool build(const char* json, size_t n, PackLayout& out, char* why, size_t whyCap) {
    out.lo.board[0] = out.lo.board[1] = nullptr;
    if (n > MAX_JSON) {
        if (why && whyCap) snprintf(why, whyCap, "layout.json over %u bytes", (unsigned)MAX_JSON);
        return false;
    }
    Layout L;   // ~300 bytes on the loader task's stack: never in .bss (internal RAM is scarce)
    char r[80];
    if (!parse(json, n, L, r, sizeof r)) {
        if (why && whyCap) snprintf(why, whyCap, "layout.json: %s", r);
        return false;
    }
    for (int b = 0; b < B_N; b++) {
        if (!L.has[b]) continue;
        if (!resolve(L, (Board)b, out.ov[b], r, sizeof r)) {
            if (why && whyCap) snprintf(why, whyCap, "layout.json: %s", r);
            out.lo.board[0] = out.lo.board[1] = nullptr;
            return false;
        }
        out.lo.board[b] = &out.ov[b];
    }
    if (why && whyCap) why[0] = 0;
    return true;
}

}  // namespace themelayout
