// The one entry point that draws a frame, in either orientation.
//
// The device and the simulator both call drawScreen(); it sizes the canvas
// from s.set.orient and picks the landscape screen (unchanged, with its own
// dirty tracking) or its portrait counterpart (ui/portrait.h, dirty regions
// from a pixel diff, ui/framediff.h).
#pragma once
#include "screens.h"

namespace ui {

// Draw s.screen. `d` as for every screen: nullptr forces nothing, otherwise
// it receives the regions that changed.
void drawScreen(Canvas& c, const State& s, Dirty* d = nullptr);

// Size `c` for an orientation: 160x120 landscape, 120x160 portrait.
void sizeCanvas(Canvas& c, uint8_t orient);

// The orientation changed: every screen's last-drawn State and the portrait
// frame shadow mean nothing now. The next frame of every screen reports the
// whole canvas; the caller still forces a full blit for the frame it is on.
void orientationChanged();

} // namespace ui
