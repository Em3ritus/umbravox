// The COSMIC MUFFIN dial centrepiece: the generated muffin bitmap (ui/muffin.h)
// blitted masked to the dial circle, with the same heartbeat/lift the Baphomet
// seal uses, so the candle/pulse searches can light it.
#pragma once
#include <stdint.h>
#include "canvas.h"
#include "themepack.h"

namespace ui {

// Draw the muffin centred on cx,cy, scaled to diameter 2*r and masked to the
// circle of radius r. `liftOverride` is absolute 0..255 brightness (128 =
// unmodified; below sinks each pixel toward pal::VOID, above brightens toward
// white), or -1 for the muffin as drawn. `frame` drives a faint star shimmer.
void drawMuffin(Canvas& c, int cx, int cy, int r, uint32_t frame, int liftOverride);

// The same baked muffin (ui/muffin.h: 72x72, 16 colours) as a theme-pack
// Emblem, so the built-in and pack emblems draw through one routine. Its
// pixels point at MUFFIN_PX; it lives for the whole program.
const themepack::Emblem& muffinEmblem();

} // namespace ui
