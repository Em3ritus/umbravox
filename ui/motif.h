// "The theme's picture": one emblem blitter for the built-in muffin and any
// theme pack's art, the {7/3} heptagram, and drawMotif, which picks between
// them so every alert rite is defined for every centrepiece kind.
#pragma once
#include <stdint.h>
#include "canvas.h"
#include "themepack.h"

namespace ui {

// Draw `e` centred on cx,cy, scaled (nearest) to diameter 2*r and masked to the
// circle of radius r. `liftOverride` is absolute 0..255 brightness (128 =
// unmodified; below sinks each pixel toward pal::VOID, above brightens toward
// white), or -1 for the art as drawn. `frame` is reserved for shimmer.
void drawEmblem(Canvas& c, const themepack::Emblem& e, int cx, int cy, int r,
                uint32_t frame, int liftOverride);

// The {7/3} heptagram: seven points, each joined to the one three along.
void heptagram(Canvas& c, int cx, int cy, int r, uint16_t col);

// The active theme's picture at cx,cy,r: its emblem blitted with `lift` when
// pal::emblem(theme) is set, otherwise a heptagram in `col`.
void drawMotif(Canvas& c, uint8_t theme, int cx, int cy, int r, uint32_t frame,
               int lift, uint16_t col);

} // namespace ui
