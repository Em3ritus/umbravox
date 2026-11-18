// Bounded recursive-descent parser for layout.json. See themelayout.h.
// Follows ui/themepack.cpp's reader on purpose (same limits and failure style)
// but keeps its own copy, so the shipped theme.json parser is left untouched.
#include "themelayout.h"
#include "themepack.h"
#include "contrast.h"
#include <string.h>
#include <stdio.h>

namespace themelayout {

const char* const PIECE_NAME[P_N] = {
    "menu", "wordmark", "status.gps", "status.nodes", "status.power", "bridge", "sd",
    "nav.scan", "nav.domain", "nav.codex", "nav.poi", "stop",
    "count.wifi", "count.5g", "count.ble",
    "radar", "spectrum", "channel", "badge"};

namespace {

constexpr int MAX_DEPTH = 4;   // the top object, a board object, a piece/style object, a rect/slice array

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

// "f3" / "a12": a pack file reference. prefix 'f' (max MAX_FONT_REFS) or 'a' (max MAX_ART_REFS).
bool ref(P& p, const char* piece, char prefix, int8_t& out) {
    char v[8]; size_t vl = 0;
    const int max = prefix == 'f' ? MAX_FONT_REFS : MAX_ART_REFS;
    const char* what = prefix == 'f' ? "bad font reference (f0..f7)" : "bad art reference (a0..a15)";
    if (!peek(p, '"')) return failPiece(p, piece, what);
    if (!str(p, v, 7, &vl)) return false;
    if (vl < 2 || vl > 3 || v[0] != prefix) return failPiece(p, piece, what);
    int n = 0;
    for (size_t i = 1; i < vl; i++) {
        if (v[i] < '0' || v[i] > '9') return failPiece(p, piece, what);
        n = n * 10 + (v[i] - '0');
    }
    if ((vl == 3 && v[1] == '0') || n >= max) return failPiece(p, piece, what);
    out = (int8_t)n;
    return true;
}
int hexDigit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}
// A theme colour role ("bright") or "#rrggbb".
bool colour(P& p, const char* piece, uieng::ColorRef& out) {
    char v[16]; size_t vl = 0;
    if (!peek(p, '"')) return failPiece(p, piece, "bad colour (a role name or #rrggbb)");
    if (!str(p, v, 15, &vl)) return false;
    if (vl == 7 && v[0] == '#') {
        int c[6];
        for (int i = 0; i < 6; i++) if ((c[i] = hexDigit(v[1 + i])) < 0) return failPiece(p, piece, "bad colour (a role name or #rrggbb)");
        const int r = c[0] * 16 + c[1], g = c[2] * 16 + c[3], b = c[4] * 16 + c[5];
        out.kind = 2; out.v = (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
        return true;
    }
    if (vl <= 15)
        for (int i = 0; i < themepack::N_COLORS; i++)
            if (strcmp(v, themepack::ROLE_KEYS[i]) == 0) { out.kind = 1; out.v = (uint16_t)i; return true; }
    return failPiece(p, piece, "bad colour (a role name or #rrggbb)");
}
bool intIn(P& p, const char* piece, int lo, int hi, const char* what, int& out) {
    int16_t v;
    if (!int16v(p, v)) return false;
    if (v < lo || v > hi) return failPiece(p, piece, what);
    out = v;
    return true;
}
bool quad(P& p, const char* piece, const char* what, int lo, int hi, int out[4]) {   // [a,b,c,d]
    if (!eat(p, '[')) return failPiece(p, piece, what);
    for (int k = 0; k < 4; k++) {
        if (k && !eat(p, ',')) return failPiece(p, piece, what);
        if (!intIn(p, piece, lo, hi, what, out[k])) return false;
    }
    if (!eat(p, ']')) return failPiece(p, piece, what);
    return true;
}
bool rectValue(P& p, const char* piece, Slot& out) {
    int v[4];
    if (!quad(p, piece, "expected four numbers", -32768, 32767, v)) return false;
    if (v[2] <= 0 || v[3] <= 0) return failPiece(p, piece, "width and height must be positive");
    out.how = SLOT_PLACED; out.x = (int16_t)v[0]; out.y = (int16_t)v[1]; out.w = (int16_t)v[2]; out.h = (int16_t)v[3];
    return true;
}
bool keyIs(const char* k, size_t kl, const char* name) { return kl == strlen(name) && memcmp(k, name, kl) == 0; }

// Iterates an object's keys: fn(key, len, index-of-known-key) handles each value; unknown keys (index -1) are
// skipped; a key seen twice fails. names lists the known keys (at most 16).
template <class Fn>
bool objectKeys(P& p, const char* piece, int depth, const char* const* names, int nNames, Fn fn) {
    if (!peek(p, '{')) return failPiece(p, piece, "expected an object");
    if (depth >= MAX_DEPTH) return fail(p, "nested too deep");
    p.i++;
    bool seen[16] = {};
    if (eat(p, '}')) return true;
    for (;;) {
        char k[16]; size_t kl = 0;
        if (!str(p, k, 15, &kl)) return false;
        if (!eat(p, ':')) return fail(p, "expected ':'");
        if (isScript(k, kl)) return fail(p, "script pieces are not supported");
        int idx = -1;
        for (int i = 0; i < nNames; i++) if (keyIs(k, kl, names[i])) { idx = i; break; }
        if (idx >= 0) {
            if (seen[idx]) return failPiece(p, piece, "duplicate key");
            seen[idx] = true;
            if (!fn(idx)) return false;
        } else if (!skipValue(p, depth + 1)) {
            return false;
        }
        if (eat(p, ',')) continue;
        if (eat(p, '}')) return true;
        return fail(p, "expected ',' or end");
    }
}

void useFont(uieng::BoardLook& k, int8_t f) { if (f >= 0) k.fontsUsed |= (uint8_t)(1u << f); }
void useArt(uieng::BoardLook& k, int8_t a) { if (a >= 0) k.artsUsed |= (uint16_t)(1u << a); }

// {"rect":[...], "font":..., "scale":..., "ink":..., "fill":..., "edge":..., "frame":..., "slice":[...], "bg":...}
bool pieceObject(P& p, const char* piece, int depth, Slot& slot, uieng::PieceStyle& st, uieng::BoardLook& look) {
    static const char* const K[] = {"rect", "font", "scale", "ink", "fill", "edge", "frame", "slice", "bg"};
    bool hasRect = false, styled = false;
    const bool ok = objectKeys(p, piece, depth, K, 9, [&](int i) {
        int v; int q[4];
        if (i) styled = true;
        switch (i) {
        case 0: hasRect = true; return rectValue(p, piece, slot);
        case 1: if (!ref(p, piece, 'f', st.font)) return false; useFont(look, st.font); return true;
        case 2: if (!intIn(p, piece, 1, 4, "scale must be 1 to 4", v)) return false; st.scale = (uint8_t)v; return true;
        case 3: return colour(p, piece, st.ink);
        case 4: return colour(p, piece, st.fill);
        case 5: return colour(p, piece, st.edge);
        case 6: if (!ref(p, piece, 'a', st.frame)) return false; useArt(look, st.frame); return true;
        case 7: if (!quad(p, piece, "slice must be four numbers 0..255", 0, 255, q)) return false;
                for (int k = 0; k < 4; k++) st.slice[k] = (uint8_t)q[k];
                return true;
        default: if (!ref(p, piece, 'a', st.bg)) return false; useArt(look, st.bg); return true;
        }
    });
    if (!ok) return false;
    if (!hasRect) return failPiece(p, piece, "needs a rect");
    if (styled) look.any = true;
    return true;
}

// "style": {"title","body","numbers","bg","panel","panelSlice","highlight":{"fill","edge"},"icons":[...]}
bool styleBlock(P& p, int depth, uieng::BoardLook& look) {
    static const char* const K[] = {"title", "body", "numbers", "bg", "panel", "panelSlice", "highlight", "icons"};
    uieng::ScreenStyle& s = look.screen;
    const char* piece = "style";
    look.any = true;
    return objectKeys(p, piece, depth, K, 8, [&](int i) {
        int q[4];
        switch (i) {
        case 0: if (!ref(p, piece, 'f', s.roleFont[1])) return false; useFont(look, s.roleFont[1]); return true;   // ROLE_TITLE
        case 1: if (!ref(p, piece, 'f', s.roleFont[0])) return false; useFont(look, s.roleFont[0]); return true;   // ROLE_BODY
        case 2: if (!ref(p, piece, 'f', s.roleFont[2])) return false; useFont(look, s.roleFont[2]); return true;   // ROLE_NUMBERS
        case 3: if (!ref(p, piece, 'a', s.bg)) return false; useArt(look, s.bg); return true;
        case 4: if (!ref(p, piece, 'a', s.panel)) return false; useArt(look, s.panel); return true;
        case 5: if (!quad(p, piece, "panelSlice must be four numbers 0..255", 0, 255, q)) return false;
                for (int k = 0; k < 4; k++) s.panelSlice[k] = (uint8_t)q[k];
                return true;
        case 6: {
            static const char* const H[] = {"fill", "edge"};
            return objectKeys(p, "highlight", depth + 1, H, 2, [&](int j) {
                return colour(p, "highlight", j == 0 ? s.hiFill : s.hiEdge);
            });
        }
        default: {
            if (!eat(p, '[')) return failPiece(p, piece, "icons must be a list of art references");
            if (eat(p, ']')) return true;
            for (int n = 0;; n++) {
                if (n >= uieng::MENU_ICONS) return failPiece(p, piece, "icons holds at most 6 entries");
                ws(p);
                if (p.n - p.i >= 4 && memcmp(p.s + p.i, "null", 4) == 0) { p.i += 4; s.icon[n] = -1; }
                else if (!ref(p, "icons", 'a', s.icon[n])) return false;
                else useArt(look, s.icon[n]);
                if (eat(p, ',')) continue;
                if (eat(p, ']')) return true;
                return failPiece(p, piece, "icons must be a list of art references");
            }
        }
        }
    });
}

// "decor.N": {"rect":[...], "art":"aN"}
bool decorValue(P& p, const char* name, int n, int depth, uieng::BoardLook& look) {
    static const char* const K[] = {"rect", "art"};
    Slot r{SLOT_DEFAULT, 0, 0, 0, 0};
    int8_t art = -1;
    bool hasRect = false;
    const bool ok = objectKeys(p, name, depth, K, 2, [&](int i) {
        if (i == 0) { hasRect = true; return rectValue(p, name, r); }
        return ref(p, name, 'a', art);
    });
    if (!ok) return false;
    if (!hasRect) return failPiece(p, name, "needs a rect");
    if (art < 0) return failPiece(p, name, "needs an art reference");
    look.decor[n].r = {r.x, r.y, r.w, r.h};
    look.decor[n].art = art;
    look.nDecor++;
    useArt(look, art);
    look.any = true;
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
bool board(P& p, Slot (&slots)[P_N], int depth, uieng::BoardLook& look) {
    if (!peek(p, '{')) return fail(p, "a board must be an object");
    if (depth >= MAX_DEPTH) return fail(p, "nested too deep");
    p.i++;
    bool seen[P_N] = {}, seenStyle = false;
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
            if (peek(p, '{')) {
                if (!pieceObject(p, PIECE_NAME[piece], depth + 1, slots[piece], look.piece[piece], look)) return false;
            } else if (!slotValue(p, PIECE_NAME[piece], slots[piece])) {
                return false;
            }
        } else if (keyIs(k, kl, "style")) {
            if (seenStyle) return fail(p, "duplicate style");
            seenStyle = true;
            if (!styleBlock(p, depth + 1, look)) return false;
        } else if (kl > 6 && memcmp(k, "decor.", 6) == 0) {
            if (kl != 7 || k[6] < '0' || k[6] >= '0' + uieng::MAX_DECOR) return fail(p, "decor pieces are decor.0 to decor.7");
            const int n = k[6] - '0';
            if (look.decor[n].art >= 0) return fail(p, "duplicate decor piece");
            if (!decorValue(p, k, n, depth + 1, look)) return false;
        } else if (!skipValue(p, depth + 1)) {
            return false;
        }
        if (eat(p, ',')) continue;
        if (eat(p, '}')) return true;
        return fail(p, "expected ',' or end");
    }
}

bool parseObject(P& p, Layout& out, uieng::BoardLook* looks) {
    memset(&out, 0, sizeof out);
    uieng::BoardLook scratch;
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
                if (!board(p, out.slot[b], 1, looks ? looks[b] : (scratch = uieng::BoardLook(), scratch))) return false;
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
    return parse(json, n, out, nullptr, why, whyCap);
}

bool parse(const char* json, size_t n, Layout& out, uieng::BoardLook* looks, char* why, size_t whyCap) {
    if (looks) for (int b = 0; b < B_N; b++) looks[b] = uieng::BoardLook();
    P p{json, n, 0, nullptr, {0}};
    bool ok = false;
    if (!json || n == 0) fail(p, "empty file");
    else if (n > MAX_JSON) fail(p, "layout.json too large");
    else if (parseObject(p, out, looks)) {
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
constexpr int MIN_RADAR_BOX = 2 * 24 + 5;   // radius 24 plus the 2 px marker margin each side
const int BOARD_W[B_N] = {480, 320}, BOARD_H[B_N] = {320, 240};

bool isTile(int p) { return p == P_COUNT_WIFI || p == P_COUNT_5G || p == P_COUNT_BLE; }
bool isGroup(int p) { return p == P_RADAR || p == P_SPECTRUM || p == P_CHANNEL || p == P_BADGE; }   // the radar group
bool isChrome(int p) { return p < P_COUNT_WIFI; }                                                   // header and footer
bool tappable(int p) { return isChrome(p) ? (p != P_WORDMARK && p != P_SD) : isTile(p); }
bool hideable(int p) { return p == P_WORDMARK || p == P_SD || isTile(p) || isGroup(p); }
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
    const uieng::ScanLayout s = uieng::builtinScan(W, H);
    switch (p) {
    case P_RADAR: return s.radar;
    case P_SPECTRUM: return s.spectrum;
    case P_CHANNEL: return s.channel;
    case P_BADGE: return s.badge;
    default: return s.tiles[p - P_COUNT_WIFI];
    }
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
        if (p == P_WORDMARK || p == P_SD || p == P_CHANNEL) {   // drawn at fixed offsets: smaller would spill outside the piece
            const Rect d = defaultRect(b, (Piece)p);
            if (s.w < d.w || s.h < d.h) return say(why, whyCap, "%s must be at least its built-in size", name);
        }
        if (p == P_RADAR) {    // square; r = (w-5)/2 from 24 up to today's radius on this board
            if (s.w != s.h) return say(why, whyCap, "radar must be square");
            const Rect d = defaultRect(b, P_RADAR);
            if (s.w < MIN_RADAR_BOX || s.w > d.w) return say(why, whyCap, "radar must be 53 px up to its built-in size");
        }
        if (p == P_SPECTRUM && (s.w < 42 || s.h < 22)) return say(why, whyCap, "spectrum must be at least 42x22 px");
        if (p == P_BADGE && (s.w < 24 || s.h < 24)) return say(why, whyCap, "badge must be at least 24x24 px");
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
    for (int p = P_COUNT_WIFI; p < P_N; p++) {
        if (L.slot[b][p].how != SLOT_DEFAULT) continue;
        if (isTile(p)) r[p] = shifted(bs.tiles[p - P_COUNT_WIFI]);
        else if (p == P_RADAR) r[p] = shifted(bs.radar);
        else if (p == P_SPECTRUM) r[p] = shifted(bs.spectrum);
        else if (p == P_CHANNEL) r[p] = shifted(bs.channel);
        else if (p == P_BADGE) r[p] = shifted(bs.badge);
    }

    for (int p = 0; p < P_N; p++)
        for (int q = p + 1; q < P_N; q++) {
            if (isGroup(p) && isGroup(q)) continue;   // the radar's parts may overlap each other, as the built-in layout's do
            if (overlaps(r[p], r[q])) return say(why, whyCap, "%s overlaps %s", PIECE_NAME[p], PIECE_NAME[q]);
        }
    // The hero (alert area, threat target): the bounding box of the visible radar group, or the built-in hero.
    Rect hero{0, 0, 0, 0};
    for (int p = P_RADAR; p <= P_BADGE; p++) {
        const Rect& e = r[p];
        if (!visible(e)) continue;
        if (!visible(hero)) { hero = e; continue; }
        const int x0 = e.x < hero.x ? e.x : hero.x, y0 = e.y < hero.y ? e.y : hero.y;
        const int x1 = e.x + e.w > hero.x + hero.w ? e.x + e.w : hero.x + hero.w;
        const int y1 = e.y + e.h > hero.y + hero.h ? e.y + e.h : hero.y + hero.h;
        hero = {x0, y0, x1 - x0, y1 - y0};
    }
    const bool groupDefault = L.slot[b][P_RADAR].how == SLOT_DEFAULT && L.slot[b][P_SPECTRUM].how == SLOT_DEFAULT &&
                              L.slot[b][P_CHANNEL].how == SLOT_DEFAULT && L.slot[b][P_BADGE].how == SLOT_DEFAULT;
    if (groupDefault || !visible(hero)) hero = shifted(bs.hero);
    else    // the group's box is the alert area and the threat target: it may not take in a tile or a header/footer piece
        for (int p = 0; p < P_RADAR; p++)
            if (overlaps(hero, r[p])) return say(why, whyCap, "the radar parts enclose %s", PIECE_NAME[p]);

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
    out.radar = r[P_RADAR]; out.spectrum = r[P_SPECTRUM]; out.channel = r[P_CHANNEL]; out.badge = r[P_BADGE];
    out.counts = shifted(bs.counts);
    out.scanBody = shifted(bs.body);
    if (why && whyCap) why[0] = 0;
    return true;
}

bool checkLook(const uieng::BoardLook& k, Board b, const uint16_t* colors, char* why, size_t whyCap) {
    if (b >= B_N) return say(why, whyCap, "bad board");
    const int W = BOARD_W[b], H = BOARD_H[b];
    static const char* const DECOR[uieng::MAX_DECOR] = {"decor.0", "decor.1", "decor.2", "decor.3", "decor.4", "decor.5", "decor.6", "decor.7"};
    for (int i = 0; i < uieng::MAX_DECOR; i++) {
        const uieng::Decor& d = k.decor[i];
        if (d.art < 0) continue;
        if (d.r.x < 0 || d.r.y < 0 || d.r.x + d.r.w > W || d.r.y + d.r.h > H) return say(why, whyCap, "%s is off screen", DECOR[i]);
    }
    // Text on the pieces that have no other route must stay readable against their fill.
    static const int MUST_READ[] = {P_MENU, P_STOP, P_NAV_SCAN, P_NAV_DOMAIN, P_NAV_CODEX, P_NAV_POI, P_BRIDGE};
    for (int p : MUST_READ) {
        const uieng::PieceStyle& s = k.piece[p];
        if (!s.ink.kind && !s.fill.kind) continue;
        auto value = [&](const uieng::ColorRef& c, int role, bool& known) -> uint16_t {
            if (c.kind == 2) return c.v;
            const int r = c.kind == 1 ? c.v : role;
            if (!colors) { known = false; return 0; }
            return colors[r];
        };
        bool known = true;
        const uint16_t ink = value(s.ink, themepack::R_TEXT, known), fill = value(s.fill, themepack::R_PANEL, known);
        if (known && ui::contrastX10(ink, fill) < ui::MIN_EDGE_CONTRAST_X10)
            return say(why, whyCap, "%s: text and fill contrast under 3:1", PIECE_NAME[p]);
    }
    if (why && whyCap) why[0] = 0;
    return true;
}

bool build(const char* json, size_t n, PackLayout& out, char* why, size_t whyCap, const uint16_t* colors) {
    out.lo.board[0] = out.lo.board[1] = nullptr;
    out.lo.look[0] = out.lo.look[1] = nullptr;
    if (n > MAX_JSON) {
        if (why && whyCap) snprintf(why, whyCap, "layout.json over %u bytes", (unsigned)MAX_JSON);
        return false;
    }
    Layout L;   // ~300 bytes on the loader task's stack: never in .bss (internal RAM is scarce)
    char r[80];
    if (!parse(json, n, L, out.look, r, sizeof r)) {
        if (why && whyCap) snprintf(why, whyCap, "layout.json: %s", r);
        return false;
    }
    for (int b = 0; b < B_N; b++) {
        if (!L.has[b]) continue;
        if (!resolve(L, (Board)b, out.ov[b], r, sizeof r) || !checkLook(out.look[b], (Board)b, colors, r, sizeof r)) {
            if (why && whyCap) snprintf(why, whyCap, "layout.json: %s", r);
            out.lo.board[0] = out.lo.board[1] = nullptr;
            out.lo.look[0] = out.lo.look[1] = nullptr;
            return false;
        }
        out.lo.board[b] = &out.ov[b];
        if (out.look[b].any) out.lo.look[b] = &out.look[b];
    }
    if (why && whyCap) why[0] = 0;
    return true;
}

}  // namespace themelayout
