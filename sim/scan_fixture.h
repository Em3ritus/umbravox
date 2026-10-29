// Caller-supplied SCAN scene fixtures for host rendering/tests. The compositor
// carries no embedded contact/spectrum data; this is where the demo values live.
#pragma once
#include "../ui/scan_render.h"

namespace scanfix {

// Pre-projected field contacts (bearing, dist 0..100, band, lastSeen): the same
// shape the device supplies from capture's dial state -- no invented identity.
static const scanr::FieldBlip BLIPS[6] = {
    { 20, 15, 0, 1000}, {140, 45, 1, 1500}, { 75, 30, 2,  800},
    {210, 70, 0, 2000}, {300, 85, 1,  500}, {355, 10, 2, 1200},
};
static const int CHANNELS[21] = {20,40,65,100,50,30,45,80,25,60,85,35,72, 55,95,33,70,44,82,30,62};

// A populated scene: fresh 3D fix, recording off, contacts aged a few seconds.
inline scanr::ScanScene demo(uint8_t theme, uint32_t now = 5000) {
    scanr::ScanScene s;
    s.gps = {true, 3, 12, 500};   // valid 3D fix, 12 sats, 0.5s old -> fresh
    s.nodes = 3;
    s.hasCell = true; s.battPct = 78; s.rec = scanr::Rec::Off;
    s.wifi = 1665; s.g5 = 418; s.ble = 2903;
    s.ch = "CH 06"; s.chBand = "2.4 GHz";
    s.blips = BLIPS; s.nBlips = 6;
    s.channels = CHANNELS; s.nChannels = 21;
    s.now = now; s.theme = theme;
    return s;
}

}  // namespace scanfix
