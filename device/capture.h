// WiFi capture: promiscuous sniffing across both bands, deduplicated.
//
// Hardware-side by nature, so it lives here rather than in ui/. It owns the
// radio and publishes into a ui::State, which is the only thing the screen
// ever sees.
#pragma once
#include <stdint.h>
#include "model.h"
#include "signatures.h"
class Print;

namespace cap {

void begin();

// Advance the channel hop. Call every loop; it decides when to move.
void tick(uint32_t nowMs);

// Publish counts, per-channel load and the contact field into `s`.
void fill(ui::State& s, uint32_t nowMs);

// Fill the codex rows, newest first, starting `offset` rows down.
// `only5` restricts to the 5GHz band, which is what the 5 GHZ tile opens.
void fillSights(ui::State& s, int offset, bool only5);
uint16_t sightCount(bool only5);

// Everything known about the device at `index` in that filtered list.
void fillDetail(ui::State& s, int index, bool only5, uint32_t nowMs);

// The strongest recent signature hit. False if nothing has ever matched.
bool lastAlert(ui::AlertRec& out);

// Fold in a sighting that a node heard, through the same dedup and counting
// path as our own.
// `ageMs` is how long ago the node heard it, so the Core can date the fix.
// `src` is 0 for this radio, or a node's slot + 1 -- whoever gets credit for
// first discovering the device.
void ingest(const uint8_t* bssid, int8_t rssi, uint8_t chan, uint8_t band,
            const char* ssid, uint32_t ageMs, uint8_t src, uint16_t auth);

// Devices first discovered by that source. A node camped on a busy channel
// relays a great deal while discovering very little; this is the number that
// says whether it is earning its place.
uint16_t foundBy(uint8_t src);

// Deauthentication/disassociation frames: total, and the rate in the last
// window. A flood is the one finding here that is measured rather than
// matched.
// Project located devices onto the territory map at the given scale.
void fillPlots(ui::State& s, int rangeM, int32_t myLat7, int32_t myLon7, bool haveFix);

// Probe requests: clients calling out for networks they have joined before.
void fillEchoes(ui::State& s, int offset, uint32_t nowMs);
uint16_t echoCount();
uint32_t probeFrames();

uint32_t deauths();
uint32_t deauthPerS();

// Emit WiFi rows of a WigleWifi-1.6 CSV. The caller writes the markers and
// the column header; `skipped` accumulates rows dropped for want of a
// position or a clock.
uint32_t exportRows(Print& out, uint32_t& skipped);

// How many known APs carry a position. The gap between this and uniques()
// is what will not upload to WiGLE.
uint16_t locatedCount();

void setDwell(uint16_t ms);
// Hold one channel instead of sweeping. 0 resumes the sweep.
void setCamp(int chan);
bool camping();
void setBands(uint8_t mode);   // 0 both, 1 = 2.4 only, 2 = 5 only
void reset();

// Diagnostics for the console.
uint32_t frames();      // every sighting in the table, local or relayed
uint32_t localFrames(); // only what this radio heard
uint32_t uniques();     // distinct BSSIDs
uint16_t tableUse();    // dedup slots used
uint16_t tableCap();

} // namespace cap
