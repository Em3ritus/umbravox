// Host tests for the NMEA parser.
//
// The conversion worth testing is ddmm.mmmm -> degrees: NMEA angles carry
// *minutes*, not a decimal fraction, so parsing one with a plain atof is
// wrong inside every coordinate it produces and wrong in a way that still
// looks like a plausible position.
#include "../ui/gnss.h"
#include <stdio.h>
#include <string.h>

static int fails = 0;

static void eq(const char* what, long got, long want) {
    if (got != want) { printf("  FAIL %-22s got %ld want %ld\n", what, got, want); fails++; }
    else             { printf("  ok   %-22s %ld\n", what, got); }
}

// Feed a string as the UART would, one byte at a time.
static void wire(const char* s, uint32_t nowMs) {
    for (const char* p = s; *p; p++) ui::Gnss::feed(*p, nowMs);
}

// A block of sentences, each with its CRLF, as one second of output.
static void second(const char* const* lines, int n, uint32_t nowMs) {
    for (int i = 0; i < n; i++) { wire(lines[i], nowMs); wire("\r\n", nowMs); }
}
#define SECOND(arr, t) second(arr, (int)(sizeof(arr) / sizeof(arr[0])), t)

// "$body*hh" with the right checksum, for lines the fixtures cannot spell
// literally (control bytes, lengths).
static const char* withCk(const char* body) {
    static char out[400];
    uint8_t ck = 0;
    for (const char* p = body; *p; p++) ck ^= (uint8_t)*p;
    snprintf(out, sizeof out, "$%s*%02X", body, (unsigned)ck);
    return out;
}

// ---------------------------------------------------------------------------
// Fixtures: what a u-blox NEO-6M (GY-GPS6MV2) emits at its defaults, 9600 8N1,
// 1 Hz, GP talker. Checksums computed independently (python XOR), not by the
// parser under test. Sentence order per second is the NEO-6M's: RMC, VTG,
// GGA, GSA, GSV x n, GLL.
// ---------------------------------------------------------------------------

