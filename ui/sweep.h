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
bool searchTouches(uint8_t mode, uint32_t frame, int angle, int dist);

} // namespace ui
