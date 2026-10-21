// Bounded recursive-descent parser for theme.json. See themepack.h.
#include "themepack.h"
#include "model.h"
#include <string.h>

namespace themepack {
namespace {

constexpr int MAX_DEPTH = 4;

struct P {
    const char* s;
    size_t n;
    size_t i;
    const char* err;
};

bool fail(P& p, const char* why) {
    if (!p.err) p.err = why;
    return false;
}

void ws(P& p) {
    while (p.i < p.n && (p.s[p.i] == ' ' || p.s[p.i] == '\t' ||
                         p.s[p.i] == '\n' || p.s[p.i] == '\r'))
        p.i++;
}

bool eat(P& p, char c) {
    ws(p);
    if (p.i < p.n && p.s[p.i] == c) { p.i++; return true; }
    return false;
}

// A string with no escapes and no control bytes. Writes up to cap chars into
// buf (when non-null); a longer string is an error only when `strict`, else
// it is consumed and truncated by returning len > cap via *len.
bool str(P& p, char* buf, size_t cap, size_t* len) {
    ws(p);
    if (p.i >= p.n || p.s[p.i] != '"') return fail(p, "expected string");
    p.i++;
    size_t k = 0;
    while (p.i < p.n) {
        unsigned char c = (unsigned char)p.s[p.i];
        if (c == '"') { p.i++; if (len) *len = k; if (buf && k <= cap) buf[k] = 0; return true; }
        if (c == '\\') return fail(p, "string escapes not supported");
        if (c < 32 || c > 126) return fail(p, "bad character in string");
        if (buf && k < cap) buf[k] = (char)c;
        k++;
        p.i++;
    }
    return fail(p, "unterminated string");
}

bool skipValue(P& p, int depth);

bool skipContainer(P& p, int depth, char open, char close) {
    if (depth >= MAX_DEPTH) return fail(p, "nested too deep");
    p.i++;  // the opener
    (void)open;
    if (eat(p, close)) return true;
    for (;;) {
        if (open == '{') {
            if (!str(p, nullptr, 0, nullptr)) return false;
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
    if (c == '{') return skipContainer(p, depth, '{', '}');
    if (c == '[') return skipContainer(p, depth, '[', ']');
    if (c == '-' || (c >= '0' && c <= '9')) {
        while (p.i < p.n && (p.s[p.i] == '-' || p.s[p.i] == '+' || p.s[p.i] == '.' ||
                             p.s[p.i] == 'e' || p.s[p.i] == 'E' ||
                             (p.s[p.i] >= '0' && p.s[p.i] <= '9')))
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

bool integer(P& p, long& v) {
    ws(p);
    size_t st = p.i;
    v = 0;
    while (p.i < p.n && p.s[p.i] >= '0' && p.s[p.i] <= '9') {
        if (v < 1000000) v = v * 10 + (p.s[p.i] - '0');
        p.i++;
    }
    if (p.i == st) return fail(p, "expected integer");
    return true;
}

uint16_t rgb565(unsigned r, unsigned g, unsigned b) {
    return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

int hexv(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

bool colour(P& p, uint16_t& out) {
    char b[8];
    size_t len = 0;
    if (!str(p, b, 7, &len)) return false;
    if (len != 7 || b[0] != '#') return fail(p, "colour must be #RRGGBB");
    unsigned v[6];
    for (int k = 0; k < 6; k++) {
        int h = hexv(b[1 + k]);
        if (h < 0) return fail(p, "bad hex digit in colour");
        v[k] = (unsigned)h;
    }
    out = rgb565(v[0] << 4 | v[1], v[2] << 4 | v[3], v[4] << 4 | v[5]);
    return true;
}

const char* const ROLE_KEYS[N_COLORS] = {
    "void", "panel", "grid", "edge", "edgeLit", "dim", "text", "bright",
    "witch", "witchHi", "bone", "sweep", "sweepHi", "live", "liveHi",
    "magenta", "ice", "green", "amber", "red"};

struct SweepName { const char* n; ui::Sweep id; };
const SweepName SWEEPS[] = {
    {"radar", ui::SW_RADAR},   {"pulse", ui::SW_PULSE},   {"star", ui::SW_STAR},
    {"candle", ui::SW_CANDLE}, {"galaxy", ui::SW_GALAXY}, {"orbit", ui::SW_ORBIT},
    {"starfall", ui::SW_STARFALL}, {"pulsar", ui::SW_PULSAR},
    {"comet", ui::SW_COMET},   {"aurora", ui::SW_AURORA}};

bool sweepId(const char* s, uint8_t& id) {
    for (const SweepName& e : SWEEPS)
        if (strcmp(s, e.n) == 0) { id = (uint8_t)e.id; return true; }
    return false;
}

bool colors(P& p, Pack& out, int depth) {
    if (depth >= MAX_DEPTH) return fail(p, "nested too deep");
    if (!eat(p, '{')) return fail(p, "colors must be an object");
    bool seen[N_COLORS] = {};
    if (!eat(p, '}')) {
        for (;;) {
            char k[16];
            size_t kl = 0;
            if (!str(p, k, 15, &kl)) return false;
            if (!eat(p, ':')) return fail(p, "expected ':'");
            int role = -1;
            if (kl <= 15)
                for (int r = 0; r < N_COLORS; r++)
                    if (strcmp(k, ROLE_KEYS[r]) == 0) { role = r; break; }
            if (role >= 0) {
                if (seen[role]) return fail(p, "duplicate colour");
                seen[role] = true;
                if (!colour(p, out.color[role])) return false;
            } else if (!skipValue(p, depth + 1)) {
                return false;
            }
            if (eat(p, ',')) continue;
            if (eat(p, '}')) break;
            return fail(p, "expected ',' or '}'");
        }
    }
    for (int r = 0; r < N_COLORS; r++)
        if (!seen[r]) return fail(p, "missing colour");
    return true;
}

bool sweepList(P& p, Pack& out) {
    if (!eat(p, '[')) return fail(p, "sweeps must be an array");
    out.sweeps = 0;
    if (eat(p, ']')) return true;
    for (;;) {
        char b[12];
        size_t len = 0;
        if (!str(p, b, 11, &len)) return false;
        uint8_t id;
        if (len > 11 || !sweepId(b, id)) return fail(p, "unknown or unavailable sweep");
        out.sweeps |= (uint16_t)(1u << id);
        if (eat(p, ',')) continue;
        if (eat(p, ']')) return true;
        return fail(p, "expected ',' or ']'");
    }
}

bool parseObject(P& p, Pack& out) {
    enum { K_FORMAT, K_NAME, K_AUTHOR, K_HEADER, K_DIAL, K_CENTRE, K_SWEEPS,
           K_SWEEP, K_ALERT, K_COLORS, K_N };
    static const char* const keys[K_N] = {"format", "name", "author", "header", "dial",
        "centrepiece", "sweeps", "sweep", "alert", "colors"};
    bool seen[K_N] = {};
    char sweepName[12] = {};
    bool haveSweepName = false;

    memset(&out, 0, sizeof out);
    out.dial = DIAL_COMPACT;
    out.centre = CENTRE_HEPTAGRAM;
    out.rite = RITE_WARD;

    if (!eat(p, '{')) return fail(p, "not a JSON object");
    if (!eat(p, '}')) {
        for (;;) {
            char k[16];
            size_t kl = 0;
            if (!str(p, k, 15, &kl)) return false;
            if (!eat(p, ':')) return fail(p, "expected ':'");
            int key = -1;
            if (kl <= 15)
                for (int j = 0; j < K_N; j++)
                    if (strcmp(k, keys[j]) == 0) { key = j; break; }
            if (key >= 0) {
                if (seen[key]) return fail(p, "duplicate key");
                seen[key] = true;
            }
            char tmp[32];
            size_t tl = 0;
            switch (key) {
            case K_FORMAT: {
                long v;
                if (!integer(p, v)) return false;
                if (v > FORMAT) return fail(p, "pack format is newer than this firmware");
                if (v < FORMAT) return fail(p, "bad pack format");
                break;
            }
            case K_NAME:
                if (!str(p, out.name, MAX_NAME, &tl)) return false;
                if (tl < 1 || tl > (size_t)MAX_NAME) return fail(p, "name must be 1-20 chars");
                for (size_t j = 0; j < tl; j++) {
                    char c = out.name[j];
                    if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == ' ' ||
                          c == '-' || c == '.'))
                        return fail(p, "name has a character outside A-Z 0-9 space - .");
                }
                break;
            case K_AUTHOR:
                if (!str(p, out.author, MAX_AUTHOR, &tl)) return false;
                if (tl > (size_t)MAX_AUTHOR) return fail(p, "author longer than 24");
                break;
            case K_HEADER:
                if (!str(p, out.header, MAX_HEADER, &tl)) return false;
                if (tl < 1 || tl > (size_t)MAX_HEADER) return fail(p, "header must be 1-18 chars");
                for (size_t j = 0; j < tl; j++) {
                    char& c = out.header[j];
                    if (c >= 'a' && c <= 'z') c = (char)(c - 32);
                    if (c < 32 || c > 95) return fail(p, "header has a character the font lacks");
                }
                break;
            case K_DIAL:
                if (!str(p, tmp, 31, &tl)) return false;
                if (tl <= 31 && strcmp(tmp, "compact") == 0) out.dial = DIAL_COMPACT;
                else if (tl <= 31 && strcmp(tmp, "large") == 0) out.dial = DIAL_LARGE;
                else return fail(p, "dial must be compact or large");
                break;
            case K_CENTRE:
                if (!str(p, tmp, 31, &tl)) return false;
                if (tl <= 31 && strcmp(tmp, "emblem") == 0) out.centre = CENTRE_EMBLEM;
                else if (tl <= 31 && strcmp(tmp, "heptagram") == 0) out.centre = CENTRE_HEPTAGRAM;
                else return fail(p, "centrepiece must be emblem or heptagram");
                break;
            case K_SWEEPS:
                if (!sweepList(p, out)) return false;
                break;
            case K_SWEEP:
                if (!str(p, sweepName, 11, &tl)) return false;
                if (tl > 11) return fail(p, "unknown or unavailable sweep");
                haveSweepName = true;
                break;
            case K_ALERT:
                if (!str(p, tmp, 31, &tl)) return false;
                if (tl <= 31 && strcmp(tmp, "ward") == 0) out.rite = RITE_WARD;
                else if (tl <= 31 && strcmp(tmp, "supernova") == 0) out.rite = RITE_SUPERNOVA;
                else if (tl <= 31 && strcmp(tmp, "vortex") == 0) out.rite = RITE_VORTEX;
                else if (tl <= 31 && strcmp(tmp, "comet") == 0) out.rite = RITE_COMET;
                else if (tl <= 31 && strcmp(tmp, "lockon") == 0) out.rite = RITE_LOCKON;
                else return fail(p, "unknown or unavailable alert");
                break;
            case K_COLORS:
                if (!colors(p, out, 1)) return false;
                break;
            default:
                if (!skipValue(p, 1)) return false;
            }
            if (eat(p, ',')) continue;
            if (eat(p, '}')) break;
            return fail(p, "expected ',' or '}'");
        }
    }
    if (!seen[K_FORMAT]) return fail(p, "missing format");
    if (!seen[K_NAME]) return fail(p, "missing name");
    if (!seen[K_SWEEPS]) return fail(p, "missing sweeps");
    if (!seen[K_COLORS]) return fail(p, "missing colors");
    if (out.sweeps == 0) return fail(p, "sweeps is empty");
    if (!seen[K_HEADER]) {
        // Default the header to the name (already within the font and 20 chars).
        size_t l = strlen(out.name);
        if (l > (size_t)MAX_HEADER) l = MAX_HEADER;
        memcpy(out.header, out.name, l);
        out.header[l] = 0;
    }
    if (haveSweepName) {
        uint8_t id;
        if (!sweepId(sweepName, id)) return fail(p, "unknown or unavailable sweep");
        out.sweep = id;
        if (!(out.sweeps & (1u << id))) return fail(p, "sweep is not in sweeps");
    } else {
        for (int id = 0; id < 16; id++)
            if (out.sweeps & (1u << id)) { out.sweep = (uint8_t)id; break; }
    }
    return true;
}

}  // namespace

bool parse(const char* json, size_t n, Pack& out, char* why, size_t whyCap) {
    P p{json, n, 0, nullptr};
    bool ok = false;
    if (!json || n == 0) fail(p, "empty file");
    else if (n > MAX_JSON) fail(p, "theme.json too large");
    else if (parseObject(p, out)) {
        ws(p);
        if (p.i != p.n) fail(p, "trailing data after JSON");
        else ok = true;
    }
    if (!ok && !p.err) p.err = "invalid theme.json";
    if (why && whyCap) {
        if (ok) why[0] = 0;
        else {
            strncpy(why, p.err, whyCap - 1);
            why[whyCap - 1] = 0;
        }
    }
    return ok;
}

}  // namespace themepack
