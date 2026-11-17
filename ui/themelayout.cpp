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
        if (vl == 6 && memcmp(v, "hidden", 6) == 0) { out.how = HIDDEN; return true; }
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
    out.how = PLACED; out.x = v[0]; out.y = v[1]; out.w = v[2]; out.h = v[3];
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
