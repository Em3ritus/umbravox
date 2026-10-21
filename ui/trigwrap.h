// Keep trig arguments small.
//
// The C5 has no FPU, so sinf/cosf are software, and newlib's fast path covers
// only |x| <= 2^7*(pi/2) ~= 201 rad. Past that every call takes the slow
// Payne-Hanek reduction. An animation phase taken straight from the frame
// counter (or an alert's age) grows without bound, so a minute or so after
// boot the sweeps -- dozens of trig calls a frame -- cross that line and the
// draw time doubles. Reducing the phase to one turn first costs a multiply and
// a floor and keeps every call on the fast path, however long it has run.
//
// Precision: float spacing grows with the argument, so after days of uptime the
// phase of a fast flicker is coarse. These are animation phases; that is
// invisible. Do not use these for anything that must be exact.
#pragma once
#include <math.h>

namespace ui {

constexpr float TWO_PI_F = 6.2831853f;

inline float wrapRad(float x) { return x - TWO_PI_F * floorf(x * (1.0f / TWO_PI_F)); }
inline float wrapDeg(float d) { return d - 360.0f * floorf(d * (1.0f / 360.0f)); }

} // namespace ui
