// When blescan stops and restarts the BLE scan, as a pure decision so it can
// be host-tested (sim/test_blerecycle.cpp).
//
// Why (Task 1 report, section 2): the Arduino BLEScan keeps every distinct
// advertiser in a std::map (a `new BLEAdvertisedDevice` each, ~250-800 B of
// internal heap) until the scan completes or is restarted, and blescan's scan
// runs forever. In the field logs heap fell from ~64 KB to ~16 KB in step with
// the device count, and at that floor BLE counting stopped for the rest of the
// boot. scan->start(..., is_continue = false) clears the map, so restarting
// the scan every RECYCLE_MS (or as soon as the map holds more than MAX_RESULTS)
// bounds it. A scan that has stopped on its own is started again.
//
// Nothing is lost by a recycle: blescan reads every advert in the scan
// callback, the moment it arrives (a passive scan reports at once), and keeps
// its own address table. The library's map is only the library's.
#pragma once
#include <stdint.h>

namespace blerecycle {

enum Action : uint8_t {
    NONE,
    RECYCLE,   // scanning: stop and start again, clearing the result map
    RESTART,   // the scan has stopped on its own: start it again (and log it)
};

constexpr uint32_t RECYCLE_MS  = 12000;   // routine recycle
constexpr uint32_t MAX_RESULTS = 64;      // recycle early past this many results
constexpr uint32_t MIN_GAP_MS  = 1000;    // ...but at most once a second
constexpr uint32_t RESTART_MS  = 5000;    // a stopped scan is retried this often

// `count` is the library's result-map size, `lastMs` the last stop/start this
// decision asked for, `scanning` what the scanner reports, `duty` the BLE share
// of the radio (0 = BLE off: the scan is stopped on purpose, leave it). Elapsed
// times are unsigned, so this holds across the millis() wrap.
inline Action shouldRecycle(uint32_t count, uint32_t nowMs, uint32_t lastMs,
                            bool scanning, uint8_t duty) {
    if (duty == 0) return NONE;
    const uint32_t since = nowMs - lastMs;
    if (!scanning) return since >= RESTART_MS ? RESTART : NONE;
    if (since >= RECYCLE_MS) return RECYCLE;
    if (count > MAX_RESULTS && since >= MIN_GAP_MS) return RECYCLE;
    return NONE;
}

} // namespace blerecycle
