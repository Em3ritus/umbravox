// BLE capture: passive scanning, deduplicated by address.
//
// Passive on purpose. An active scan transmits scan requests to pull extra
// data out of advertisers; a wardriver should listen, not announce itself,
// and passive keeps us off the air.
//
// This shares one radio with the WiFi sniffer, so the two time-share and
// neither runs at the duty cycle it would alone. That cost is real and worth
// watching in the frame counters.
#pragma once
#include <stdint.h>
#include "model.h"
#include "signatures.h"
class Print;
namespace applog { struct Source; }

namespace blescan {

void begin();

// Append BLE blips to the contact field, after cap::fill() has placed the
// WiFi ones. Also publishes the BLE count.
void fill(ui::State& s, uint32_t nowMs);

// Codex rows for the BLE tile: address and signal, newest first.
void fillSights(ui::State& s, int offset);
uint16_t sightCount();
void fillDetail(ui::State& s, int index, uint32_t nowMs);

// 0 hands the whole radio to the WiFi sniffer.
bool lastAlert(ui::AlertRec& out);

// Sweep for trackers that have travelled with us. Call from the main loop;
// it rate-limits itself.
void tick(uint32_t nowMs);
uint16_t followCount();

void setDuty(uint8_t pct);
void reset();

// Emit Bluetooth rows of a WigleWifi-1.6 CSV, after the WiFi ones.
// (The WIGLE console dump; the SD log appends through logSource().)
uint32_t exportRows(Print& out, uint32_t& skipped);

// The table's rows, first-seen order, for the append-only logger (applog.h).
applog::Source& logSource();

// Clear the address table for a log rollover, keeping the dial and alert.
// Main loop only.
void rollover();

uint32_t uniques();
uint32_t adverts();
uint32_t tableDrops();   // adverts from new addresses dropped: table past its insert limit

} // namespace blescan
