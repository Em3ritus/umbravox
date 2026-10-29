// Caller-supplied SCAN scene fixtures for host rendering/tests. The compositor
// carries no embedded contact/spectrum data; this is where the demo values live.
#pragma once
#include "../ui/scan_render.h"

namespace scanfix {

static const scanr::FieldContact CONTACTS[6] = {
    {"ap:aa:1", -42, 0, 1000}, {"ap:bb:5", -61, 1, 1500}, {"ble:cc", -55, 2,  800},
    {"ap:dd:6", -78, 0, 2000}, {"ap:ee:1", -85, 1,  500}, {"ble:ff", -33, 2, 1200},
};
static const int CHANNELS[21] = {20,40,65,100,50,30,45,80,25,60,85,35,72, 55,95,33,70,44,82,30,62};

// A populated scene: fresh 3D fix, recording off, contacts aged a few seconds.
inline scanr::ScanScene demo(uint8_t theme, uint32_t now = 5000) {
    scanr::ScanScene s;
    s.gps = scanr::Gps::Fresh; s.gpsSats = 12; s.nodes = 3;
    s.hasCell = true; s.battPct = 78; s.rec = scanr::Rec::Off;
    s.wifi = 1665; s.g5 = 418; s.ble = 2903;
    s.ch = "CH 06"; s.chBand = "2.4 GHz";
    s.contacts = CONTACTS; s.nContacts = 6;
    s.channels = CHANNELS; s.nChannels = 21;
    s.now = now; s.theme = theme;
    return s;
}

}  // namespace scanfix