// Boot banner (u-blox 6 firmware 7.03).
static const char* const BOOT[] = {
    "$GPTXT,01,01,02,u-blox ag - www.u-blox.com*50",
    "$GPTXT,01,01,02,HW  UBX-G60xx  00040007 FF7FFFFFp*53",
    "$GPTXT,01,01,02,ROM CORE 7.03 (45969) Mar 17 2011 16:18:34*59",
    "$GPTXT,01,01,02,ANTSUPERV=AC SD PDoS SR*20",
    "$GPTXT,01,01,02,ANTSTATUS=DONTKNOW*33",
};
// Cold start, nothing known: empty time, quality 0, GSA mode 1, GSV 0 in view.
static const char* const NOTIME[] = {
    "$GPRMC,,V,,,,,,,,,,N*53",
    "$GPVTG,,,,,,,,,N*30",
    "$GPGGA,,,,,,0,00,99.99,,,,,,*48",
    "$GPGSA,A,1,,,,,,,,,,,,,99.99,99.99,99.99*30",
    "$GPGSV,1,1,00*79",
    "$GPGLL,,,,,,V,N*64",
};
// Time from the RTC, no date yet, two satellites heard (no almanac positions).
static const char* const TIMEONLY[] = {
    "$GPRMC,235947.00,V,,,,,,,,,,N*73",
    "$GPVTG,,,,,,,,,N*30",
    "$GPGGA,235947.00,,,,,0,00,99.99,,,,,,*68",
    "$GPGSA,A,1,,,,,,,,,,,,,99.99,99.99,99.99*30",
    "$GPGSV,1,1,02,10,,,28,07,,,24*71",
    "$GPGLL,,,,,235947.00,V,N*44",
};
// Time and date decoded from a satellite, still no position.
static const char* const DATED[] = {
    "$GPRMC,101512.00,V,,,,,,,281026,,,N*74",
    "$GPVTG,,,,,,,,,N*30",
    "$GPGGA,101512.00,,,,,0,02,8.47,,,,,,*59",
    "$GPGSA,A,1,10,07,,,,,,,,,,,8.53,8.47,1.00*02",
    "$GPGSV,2,1,07,10,63,137,31,07,61,098,29,05,59,290,20,08,54,157,*7E",
    "$GPGSV,2,2,07,02,39,223,,13,28,070,17,26,23,252,*4C",
    "$GPGLL,,,,,101512.00,V,N*4C",
};
// First fix: 2D, three satellites.
static const char* const FIX2D[] = {
    "$GPRMC,101540.00,A,4717.11437,N,00833.91522,E,0.004,,281026,,,A*7A",
    "$GPVTG,,T,,M,0.004,N,0.008,K,A*2F",
    "$GPGGA,101540.00,4717.11437,N,00833.91522,E,1,03,2.31,499.6,M,48.0,M,,*50",
    "$GPGSA,A,2,10,07,05,,,,,,,,,,2.53,2.31,1.00*05",
    "$GPGSV,2,1,07,10,63,137,33,07,61,098,30,05,59,290,26,08,54,157,18*7B",
    "$GPGSV,2,2,07,02,39,223,,13,28,070,17,26,23,252,*4C",
    "$GPGLL,4717.11437,N,00833.91522,E,101540.00,A,A*68",
};
// 3D, eight used, eleven in view across three GSV sentences.
static const char* const FIX3D[] = {
    "$GPRMC,101601.00,A,4717.11399,N,00833.91590,E,0.012,77.52,281026,,,A*58",
    "$GPVTG,77.52,T,,M,0.012,N,0.022,K,A*09",
    "$GPGGA,101601.00,4717.11399,N,00833.91590,E,1,08,1.01,499.6,M,48.0,M,,*57",
    "$GPGSA,A,3,10,07,05,02,29,04,08,13,,,,,1.72,1.01,1.38*08",
    "$GPGSV,3,1,11,10,63,137,37,07,61,098,35,05,59,290,30,08,54,157,30*71",
    "$GPGSV,3,2,11,02,39,223,19,13,28,070,27,26,23,252,,04,14,186,24*79",
    "$GPGSV,3,3,11,29,09,301,24,16,09,020,,36,,,*76",
    "$GPGLL,4717.11399,N,00833.91590,E,101601.00,A,A*64",
};
// Under a bridge mid-drive: the receiver keeps its clock, loses the solution.
static const char* const LOST[] = {
    "$GPRMC,101830.00,V,,,,,,,281026,,,N*79",
    "$GPGGA,101830.00,,,,,0,00,99.99,,,,,,*6D",
    "$GPGSA,A,1,,,,,,,,,,,,,99.99,99.99,99.99*30",
    "$GPGLL,,,,,101830.00,V,N*41",
};
// A multi-GNSS module (NEO-M8 and later): GN for combined solutions, per-
// constellation GSV.
static const char* const MULTI[] = {
    "$GNRMC,101601.00,A,4717.11399,N,00833.91590,E,0.012,77.52,281026,,,A*46",
    "$GNGGA,101601.00,4717.11399,N,00833.91590,E,1,12,0.81,499.6,M,48.0,M,,*4B",
    "$GNGSA,A,3,10,07,05,02,29,04,08,13,,,,,1.20,0.81,0.90*1B",
    "$GLGSV,1,1,03,65,30,040,30,66,40,120,28,72,15,300,22*5E",
    "$GAGSV,1,1,02,11,40,080,31,12,20,200,27*62",
    "$BDGSV,1,1,01,203,50,100,33*6C",
};
static const long LAT3D = 472852331L;   // 47 + 17.11399/60
static const long LON3D = 85652650L;    //  8 + 33.91590/60

