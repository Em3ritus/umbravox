// Host tests for signature matching.
//
// The cases that matter are the ones that must NOT fire: a matcher that
// alerts on an Apple phone or a bare ESP32 is worse than no matcher, because
// it teaches you to dismiss the alert.
#include "../ui/signatures.h"
#include <stdio.h>
#include <string.h>

static int fails = 0;

static void check(const char* what, bool ok) {
    printf(ok ? "  ok   %s\n" : "  FAIL %s\n", what);
    if (!ok) fails++;
}

int main() {
    using namespace ui;
    // A prefix that is in none of the tables. The first version of this
    // test used 24:0A:C4 as an "arbitrary" MAC -- which turns out to be an
    // Espressif block, and therefore sits in the Flock list at LOW. The test
    // accidentally reproduced the exact false positive the grading exists
    // for, so the case below is now kept on purpose.
    const uint8_t anyMac[6] = {0xDE, 0xAD, 0xBE, 0x11, 0x22, 0x33};
    const uint8_t espMac[6] = {0x24, 0x0A, 0xC4, 0x11, 0x22, 0x33};

    printf("named devices:\n");
    Hit h = matchWifi(anyMac, "AB3-X27741");
    check("axon SSID -> BODYCAM", h.threat == TH_BODYCAM);
    check("axon graded HIGH",     h.conf   == CONF_HIGH);

    h = matchWifi(anyMac, "Pineapple_4F2A");
    check("pineapple -> PENTEST", h.threat == TH_PENTEST);
    check("pineapple graded HIGH", h.conf  == CONF_HIGH);

    h = matchWifi(anyMac, "ab3-lowercase");
    check("SSID match is case-insensitive", h.threat == TH_BODYCAM);

    printf("must not fire:\n");
    h = matchWifi(anyMac, "NETGEAR58");
    check("ordinary SSID -> nothing", h.threat == TH_NONE);
    h = matchWifi(anyMac, "");
    check("hidden SSID -> nothing",   h.threat == TH_NONE);
    h = matchWifi(anyMac, nullptr);
    check("null SSID is safe",        h.threat == TH_NONE);
    h = matchWifi(anyMac, "ABBEY ROAD");
    check("'AB' alone is not 'AB2-'", h.threat == TH_NONE);

    printf("shared silicon is weak evidence:\n");
    h = matchWifi(espMac, "");
    check("espressif prefix -> FLOCK", h.threat == TH_FLOCK);
    check("...but only LOW",           h.conf   == CONF_LOW);
    check("so a MED floor hides it",   h.conf   <  CONF_MED);
    // The point of the floor: this is a dev board, a smart plug or a camera,
    // and the prefix cannot tell them apart.

    printf("ble:\n");
    h = matchBle(0xFEED, 0);
    check("tile UUID -> TRACKER", h.threat == TH_TRACKER && h.conf == CONF_HIGH);
    h = matchBle(0xFD5F, 0);
    check("meta UUID -> GLASSES", h.threat == TH_GLASSES);
    h = matchBle(0, 0x004C);
    check("apple company id -> TRACKER", h.threat == TH_TRACKER);
    check("...but only LOW: every Apple device sends it", h.conf == CONF_LOW);
    h = matchBle(0, 0);
    check("no evidence -> nothing", h.threat == TH_NONE);
    h = matchBle(0x1234, 0x9999);
    check("unknown ids -> nothing", h.threat == TH_NONE);

    printf("strongest evidence wins:\n");
    h = matchBle(0xFEAA, 0x004C);     // both LOW
    check("two weak signals stay LOW", h.conf == CONF_LOW);

    printf(fails ? "\n%d FAILED\n" : "\nall passed\n", fails);
    return fails ? 1 : 0;
}
