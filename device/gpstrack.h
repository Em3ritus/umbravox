// A short history of where the Core has been.
//
// Node reports arrive in batches, so a sighting can be a couple of seconds
// old by the time it lands. Stamping it with the Core's position at *arrival*
// is wrong by however far the vehicle moved in between -- about 33m at 30mph
// with a 2.5s report interval, which is worse than the GPS error it is
// supposed to be recording.
//
// So each record carries its age, and this answers "where were we then".
#pragma once
#include <stdint.h>

namespace gpstrack {

// Enough history to cover the slowest report interval several times over.
constexpr int DEPTH = 24;          // seconds

void push(uint32_t nowMs, bool valid, int32_t lat7, int32_t lon7);

// Position as of `agoMs` before now. Returns false when there is no fix in
// that part of the history, which is the honest answer -- better than
// quietly substituting the current one.
bool at(uint32_t nowMs, uint32_t agoMs, int32_t& lat7, int32_t& lon7);

// Walk the history oldest-first. Returns false when `i` is past the end.
bool sample(int i, int32_t& lat7, int32_t& lon7);
int  count();

} // namespace gpstrack