static void neo6m() {
    using namespace ui;
    printf("NEO-6M boot banner:\n");
    Gnss::reset();
    SECOND(BOOT, 100);
    eq("TXT all accepted", (long)Gnss::good(), 5);
    eq("TXT counted", (long)Gnss::counts().txt, 5);
    eq("none bad", (long)Gnss::bad(), 0);
    eq("no fix from TXT", Gnss::fix().valid, 0);
    eq("bytes counted", (long)Gnss::counts().bytes > 200, 1);
    eq("last byte time", (long)Gnss::counts().lastByteMs, 100);

    printf("cold start, step 1: nothing known:\n");
    SECOND(NOTIME, 1000);
    eq("all six good", (long)Gnss::good(), 11);
    eq("none bad", (long)Gnss::bad(), 0);
    eq("RMC counted", (long)Gnss::counts().rmc, 1);
    eq("GGA counted", (long)Gnss::counts().gga, 1);
    eq("GSA counted", (long)Gnss::counts().gsa, 1);
    eq("GSV counted", (long)Gnss::counts().gsv, 1);
    eq("VTG+GLL as other", (long)Gnss::counts().other, 2);
    eq("not fresh: NO GPS", Gnss::fresh(1000), 0);
    eq("quality 0", Gnss::status().quality, 0);
    eq("GSA mode 1 (no fix)", Gnss::status().mode, 1);
    eq("HDOP 99.99", Gnss::status().hdop100, 9999);
    eq("in view 0", Gnss::inView(), 0);
    eq("no date", (long)Gnss::utcEpoch(1000), 0);

    printf("cold start, step 2: RTC time, no date:\n");
    SECOND(TIMEONLY, 2000);
    eq("not fresh", Gnss::fresh(2000), 0);
    eq("in view 2", Gnss::inView(), 2);
    eq("time alone gives no date", (long)Gnss::utcEpoch(2000), 0);

    printf("cold start, step 3: date known, no position:\n");
    SECOND(DATED, 3000);
    eq("not fresh", Gnss::fresh(3000), 0);
    eq("date: log files named dated", (long)Gnss::utcEpoch(3000),
       (long)Gnss::toEpoch(2026, 10, 28, 10, 15, 12));
    eq("GGA sats while q0", Gnss::status().sats, 2);
    eq("no sats in a fix yet", Gnss::fix().used, 0);
    eq("in view 7", Gnss::inView(), 7);
    eq("HDOP 8.47", Gnss::status().hdop100, 847);
    eq("mode 1", Gnss::status().mode, 1);

    printf("cold start, step 4: 2D fix:\n");
    SECOND(FIX2D, 4000);
    eq("fresh: FIXED", Gnss::fresh(4000), 1);
    eq("quality 1", Gnss::status().quality, 1);
    eq("mode 2", Gnss::status().mode, 2);
    eq("used 3", Gnss::fix().used, 3);
    eq("in view 7", Gnss::inView(), 7);
    eq("HDOP 2.31", Gnss::status().hdop100, 231);

    printf("cold start, step 5: 3D fix:\n");
    SECOND(FIX3D, 5000);
    eq("fresh", Gnss::fresh(5000), 1);
    eq("mode 3", Gnss::status().mode, 3);
    eq("used 8", Gnss::fix().used, 8);
    eq("in view 11", Gnss::inView(), 11);
    eq("HDOP 1.01", Gnss::status().hdop100, 101);
    eq("lat", Gnss::fix().lat7, LAT3D);
    eq("lon", Gnss::fix().lon7, LON3D);
    eq("clock from RMC", (long)Gnss::utcEpoch(5000), (long)Gnss::toEpoch(2026, 10, 28, 10, 16, 1));
    eq("still no bad", (long)Gnss::bad(), 0);

    printf("fix lost mid-drive:\n");
    for (uint32_t t = 6000; t <= 11000; t += 1000) SECOND(LOST, t);
    eq("1 s after last good: held", Gnss::fresh(6000), 1);
    eq("mode 1 reported", Gnss::status().mode, 1);
    eq("quality 0 reported", Gnss::status().quality, 0);
    eq("6 s on: NO GPS", Gnss::fresh(11000), 0);
    eq("position kept", Gnss::fix().lat7, LAT3D);
    eq("date kept", (long)Gnss::utcEpoch(11000), (long)Gnss::toEpoch(2026, 10, 28, 10, 18, 30));
    SECOND(FIX3D, 12000);
    eq("sky back: FIXED", Gnss::fresh(12000), 1);
}

