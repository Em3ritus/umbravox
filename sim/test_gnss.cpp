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

int main() {
    using namespace ui;

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

    printf(fails ? "\n%d FAILED\n" : "\nall passed\n", fails);
    return fails ? 1 : 0;
}
