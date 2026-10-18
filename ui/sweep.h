// How the dial searches the field.
//
// The rotating radar sweep is a convention borrowed from an instrument, and
// it is the most dashboard-shaped thing on the screen. These are the other
// ways of asking the same question, kept as a setting because which one
// reads best is taste rather than correctness.
#pragma once
#include <stdint.h>
#include "canvas.h"

namespace ui {

// Draw the search. `eyeLx`, `eyeRx` and `eyeY` are offsets from cx,cy and are
// only read by SW_GAZE, which emits from the seal's eyes rather than from the
// middle of the circle.
void drawSearch(Canvas& c, uint8_t mode, int cx, int cy, int rField,
                int eyeLx, int eyeRx, int eyeY, uint32_t frame);

// Is this contact being touched by the search, right now?
//
// Every mode needs its own answer and they are not interchangeable: a sweep
// finds by BEARING, a pulse by DISTANCE, and a wash does not find at all --
// it only shows what is already known.
//
// `dist` is 0..100 and is mapped to a radius exactly as the dial places its
// blips: dist * (R_FIELD - 3) / 100, against the live ui::R_FIELD. The pulse
// detects against the same ring radius that drawSearch draws.
bool searchTouches(uint8_t mode, uint32_t frame, int angle, int dist);

// How brightly a mark on the ring should burn, for a mark at this bearing.
//
// The ring used to key off an implicit radar beam, which is wrong for the
// searches that have no bearing at all -- it animated to a beam that was not
// drawn. Each mode answers for itself: the ones that sweep light the marks
// they pass, and the ones that do not simply hold them steady.
// `floorLum` is the level of a mark the search is not near (the two rings
// have always used different floors).
int searchMarkLum(uint8_t mode, uint32_t frame, float markAngle, int floorLum);

// How brightly the centrepiece should burn this frame, for searches that
// light the field instead of sweeping it.
//
// Returns -1 when the mode has no opinion and the centrepiece should use its
// own rhythm. The candle returns an absolute 0..255 (128 = seal as drawn): it IS the light in the
// circle, so it overrides the heartbeat rather than adding to it.
//
// The candle is primarily a BAPH0MAU5 mode: AETHERSCAN has no bitmap
// centrepiece to light, so there it remains just the faint rings.
int searchLift(uint8_t mode, uint32_t frame);

} // namespace ui
