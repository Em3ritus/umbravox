// sub_render.h -- a shared control helper for the deeper MENU sub-screens.
//
// The status header and bottom nav bar every engine screen carries now live in
// ONE shared renderer, scanr::drawChrome (scan_render.h): MESH / OMENS / ECHOES
// / ATTUNE / ASPECT / DETAIL / FILES and the main tabs all compose it, so the
// design-1 chrome draws identically everywhere and a chrome fix lands once. What
// remains here is the [-]/[+] stepper button ASPECT shares. Host + device
// compilable (no Arduino deps).
#pragma once
#include "scan_render.h"
#include "layout_engine.h"
#include "palette.h"
#include "font5x7.h"
#include "canvas.h"
#include <cstdio>

namespace subr {

// A framed [-]/[+] stepper button (a >=44px touch target), live or dimmed.
inline void stepBtn(scanr::S& s, const uieng::Rect& r, const char* glyph, bool live) {
    using scanr::box; using scanr::frame; using scanr::centered;
    box(s, r.x, r.y + 1, r.w, r.h - 2, ui::Canvas::mix(pal::PANEL, pal::VOID, 1, 3));
    frame(s, r.x, r.y + 1, r.w, r.h - 2, live ? pal::EDGE_LIT : pal::EDGE);
    centered(s, r.x + r.w / 2, r.y + r.h / 2 - 5, glyph, 2, live ? pal::WITCH_HI : pal::DIM);
}

}  // namespace subr
