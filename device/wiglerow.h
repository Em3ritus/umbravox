// One WigleWifi-1.6 row, WiFi or BLE. Pure (csv.h only), so the bytes are
// pinned on the host (sim/test_wiglerow.cpp, against rows from a field CSV)
// and the SD append path and the console WIGLE dump cannot drift apart.
//
// snprintf into the caller's buffer rather than Print::printf: that mallocs
// for every line over 63 characters, and writes nothing when the malloc fails.
#pragma once
#include <stdint.h>
#include <stdio.h>
#include "csv.h"

namespace wiglerow {

// Returns the row's length (newline included), or 0 if it does not fit --
// never a truncated row.
inline int wifi(char* buf, int cap, const uint8_t* mac6, const char* ssidRaw,
                const char* authStr, uint32_t firstEpoch, int chan, int rssi,
                int32_t lat7, int32_t lon7) {
    char mac[20], ssid[48], la[20], lo[20], when[24];
    csv::mac(mac, mac6);
    csv::quote(ssidRaw, ssid, sizeof ssid);
    csv::deg7(la, lat7);
    csv::deg7(lo, lon7);
    csv::stampUtc(when, firstEpoch);
    // Full WigleWifi-1.6 row: Frequency after Channel, empty RCOIs and
    // MfgrId before Type (we read neither off the air).
    const int n = snprintf(buf, (size_t)cap, "%s,%s,%s,%s,%u,%d,%d,%s,%s,0,%u,,,WIFI\n",
                           mac, ssid, authStr, when, (unsigned)chan,
                           csv::freqMhz(chan), rssi, la, lo, 30u);
    return (n > 0 && n < cap) ? n : 0;
}

// Type BLE, and "Misc [LE]" in the AuthMode column -- the convention
// Bluetooth rows use there, where WiFi puts its cipher suite. Channel and
// Frequency are both 0: BLE advertises on three channels and we do not know
// which one carried this. Empty RCOIs/MfgrId before Type keep the row on the
// full WigleWifi-1.6 column set.
inline int ble(char* buf, int cap, const uint8_t* mac6, const char* nameRaw,
               uint32_t firstEpoch, int rssi, int32_t lat7, int32_t lon7) {
    char mac[20], name[48], la[20], lo[20], when[24];
    csv::mac(mac, mac6);
    csv::quote(nameRaw, name, sizeof name);
    csv::deg7(la, lat7);
    csv::deg7(lo, lon7);
    csv::stampUtc(when, firstEpoch);
    const int n = snprintf(buf, (size_t)cap, "%s,%s,Misc [LE],%s,0,0,%d,%s,%s,0,%u,,,BLE\n",
                           mac, name, when, rssi, la, lo, 30u);
    return (n > 0 && n < cap) ? n : 0;
}

} // namespace wiglerow
