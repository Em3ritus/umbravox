// Portrait mode (ORIENT_PORTRAIT): the tall 120x160 canvas.
//
// The first version lays out the core screens -- SCAN (with the alert card),
// GRIMOIRE, ATTUNE and FILES. Every other screen draws a placeholder card
// with its name and a BACK control, so portrait never strands the user: the
// way back to landscape is GRIMOIRE -> ATTUNE -> PAGE 2 -> ORIENT.
//
// Nothing here keeps its own dirty state: drawScreen() (ui/draw.h) diffs the
// finished portrait frame against the last one sent (ui/framediff.h).
#pragma once
#include "canvas.h"
#include "model.h"

namespace ui {

// True for the screens that have a portrait layout.
bool portraitCore(uint8_t screen);

// The whole frame for State `s` in portrait; `c` must already be 120x160.
void drawPortrait(Canvas& c, const State& s);

// The pieces, for the screens and the tests.
void drawScanP(Canvas& c, const State& s);
void drawGrimoireP(Canvas& c, const State& s);
void drawAttuneP(Canvas& c, const State& s);
void drawFilesP(Canvas& c, const State& s);
void drawPlaceholderP(Canvas& c, const State& s);
void drawHeaderP(Canvas& c, const State& s, const char* title);
void drawFooterP(Canvas& c, const State& s);
// SCAN's masthead in the 13 characters portrait has: the theme's header if
// it fits, else the theme's name, else the header (cut by drawHeaderP).
const char* portraitTitle(uint8_t theme);

} // namespace ui
