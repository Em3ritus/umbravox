// The WigleWifi-1.6 document, in one place, so Serial and the SD file emit the
// same bytes. capture.cpp:726 states the rule: main owns the markers and the
// header so the two exporters cannot disagree about them.
#pragma once
#include <stdint.h>

namespace ui_wigle {

// A line sink. On the host a test buffer; on the device a thin wrapper over
// Print (see writeWigle in main.cpp). Keeping header() free of Print makes it
// host-testable.
struct Sink { virtual void put(const char* s) = 0; };

// The preamble line and the column header. Pure; no exporters.
inline void header(Sink& out) {
    out.put("WigleWifi-1.6,appRelease=aetherscan,model=NM-CYD-C5,release=1,"
            "device=esp32c5,display=,board=,brand=AETHERSCAN\n");
    out.put("MAC,SSID,AuthMode,FirstSeen,Channel,RSSI,CurrentLatitude,"
            "CurrentLongitude,AltitudeMeters,AccuracyMeters,Type\n");
}

} // namespace ui_wigle
