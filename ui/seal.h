// The BAPH0MAU5 centrepiece.
//
// Their art carries the face; the figure around it is drawn rather than
// baked. Lines stay sharp at any size, cost nothing to store, and -- the
// reason that actually matters -- a drawn figure can answer to the state
// where a bitmap can only sit there.
#pragma once
#include <stdint.h>
#include "canvas.h"

namespace ui {

// Draw the seal centred on cx,cy.
//
//   frame       the animation clock
//   found       the search has just turned a contact up; the eyes answer it
//   foundAngle  its bearing, so the eyes can lean a pixel toward it
//   liftOverride  absolute 0..255 brightness, or -1 for the seal's own heartbeat
//   heat        0..255 possession -- how far the fire has spread. 0 is idle.
void drawSeal(Canvas& c, int cx, int cy, int rField, int rRunes, int rStar,
              int rBound, uint32_t frame, bool found, int foundAngle,
              uint8_t heat, int liftOverride = -1);

// Where the eyes sit, as offsets from the seal's centre.
//
// This exists so that ui/sigil.h is included by exactly one translation unit.
// Its arrays are `const` at namespace scope and therefore internal linkage,
// so a second #include does not fail to link -- it silently compiles another
// 7.5 KB copy of them into that object file. The gaze needs these two numbers
// and nothing else from the header.
void sealEyes(int& lx, int& rx, int& y);

} // namespace ui
