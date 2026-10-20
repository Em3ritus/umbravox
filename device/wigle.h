// The WigleWifi-1.6 document, in one place, so Serial and the SD file emit the
// same bytes. capture.cpp:726 states the rule: main owns the markers and the
// header so the two exporters cannot disagree about them.
//
// The column set is the full WigleWifi-1.6 layout -- Frequency after Channel,
// and RCOIs/MfgrId before Type -- so the same file uploads unchanged to WiGLE,
// WarDrift and WDGWARS. The strict parser (WDGWARS) checks that the columns
// match the declared version, so declaring 1.6 while emitting the older
// 11-column rows would be rejected there even though WiGLE accepts it.
#pragma once
#include <stdint.h>

namespace ui_wigle {

// A line sink. On the host a test buffer; on the device a thin wrapper over
// Print (see writeWigle in main.cpp). Keeping header() free of Print makes it
// host-testable.
struct Sink { virtual void put(const char* s) = 0; };

// The preamble line and the column header. Pure; no exporters.
inline void header(Sink& out) {
    out.put("WigleWifi-1.6,appRelease=umbravox,model=NM-CYD-C5,release=1,"
            "device=esp32c5,display=,board=,brand=UMBRAVOX\n");
    out.put("MAC,SSID,AuthMode,FirstSeen,Channel,Frequency,RSSI,"
            "CurrentLatitude,CurrentLongitude,AltitudeMeters,AccuracyMeters,"
            "RCOIs,MfgrId,Type\n");
}

} // namespace ui_wigle
