// Host test for the WiGLE document framing. Only the preamble and column
// header are pure and testable here; the rows come from device-only exporters
// and are exercised by the WIGLE console command on hardware.
#include "../device/wigle.h"
#include <stdio.h>
#include <string.h>

static int fails = 0;
static void check(const char* what, bool ok) {
    printf(ok ? "  ok   %s\n" : "  FAIL %s\n", what);
    if (!ok) fails++;
}

// The sink wigle::header writes into. On device this is backed by Print; here
// by a growing buffer. See the Sink definition in wigle.h.
struct Buf : ui_wigle::Sink {
    char s[512]; int n = 0;
    void put(const char* t) override { for (int i = 0; t[i] && n < 511; i++) s[n++] = t[i]; s[n] = 0; }
};

int main() {
    Buf b;
    ui_wigle::header(b);
    const char* want =
        "WigleWifi-1.6,appRelease=umbravox,model=NM-CYD-C5,release=1,"
        "device=esp32c5,display=,board=,brand=UMBRAVOX\n"
        "MAC,SSID,AuthMode,FirstSeen,Channel,Frequency,RSSI,"
        "CurrentLatitude,CurrentLongitude,AltitudeMeters,AccuracyMeters,"
        "RCOIs,MfgrId,Type\n";
    check("preamble and column header are exact WigleWifi-1.6", strcmp(b.s, want) == 0);

    printf(fails ? "\n%d FAILED\n" : "\nall passed\n", fails);
    return fails ? 1 : 0;
}