static void wireNoise() {
    using namespace ui;
    const char* GGA3D = FIX3D[2];

    printf("brief's no-fix RMC with the empty-time checksum:\n");
    Gnss::reset();
    // *53 is the checksum of "$GPRMC,,V,,,,,,,,,,N"; with a time in the line
    // the real one is *73. A line that disagrees with its checksum is noise.
    Gnss::sentence("$GPRMC,235947.00,V,,,,,,,,,,N*53", 1000);
    eq("rejected", (long)Gnss::bad(), 1);

    printf("checksum error (one bit flipped in latitude):\n");
    Gnss::reset();
    wire("$GPGGA,101601.00,4717.11399,N,00833.91590,E,1,08,1.01,499.6,M,48.0,M,,*57\r\n", 1000);
    wire("$GPGGA,101601.00,4717.11389,N,00833.91590,E,1,08,1.01,499.6,M,48.0,M,,*57\r\n", 2000);
    eq("good one accepted", (long)Gnss::good(), 1);
    eq("bad one rejected", (long)Gnss::bad(), 1);
    eq("position from the good one", Gnss::fix().lat7, LAT3D);
    eq("fix time from the good one", (long)Gnss::fix().atMs, 1000);

    printf("no checksum at all:\n");
    Gnss::reset();
    wire("$GPGGA,101601.00,4717.11399,N,00833.91590,E,1,08,1.01,499.6,M,48.0,M,,\r\n", 1000);
    eq("rejected", (long)Gnss::bad(), 1);
    eq("no fix", Gnss::fix().valid, 0);

    printf("truncated by a dropped byte run, then CRLF:\n");
    Gnss::reset();
    wire("$GPGGA,101601.00,4717.11399,N,00833.91590,E,1,08\r\n", 1000);
    eq("rejected", (long)Gnss::bad(), 1);
    eq("no fix", Gnss::fix().valid, 0);
    wire("$GPGGA,101601.00,4717.11399,N,00833.91590,E,1,08,1.01,499.6,M,48.0,M,,57\r\n", 1000);
    eq("'*' lost: rejected", (long)Gnss::bad(), 2);
    eq("still no fix", Gnss::fix().valid, 0);

    printf("truncated by a new '$' (lost CRLF):\n");
    Gnss::reset();
    wire("$GPGGA,101601.00,4717.11399,N,0083", 1000);
    wire("$GPRMC,101601.00,A,4717.11399,N,00833.91590,E,0.012,77.52,281026,,,A*58\r\n", 1000);
    eq("partial counted bad", (long)Gnss::bad(), 1);
    eq("next line still parsed", (long)Gnss::counts().rmc, 1);
    eq("no fix from the partial", Gnss::fix().valid, 0);

    printf("garbage bytes:\n");
    Gnss::reset();
    wire("\xff\xfe\x80~\x13", 1000);
    wire("\x13\xf0\xa5\r\n", 1000);
    eq("junk line counted bad", (long)Gnss::bad(), 1);
    eq("nothing good", (long)Gnss::good(), 0);
    wire("\xf0\xa5", 1000);
    wire(GGA3D, 1000); wire("\r\n", 1000);
    eq("junk then '$' resyncs", Gnss::fix().valid, 1);
    // A control byte whose checksum happens to match: still noise.
    Gnss::reset();
    wire(withCk("GPGGA,101601.00,4717.1\x01""1399,N,00833.91590,E,1,08,1.01,499.6,M,48.0,M,,"), 1000);
    wire("\r\n", 1000);
    eq("control byte rejected", (long)Gnss::bad(), 1);
    eq("no fix", Gnss::fix().valid, 0);

    printf("very long lines:\n");
    Gnss::reset();
    {
        char body[300];
        int n = snprintf(body, sizeof body, "GPTXT,01,01,02,");
        while (n < 250) body[n++] = 'x';
        body[n] = 0;
        wire(withCk(body), 1000); wire("\r\n", 1000);
    }
    eq("overlong rejected", (long)Gnss::bad(), 1);
    eq("not counted as TXT", (long)Gnss::counts().txt, 0);
    wire(GGA3D, 1000); wire("\r\n", 1000);
    eq("next line fine", Gnss::fix().valid, 1);
    {
        // 82 characters with CRLF: the NMEA 0183 maximum.
        char body[100];
        int n = snprintf(body, sizeof body, "GPTXT,01,01,02,");
        while (n < 76) body[n++] = 'y';
        body[n] = 0;
        const char* s = withCk(body);
        eq("max line is 80 + CRLF", (long)strlen(s), 80);
        wire(s, 1000); wire("\r\n", 1000);
        eq("max-length line accepted", (long)Gnss::counts().txt, 1);
    }

    printf("CRLF split across reads, LF-only, CR-only, blank lines:\n");
    Gnss::reset();
    wire("$GPGGA,101601.00,4717.11399,N,00833.91590,E,1,08,1.01,499.6,M,48.0,M,,*57\r", 1000);
    wire("\n$GPRMC,101601.00,A,4717.11399,N,00833.91590,E,0.012,77.52,281026,,,A*", 1000);
    wire("58", 1000);
    wire("\r", 1000);
    wire("\n", 1000);
    wire("$GPGSV,1,1,00*79\n", 1000);
    wire("$GPGLL,,,,,,V,N*64\r", 1000);
    wire("\r\n\r\n\n", 1000);
    eq("four good", (long)Gnss::good(), 4);
    eq("no bad, blanks ignored", (long)Gnss::bad(), 0);
    eq("fix", Gnss::fix().valid, 1);
    eq("dated", Gnss::utcEpoch(1000) != 0, 1);

    printf("byte-at-a-time over a whole 3D second == whole sentences:\n");
    Gnss::reset();
    SECOND(FIX3D, 1000);
    const long latA = Gnss::fix().lat7, epA = (long)Gnss::utcEpoch(1000);
    const long viewA = Gnss::inView(), goodA = (long)Gnss::good();
    Gnss::reset();
    for (const char* s : FIX3D) Gnss::sentence(s, 1000);
    eq("lat agrees", Gnss::fix().lat7, latA);
    eq("epoch agrees", (long)Gnss::utcEpoch(1000), epA);
    eq("in view agrees", Gnss::inView(), viewA);
    eq("good agrees", (long)Gnss::good(), goodA);
}

