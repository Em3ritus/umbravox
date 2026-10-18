#include "gnss.h"

namespace ui {
namespace Gnss {
namespace {

uint32_t epochAtMs = 0;      // millis() when `epoch` was true
uint32_t epoch     = 0;      // UTC seconds at that moment
uint32_t todaySecs = 0;      // seconds into the day, from GGA
uint32_t dateYmd   = 0;      // yyyymmdd, from RMC

char     buf[100];
int      bufN = 0;
Fix      cur;
uint32_t nGood = 0, nBad = 0;

// One slot per talker ID we accept, so GSV totals can be summed rather than
// overwritten. Index is the two-character talker, packed.
struct View { uint16_t id; uint8_t n; };
View views[8];
int  nViews = 0;

int hexVal(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

// Copy comma-separated field `want` into out.
void field(const char* s, int want, char* out, int n) {
    int f = 0, o = 0;
    out[0] = 0;
    for (const char* p = s; *p && *p != '*'; p++) {
        if (*p == ',') { if (++f > want) break; continue; }
        if (f == want && o + 1 < n) out[o++] = *p;
    }
    out[o] = 0;
}

// hhmmss.sss -> seconds into the day. Returns -1 if it does not look like one.
int hmsToSecs(const char* s) {
    int n = 0;
    while (s[n] && s[n] != '.') n++;
    if (n < 6) return -1;
    const int h = (s[0]-'0')*10 + (s[1]-'0');
    const int m = (s[2]-'0')*10 + (s[3]-'0');
    const int c = (s[4]-'0')*10 + (s[5]-'0');
    if (h > 23 || m > 59 || c > 60) return -1;
    return h * 3600 + m * 60 + c;
}

int toInt(const char* s) {
    int v = 0;
    for (; *s >= '0' && *s <= '9'; s++) v = v * 10 + (*s - '0');
    return v;
}

// NMEA angles are ddmm.mmmm (or dddmm.mmmm) -- degrees and *minutes*, not
// decimal degrees. Converting with a plain atof is the classic way to be
// wrong by a factor inside every coordinate.
int32_t toDeg7(const char* s, char hemi) {
    int dot = -1;
    int n = 0;
    while (s[n]) { if (s[n] == '.') dot = n; n++; }
    if (dot < 2) return 0;
    const int degDigits = dot - 2;

    int32_t deg = 0;
    for (int i = 0; i < degDigits; i++) deg = deg * 10 + (s[i] - '0');

    // minutes, scaled to 1e5 so deg*1e7 + min/60*1e7 stays in int64 range
    int64_t minScaled = 0;
    int frac = 0;
    for (int i = degDigits; s[i]; i++) {
        if (s[i] == '.') continue;
        if (s[i] < '0' || s[i] > '9') break;
        if (frac < 7) { minScaled = minScaled * 10 + (s[i] - '0'); frac++; }
    }
    while (frac < 7) { minScaled *= 10; frac++; }   // mmmmmmm, i.e. min x 1e5

    int64_t v = (int64_t)deg * 10000000LL + minScaled * 10LL / 6LL;
    if (hemi == 'S' || hemi == 'W') v = -v;
    return (int32_t)v;
}

void noteView(const char* s, uint8_t count) {
    const uint16_t id = (uint16_t)((uint8_t)s[1] << 8 | (uint8_t)s[2]);
    for (int i = 0; i < nViews; i++)
        if (views[i].id == id) { views[i].n = count; return; }
    if (nViews < (int)(sizeof(views) / sizeof(views[0])))
        views[nViews++] = View{id, count};
}

} // namespace

// Howard Hinnant's days_from_civil, which is exact and has no table.
uint32_t toEpochImpl(int y, int m, int d, int hh, int mm, int ss) {
    y -= m <= 2;
    const int era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = (unsigned)(y - era * 400);
    const unsigned doy = (unsigned)((153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1);
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    const long days = (long)era * 146097 + (long)doe - 719468;
    return (uint32_t)(days * 86400L + hh * 3600 + mm * 60 + ss);
}

bool sentence(const char* s, uint32_t nowMs) {
    if (!s || s[0] != '$') return false;

    // Checksum: XOR of everything between '$' and '*'.
    uint8_t sum = 0;
    int i = 1;
    for (; s[i] && s[i] != '*'; i++) sum ^= (uint8_t)s[i];
    if (s[i] == '*') {
        const int hi = hexVal(s[i + 1]), lo = hexVal(s[i + 2]);
        if (hi < 0 || lo < 0 || sum != (uint8_t)((hi << 4) | lo)) { nBad++; return false; }
    }
    nGood++;

    const char* type = s + 3;          // skip '$' and the 2-char talker
    char f[16];

    if (!(type[0] == 'G' && type[1] == 'G' && type[2] == 'A')) {
        if (type[0] == 'G' && type[1] == 'S' && type[2] == 'V') {
            field(s, 3, f, sizeof f);
            noteView(s, (uint8_t)toInt(f));
        } else if (type[0] == 'R' && type[1] == 'M' && type[2] == 'C') {
            // RMC field 9 is ddmmyy -- the only place the date appears.
            char d[12];
            field(s, 9, d, sizeof d);
            if (d[0] && d[5]) {
                const int dd = (d[0]-'0')*10 + (d[1]-'0');
                const int mo = (d[2]-'0')*10 + (d[3]-'0');
                const int yy = (d[4]-'0')*10 + (d[5]-'0');
                if (dd >= 1 && dd <= 31 && mo >= 1 && mo <= 12)
                    dateYmd = (uint32_t)((2000 + yy) * 10000 + mo * 100 + dd);
            }
            char tf[16];
            field(s, 1, tf, sizeof tf);
            const int secs = hmsToSecs(tf);
            if (secs >= 0) todaySecs = (uint32_t)secs;
            if (dateYmd && todaySecs) {
                epoch = toEpochImpl((int)(dateYmd / 10000),
                                    (int)((dateYmd / 100) % 100),
                                    (int)(dateYmd % 100), 0, 0, 0) + todaySecs;
                epochAtMs = nowMs;
            }
        }
        return true;
    }

    field(s, 6, f, sizeof f);
    const int qual = toInt(f);
    if (qual <= 0) { cur.valid = false; return true; }

    field(s, 7, f, sizeof f);
    cur.used = (uint8_t)toInt(f);

    char val[16], hemi[4];
    field(s, 2, val, sizeof val); field(s, 3, hemi, sizeof hemi);
    cur.lat7 = toDeg7(val, hemi[0]);
    field(s, 4, val, sizeof val); field(s, 5, hemi, sizeof hemi);
    cur.lon7 = toDeg7(val, hemi[0]);

    cur.valid = true;
    cur.atMs  = nowMs;
    return true;
}

void feed(char c, uint32_t nowMs) {
    if (c == '\r' || c == '\n') {
        if (bufN) { buf[bufN] = 0; sentence(buf, nowMs); bufN = 0; }
        return;
    }
    if (c == '$') bufN = 0;                       // resync on a fresh start
    if (bufN < (int)sizeof(buf) - 1) buf[bufN++] = c;
}

const Fix& fix() { return cur; }

uint8_t inView() {
    int t = 0;
    for (int i = 0; i < nViews; i++) t += views[i].n;
    return (uint8_t)(t > 255 ? 255 : t);
}

bool fresh(uint32_t nowMs) {
    return cur.valid && (nowMs - cur.atMs) < FRESH_MS;
}

uint32_t utcEpoch(uint32_t nowMs) {
    if (!epoch) return 0;
    // Carry the clock forward with millis() between sentences, so a row
    // logged a few hundred ms after the last RMC is not stamped with it.
    return epoch + (nowMs - epochAtMs) / 1000;
}

uint32_t toEpoch(int y, int mo, int d, int h, int mi, int s) {
    return toEpochImpl(y, mo, d, h, mi, s);
}

uint32_t good() { return nGood; }
uint32_t bad()  { return nBad; }

void reset() {
    bufN = 0; nViews = 0; nGood = 0; nBad = 0;
    epoch = 0; epochAtMs = 0; todaySecs = 0; dateYmd = 0;
    cur = Fix{};
}

} // namespace Gnss
} // namespace ui
