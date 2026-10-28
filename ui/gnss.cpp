#include "gnss.h"

namespace ui {
namespace Gnss {
namespace {

uint32_t epochAtMs = 0;      // millis() when `epoch` was true
uint32_t epoch     = 0;      // UTC seconds at that moment

// NMEA 0183 caps a sentence at 82 characters with its CRLF; this leaves room
// for receivers that run a little long. A longer line is dropped whole: cut
// to fit, it would lose its checksum and its tail.
char     buf[100];
int      bufN = 0;
bool     bufOver = false;
Fix      cur;
Status   st;
Counts   cnt;
uint32_t nGood = 0, nBad = 0, nQual0 = 0;

// RMC carries a two-digit year. 00-79 is read as 20yy and 80-99 as 19yy, the
// usual century rule; then anything before MIN_YEAR is refused as "no date".
// That catches the 1980-01-06 (GPS week 0) and 2080 defaults a receiver
// reports from an unset clock, and a 2000s RTC default, none of which may name
// a log file.
constexpr int MIN_YEAR = 2024;

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

bool digits(const char* s, int n) {
    for (int i = 0; i < n; i++) if (s[i] < '0' || s[i] > '9') return false;
    return true;
}

// hhmmss.sss -> seconds into the day. Returns -1 if it does not look like one.
int hmsToSecs(const char* s) {
    int n = 0;
    while (s[n] && s[n] != '.') n++;
    if (n != 6 || !digits(s, 6)) return -1;
    const int h = (s[0]-'0')*10 + (s[1]-'0');
    const int m = (s[2]-'0')*10 + (s[3]-'0');
    const int c = (s[4]-'0')*10 + (s[5]-'0');
    if (h > 23 || m > 59 || c > 60) return -1;
    return h * 3600 + m * 60 + c;
}

// ddmmyy -> days-valid y/m/d, or false. Two-digit year per the rule above.
bool parseDate(const char* d, int& y, int& mo, int& dd) {
    int n = 0;
    while (d[n]) n++;
    if (n != 6 || !digits(d, 6)) return false;
    dd = (d[0]-'0')*10 + (d[1]-'0');
    mo = (d[2]-'0')*10 + (d[3]-'0');
    const int yy = (d[4]-'0')*10 + (d[5]-'0');
    y = yy < 80 ? 2000 + yy : 1900 + yy;
    if (y < MIN_YEAR || mo < 1 || mo > 12 || dd < 1) return false;
    static const uint8_t mdays[12] = {31,28,31,30,31,30,31,31,30,31,30,31};
    const bool leap = (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
    return dd <= mdays[mo - 1] + (mo == 2 && leap);
}

// "1.01" -> 101, "99.99" -> 9999; empty -> 0.
uint16_t toCenti(const char* s) {
    uint32_t v = 0;
    int frac = -1;
    for (; *s; s++) {
        if (*s == '.') { if (frac >= 0) break; frac = 0; continue; }
        if (*s < '0' || *s > '9') break;
        if (frac >= 2) continue;
        v = v * 10 + (uint32_t)(*s - '0');
        if (frac >= 0) frac++;
        if (v > 650000) break;
    }
    if (frac < 0) frac = 0;
    while (frac < 2) { v *= 10; frac++; }
    return (uint16_t)(v > 65535 ? 65535 : v);
}

bool is3(const char* t, char a, char b, char c) { return t[0] == a && t[1] == b && t[2] == c; }

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
    if (!s || s[0] != '$') { nBad++; return false; }

    // Checksum: XOR of everything between '$' and '*'. Required: the NEO-6M
    // sends one on every sentence, and a line that lost its tail to a dropped
    // byte would otherwise be taken whole. Bytes outside printable ASCII are
    // line noise even when the XOR happens to agree.
    uint8_t sum = 0;
    int i = 1;
    for (; s[i] && s[i] != '*'; i++) {
        const uint8_t c = (uint8_t)s[i];
        if (c < 0x20 || c > 0x7e) { nBad++; return false; }
        sum ^= c;
    }
    if (s[i] != '*') { nBad++; return false; }
    const int hi = hexVal(s[i + 1]), lo = hi < 0 ? -1 : hexVal(s[i + 2]);
    if (hi < 0 || lo < 0 || sum != (uint8_t)((hi << 4) | lo)) { nBad++; return false; }
    // "$ttsss," at least: two-character talker, three-character type.
    if (i < 6) { nBad++; return false; }
    nGood++;

    const char* type = s + 3;          // skip '$' and the 2-char talker
    char f[16];

    if (!is3(type, 'G', 'G', 'A')) {
        if (is3(type, 'G', 'S', 'V')) {
            cnt.gsv++;
            field(s, 3, f, sizeof f);
            noteView(s, (uint8_t)toInt(f));
        } else if (is3(type, 'G', 'S', 'A')) {
            cnt.gsa++;
            field(s, 2, f, sizeof f);
            const int m = toInt(f);
            if (m >= 1 && m <= 3) st.mode = (uint8_t)m;
        } else if (is3(type, 'T', 'X', 'T')) {
            cnt.txt++;
        } else if (is3(type, 'R', 'M', 'C')) {
            cnt.rmc++;
            // RMC field 9 is ddmmyy -- the only place the date appears. Time
            // and date are taken from the same sentence or not at all; a
            // refused date leaves the running clock alone.
            char d[12], tf[16];
            field(s, 9, d, sizeof d);
            field(s, 1, tf, sizeof tf);
            int y, mo, dd;
            const int secs = hmsToSecs(tf);
            if (secs >= 0 && parseDate(d, y, mo, dd)) {
                epoch = toEpochImpl(y, mo, dd, 0, 0, 0) + (uint32_t)secs;
                epochAtMs = nowMs;
            }
        } else {
            cnt.other++;
        }
        return true;
    }

    cnt.gga++;
    field(s, 6, f, sizeof f);
    const int qual = toInt(f);
    st.quality = (uint8_t)(qual > 0 ? qual : 0);
    field(s, 7, f, sizeof f);
    const uint8_t sats = (uint8_t)toInt(f);
    st.sats = sats;
    field(s, 8, f, sizeof f);
    st.hdop100 = toCenti(f);
    // Quality 0: the receiver has no solution this second. Not a reason to
    // drop the fix at once -- one such GGA used to, and it was most of the
    // fix flapping in the field logs. Nothing here changes: no position is
    // taken from it, atMs is not refreshed, and fresh() ages the fix out
    // FRESH_MS after the last good GGA, the window a missing GGA already
    // gets. (Chosen over "3 consecutive": independent of the receiver's
    // output rate, and no new state.) Counted for diag.log.
    if (qual <= 0) { nQual0++; return true; }

    char lat[16], lon[16], ns[4], ew[4];
    field(s, 2, lat, sizeof lat); field(s, 3, ns, sizeof ns);
    field(s, 4, lon, sizeof lon); field(s, 5, ew, sizeof ew);
    // A solution with no coordinates is not a position (it would be 0,0).
    if (!lat[0] || !lon[0] || !ns[0] || !ew[0]) return true;

    cur.used = sats;
    cur.lat7 = toDeg7(lat, ns[0]);
    cur.lon7 = toDeg7(lon, ew[0]);
    cur.valid = true;
    cur.atMs  = nowMs;
    return true;
}

void feed(char c, uint32_t nowMs) {
    cnt.bytes++;
    cnt.lastByteMs = nowMs;
    if (c == '\r' || c == '\n') {
        if (bufOver)   nBad++;
        else if (bufN) { buf[bufN] = 0; sentence(buf, nowMs); }
        bufN = 0; bufOver = false;
        return;
    }
    if (c == '$') {                               // resync on a fresh start
        if (bufN || bufOver) nBad++;              // the line before it was cut off
        bufN = 0; bufOver = false;
    }
    if (bufOver) return;
    if (bufN < (int)sizeof(buf) - 1) buf[bufN++] = c;
    else bufOver = true;
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

const Status& status() { return st; }
const Counts& counts() { return cnt; }

uint32_t good() { return nGood; }
uint32_t bad()  { return nBad; }
uint32_t qualZero() { return nQual0; }

void reset() {
    bufN = 0; bufOver = false; nViews = 0; nGood = 0; nBad = 0; nQual0 = 0;
    epoch = 0; epochAtMs = 0;
    cur = Fix{}; st = Status{}; cnt = Counts{};
}

} // namespace Gnss
} // namespace ui
