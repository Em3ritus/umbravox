// Caller-supplied DETAIL scene fixtures for host rendering/tests.
#pragma once
#include "../ui/detail_render.h"

namespace detfix {

// A climbing RSSI history (oldest..newest): the signal is getting stronger.
static const int8_t HIST[12] = { -88,-86,-84,-85,-80,-77,-74,-72,-70,-66,-61,-58 };

inline detr::DetailScene demo(uint8_t theme, bool valid = true, uint32_t now = 5000) {
    detr::DetailScene s;
    s.gps = { true, 3, 12, 500 };
    s.nodes = 3; s.hasCell = true; s.battPct = 78; s.rec = detr::Rec::Off;
    s.valid = valid;
    if (valid) {
        s.name = "FLOCK-CAM"; s.mac = "AA:BB:CC:DD:EE:FF"; s.bandChan = "2.4GHZ CH6"; s.band = 0;
        s.rssi = -58; s.auth = "WPA2"; s.authOpen = false;
        s.threat = "FLOCK"; s.conf = 2; s.why = "OUI+SSID";
        s.firstSeen = "3H"; s.lastSeen = "2S";
        s.hist = HIST; s.histN = 12;
    }
    s.theme = theme; s.now = now;
    return s;
}

}  // namespace detfix
