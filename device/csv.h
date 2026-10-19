// Formatting shared by the WiGLE exporters.
//
// Header-only because two modules emit rows into the same file and the
// columns must agree exactly -- a second copy of the coordinate formatter is
// a second chance to disagree about decimal places.
#pragma once
#include <stdint.h>
#include <stdio.h>

namespace csv {

// Degrees x 10^7 to plain decimal, without pulling floating-point printf
// onto a soft-float target.
inline void deg7(char* out, int32_t v) {
    int k = 0;
    if (v < 0) { out[k++] = '-'; v = -v; }
    const int32_t whole = v / 10000000, frac = v % 10000000;
    char w[12];
    int n = 0;
    int32_t x = whole;
    do { w[n++] = (char)('0' + x % 10); x /= 10; } while (x);
    while (n) out[k++] = w[--n];
    out[k++] = '.';
    for (int d = 6; d >= 0; d--) {
        int32_t p = 1;
        for (int e = 0; e < d; e++) p *= 10;
        out[k++] = (char)('0' + (frac / p) % 10);
    }
    out[k] = 0;
}

// Names come off the air and may contain commas or quotes. The field is
// quoted and inner quotes doubled, per RFC 4180.
inline void quote(const char* in, char* out, int n) {
    int o = 0;
    if (o + 1 < n) out[o++] = '"';
    for (int i = 0; in && in[i] && o + 2 < n; i++) {
        if (in[i] == '"' && o + 3 < n) out[o++] = '"';
        out[o++] = in[i];
    }
    if (o + 1 < n) out[o++] = '"';
    out[o] = 0;
}

// "YYYY-MM-DD HH:MM:SS", the only FirstSeen format WiGLE reads as a date.
inline void stampUtc(char* out, uint32_t epoch) {
    const uint32_t days = epoch / 86400, rem = epoch % 86400;
    long z = (long)days + 719468;
    const long era = (z >= 0 ? z : z - 146096) / 146097;
    const unsigned long doe = (unsigned long)(z - era * 146097);
    const unsigned long yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    long y = (long)yoe + era * 400;
    const unsigned long doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned long mp = (5 * doy + 2) / 153;
    const unsigned long d = doy - (153 * mp + 2) / 5 + 1;
    const unsigned long m = mp + (mp < 10 ? 3 : -9);
    y += (m <= 2);
    if (y < 0) y = 0;
    if (y > 9999) y = 9999;   // bound %04ld so the compiler can prove the
                              // output fits; unreachable for any uint32 epoch
                              // (max year ~2106), so behavior is unchanged
    snprintf(out, 24, "%04ld-%02lu-%02lu %02lu:%02lu:%02lu", y, m, d,
             (unsigned long)(rem / 3600), (unsigned long)((rem / 60) % 60),
             (unsigned long)(rem % 60));
}

inline void mac(char* out, const uint8_t* m) {
    snprintf(out, 20, "%02X:%02X:%02X:%02X:%02X:%02X",
             m[0], m[1], m[2], m[3], m[4], m[5]);
}

} // namespace csv