static void talkers() {
    using namespace ui;
    printf("multi-GNSS talkers (GN/GL/GA/BD):\n");
    Gnss::reset();
    SECOND(MULTI, 1000);
    eq("all accepted", (long)Gnss::good(), 6);
    eq("GNGGA gives the fix", Gnss::fresh(1000), 1);
    eq("used 12", Gnss::fix().used, 12);
    eq("GNGSA mode 3", Gnss::status().mode, 3);
    eq("GNRMC dates", Gnss::utcEpoch(1000) != 0, 1);
    eq("GL+GA+BD in view", Gnss::inView(), 6);
    eq("GSV counted", (long)Gnss::counts().gsv, 3);
    Gnss::sentence("$BDGGA,101601.00,4717.11399,N,00833.91590,E,1,05,1.40,499.6,M,48.0,M,,*4E", 2000);
    eq("BDGGA accepted", Gnss::fix().used, 5);

    printf("lowercase hex checksum:\n");
    Gnss::reset();
    Gnss::sentence("$GNGGA,101601.00,4717.11399,N,00833.91590,E,1,12,0.81,499.6,M,48.0,M,,*4b", 1000);
    eq("accepted", (long)Gnss::good(), 1);
    eq("fix", Gnss::fix().valid, 1);
}

static void dates() {
    using namespace ui;
    printf("RMC dates:\n");
    struct { const char* s; long want; const char* what; } C[] = {
        {"$GPRMC,000012.00,V,,,,,,,060180,,,N*71", 0, "1980-01-06 GPS epoch default"},
        {"$GPRMC,000012.00,V,,,,,,,010180,,,N*76", 0, "yy 80 (1980/2080)"},
        {"$GPRMC,120000.00,V,,,,,,,010199,,,N*7E", 0, "yy 99 (1999)"},
        {"$GPRMC,120000.00,V,,,,,,,010123,,,N*7F", 0, "2023: before firmware"},
        {"$GPRMC,120000.00,V,,,,,,,320126,,,N*7A", 0, "day 32"},
        {"$GPRMC,12:000.00,V,,,,,,,281026,,,N*7B", 0, "garbled time"},
        {"$GPRMC,000000.00,A,4717.11399,N,00833.91590,E,0.012,77.52,291026,,,A*5E",
         (long)Gnss::toEpoch(2026, 10, 29, 0, 0, 0), "midnight is a time"},
        {"$GPRMC,101512.00,V,,,,,,,281026,,,N*74",
         (long)Gnss::toEpoch(2026, 10, 28, 10, 15, 12), "26 -> 2026"},
    };
    for (auto& c : C) {
        Gnss::reset();
        Gnss::sentence(c.s, 1000);
        eq(c.what, (long)Gnss::utcEpoch(1000), c.want);
    }
    Gnss::reset();
    Gnss::sentence(withCk("GPRMC,120000.00,V,,,,,,,290226,,,N"), 1000);
    eq("29 Feb 2026 (not leap)", (long)Gnss::utcEpoch(1000), 0);
    Gnss::sentence(withCk("GPRMC,120000.00,V,,,,,,,290228,,,N"), 1000);
    eq("29 Feb 2028 (leap)", (long)Gnss::utcEpoch(1000), (long)Gnss::toEpoch(2028, 2, 29, 12, 0, 0));

    printf("a bad date after a good one keeps the clock:\n");
    Gnss::reset();
    Gnss::sentence("$GPRMC,101512.00,V,,,,,,,281026,,,N*74", 1000);
    Gnss::sentence("$GPRMC,000012.00,V,,,,,,,060180,,,N*71", 2000);
    eq("carried, not 1980", (long)Gnss::utcEpoch(2000), (long)Gnss::toEpoch(2026, 10, 28, 10, 15, 13));
}

