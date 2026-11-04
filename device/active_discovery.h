#pragma once
#include <stdint.h>
#include <esp_wifi.h>
namespace discovery {
using Sink = void (*)(const wifi_ap_record_t&);
// All calls except the internal event callback belong to the loop task.
struct Metrics {uint32_t started=0,completed=0,failed=0,omitted=0;};
const Metrics& metrics();
bool begin();
bool start(uint8_t channel, uint16_t dwell, uint32_t now);
// Returns true while the radio remains owned. A missing completion fails closed.
bool service(uint32_t now, Sink sink);
bool busy();
uint16_t auth(const wifi_ap_record_t& record);
}
