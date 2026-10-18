// NMEA 0183 parser. No hardware in it, on purpose.
//
// Nothing here touches a UART, so the simulator and the host can feed it
// canned sentences and the device feeds it bytes off GPIO 4. That split is the
// same one `State` draws: the parser is logic, the wiring lives in device/.
#pragma once
#include <stdint.h>

namespace ui {
namespace Gnss {

struct Fix {
    bool     valid = false;   // a GGA with quality > 0
    int32_t  lat7  = 0;       // degrees x 10^7, north positive
    int32_t  lon7  = 0;       // degrees x 10^7, east positive
    uint8_t  used  = 0;       // satellites in the solution
    uint32_t atMs  = 0;       // millis() the fix landed, for staleness
};

// UTC seconds since the epoch, or 0 before the receiver has reported a date.
//
// GGA carries the time but not the date; only RMC carries the date. A log
// timestamped from GGA alone is right about the hour and wrong about the
// year, which is worse than having no timestamp -- so this stays 0 until an
// RMC has been seen.
uint32_t utcEpoch(uint32_t nowMs);

// Days-from-civil. Exposed for the tests; nothing about it is GNSS-specific.
uint32_t toEpoch(int y, int mo, int d, int h, int mi, int s);

// One character off the wire. Lines are assembled internally; a complete,
// checksum-valid sentence updates state.
void feed(char c, uint32_t nowMs);

// A whole sentence, with or without its line ending. For tests.
bool sentence(const char* s, uint32_t nowMs);

const Fix& fix();

// Satellites in view, summed across constellations.
//
// GSV is emitted per talker (GP, GL, GA, GB...), each reporting only its own
// total, so reading "the last GSV" undercounts and can report fewer in view
// than GGA reports in use. Totals are kept per talker and added.
uint8_t inView();

// A fix older than this is not a position any more.
constexpr uint32_t FRESH_MS = 5000;
bool fresh(uint32_t nowMs);

uint32_t good();   // sentences accepted
uint32_t bad();    // sentences rejected on checksum
void reset();

} // namespace Gnss
} // namespace ui
