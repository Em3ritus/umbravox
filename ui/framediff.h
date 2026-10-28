// Dirty regions by comparison: the portrait screens' dirty tracking.
//
// The landscape screens each describe what changed from their State; the
// portrait ones do not have to. frameDiff() compares the finished canvas to a
// shadow copy of the last frame it saw and reports, per horizontal band, the
// span of pixels that differ -- so it is right for any screen by
// construction, and a portrait screen has no dirty logic to get wrong.
//
// The shadow is one canvas's worth (38,400 bytes): PSRAM on the board,
// allocated on first use; a plain static on a host. Without it every frame
// reports the whole canvas, which is slower but still correct.
#pragma once
#include "canvas.h"
#include "screens.h"

namespace ui {

// Bands of this many rows: ceil(160 / 12) = 14, so a 160-row canvas never
// needs more than Dirty::MAX rectangles.
constexpr int DIFF_BAND = 14;

// Fill `d` with the regions of `c` that differ from the last call, then make
// the shadow equal `c`. The first call, and the first after a reset or a
// change of canvas size, reports the whole canvas.
void frameDiff(const Canvas& c, Dirty& d);

// Forget the shadow: the next frameDiff reports everything.
void frameDiffReset();

// Allocate the shadow now if it is not yet, and say whether there is one.
// ui/ has no log; the device asks this when it enters portrait and logs a
// failure once (every portrait frame is then a full blit, slower but right).
bool frameDiffReady();

} // namespace ui
