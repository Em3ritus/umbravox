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
    // A prefix that is in none of the tables. 24:0A:C4 is an Espressif block;
    // it used to sit in the Flock list at LOW, so every ESP32 board on that
    // prefix (dev boards, smart plugs, this project's own nodes) false-flagged
    // as a possible Flock. The bare Espressif "Flock" OUIs were pruned in the
    // 2026-10-20 refresh, so it now fires nothing -- the case below guards that.
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

    printf("pruned shared silicon no longer false-flags:\n");
    h = matchWifi(espMac, "");
    check("bare espressif prefix -> nothing", h.threat == TH_NONE);
    // It could be a dev board, a smart plug, a camera or one of our own nodes;
    // the prefix cannot tell them apart, so it is no longer evidence of Flock.

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

    printf("new signatures (2026-10 refresh):\n");
    const uint8_t dji[6]    = {0x60, 0x60, 0x1F, 0x01, 0x02, 0x03};
    const uint8_t skydio[6] = {0x38, 0x1D, 0x14, 0x01, 0x02, 0x03};
    h = matchWifi(dji, "");
    check("DJI OUI -> DRONE at MED",      h.threat == TH_DRONE && h.conf == CONF_MED);
    h = matchWifi(skydio, "");
    check("Skydio OUI -> DRONE at HIGH",  h.threat == TH_DRONE && h.conf == CONF_HIGH);
    h = matchBle(0xFE33, 0);
    check("Chipolo UUID -> TRACKER HIGH", h.threat == TH_TRACKER && h.conf == CONF_HIGH);
    h = matchBle(0xFCB2, 0);
    check("DULT UUID -> TRACKER, MED",    h.threat == TH_TRACKER && h.conf == CONF_MED);
    h = matchBle(0, 0x034D);
    check("Axon company id -> BODYCAM HIGH", h.threat == TH_BODYCAM && h.conf == CONF_HIGH);
    h = matchBle(0, 0x058E);
    check("dropped Meta-Tech id -> nothing", h.threat == TH_NONE);

    printf(fails ? "\n%d FAILED\n" : "\nall passed\n", fails);
    return fails ? 1 : 0;
}