int main() {
    using namespace ui;
    neo6m();
    wireNoise();
    talkers();
    dates();

    // Canonical example sentence: 48 deg 07.038' N, 011 deg 31.000' E.
    Gnss::reset();
    Gnss::sentence("$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47", 1000);
    const Gnss::Fix& f = Gnss::fix();
    printf("GGA with fix:\n");
    eq("valid", f.valid, 1);
    eq("sats used", f.used, 8);
    eq("lat x1e7", f.lat7, 481173000L);   // 48 + 7.038/60
    eq("lon x1e7", f.lon7, 115166666L);   // 11 + 31.000/60

    // Southern/western hemispheres must negate.
    Gnss::reset();
    Gnss::sentence("$GPGGA,123519,3345.6789,S,15112.3456,W,1,06,1.2,10.0,M,0.0,M,,*47", 1000);
    printf("southern/western:\n");
    eq("lat negative", Gnss::fix().lat7 < 0, 1);
    eq("lon negative", Gnss::fix().lon7 < 0, 1);

    // Quality 0 means no fix, whatever else the sentence carries.
    Gnss::reset();
    Gnss::sentence("$GPGGA,123519,4807.038,N,01131.000,E,0,00,,,M,,M,,*52", 1000);
    printf("quality 0:\n");
    eq("not valid", Gnss::fix().valid, 0);

    // A corrupted sentence must be rejected, not half-applied. The checksum
    // here is deliberately wrong -- the body is the valid sentence above, so
    // if this ever starts passing, the checksum is not being enforced.
    Gnss::reset();
    Gnss::sentence("$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*00", 1000);
    printf("bad checksum:\n");
    eq("rejected", Gnss::bad(), 1);
    eq("not valid", Gnss::fix().valid, 0);

    // GSV is per-constellation: the totals add, they do not overwrite. This is
    // the bug the throwaway probe had -- it reported fewer in view than in use.
    Gnss::reset();
    Gnss::sentence("$GPGSV,3,1,11,01,05,040,20,02,10,080,25,03,15,120,30,04,20,160,35*75", 1000);
    Gnss::sentence("$GLGSV,2,1,07,65,10,030,22,66,20,070,28*6F", 1000);
    printf("GSV across constellations:\n");
    eq("in view summed", Gnss::inView(), 18);   // 11 GPS + 7 GLONASS

    // Byte-at-a-time feed must agree with whole-sentence parsing.
    Gnss::reset();
    const char* s = "$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47\r\n";
    for (const char* p = s; *p; p++) Gnss::feed(*p, 2000);
    printf("byte feed:\n");
    eq("valid", Gnss::fix().valid, 1);
    eq("lat x1e7", Gnss::fix().lat7, 481173000L);
    eq("fresh at +1s", Gnss::fresh(3000), 1);
    eq("stale at +9s", Gnss::fresh(11000), 0);

    // The clock. GGA has the time but no date, so a timestamp built from it
    // alone is right about the hour and wrong about the year.
    Gnss::reset();
    printf("utc clock:\n");
    Gnss::sentence("$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47", 1000);
    eq("GGA alone gives no epoch", Gnss::utcEpoch(1000), 0);

    Gnss::sentence("$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230326,003.1,W*63", 1000);
    const long want = (long)Gnss::toEpoch(2026, 3, 23, 12, 35, 19);
    eq("RMC supplies the date", (long)Gnss::utcEpoch(1000), want);
    eq("clock advances with millis", (long)Gnss::utcEpoch(6000), want + 5);

    // A known instant, checked against a value computed outside this code.
    eq("epoch of 2000-01-01", (long)Gnss::toEpoch(2000, 1, 1, 0, 0, 0), 946684800L);
    eq("epoch of 2026-01-01", (long)Gnss::toEpoch(2026, 1, 1, 0, 0, 0), 1767225600L);

    // Debounce (Task 1 report, 4d): one quality-0 GGA used to clear the fix
    // at once, which is most of the 75 fix losses in the field log. Now a
    // quality-0 GGA changes nothing; the fix ages out at FRESH_MS after the
    // last good GGA, the same window a missing GGA already gets.
    const char* GOOD = "$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47";
    const char* Q0   = "$GPGGA,123520,5000.000,N,01000.000,E,0,00,,,M,,M,,*5E";
    printf("quality-0 debounce:\n");
    Gnss::reset();
    Gnss::sentence(GOOD, 1000);
    Gnss::sentence(Q0, 2000);
    eq("one q0: still fresh", Gnss::fresh(2500), 1);
    eq("q0 counted", (long)Gnss::qualZero(), 1);
    eq("q0 moves no position", Gnss::fix().lat7, 481173000L);
    Gnss::sentence(GOOD, 3000);
    eq("good again: fresh", Gnss::fresh(3500), 1);
    eq("and fresh to +5 s of it", Gnss::fresh(7999), 1);

    Gnss::reset();
    Gnss::sentence(GOOD, 1000);
    for (uint32_t t = 2000; t <= 7000; t += 1000) Gnss::sentence(Q0, t);
    eq("sustained q0: stale by FRESH_MS", Gnss::fresh(7000), 0);
    eq("six q0 counted", (long)Gnss::qualZero(), 6);
    eq("position still the last good one", Gnss::fix().lat7, 481173000L);

    Gnss::reset();
    Gnss::sentence(GOOD, 1000);
    Gnss::sentence("$GPRMC,123521,V,,,,,,,230326,,,N*53", 1500);
    eq("RMC status V does not touch the fix", Gnss::fresh(2000), 1);
    eq("RMC still supplies the date", Gnss::utcEpoch(1500) != 0, 1);
    Gnss::reset();
    eq("reset clears the q0 count", (long)Gnss::qualZero(), 0);

    printf(fails ? "\n%d FAILED\n" : "\nall passed\n", fails);
    return fails ? 1 : 0;
}
