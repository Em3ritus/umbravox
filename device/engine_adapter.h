// engine_adapter -- drives the WHOLE declarative UI engine on the Waveshare
// panel: every screen composed natively at 480x320 in bounded strips, touch
// routed through uinav::Router, data projected live from ui::State via
// ui/project.h. Device-only glue around the pure engine core; its body compiles
// only under the opt-in, so the default WS build and the NM path carry none of
// it and keep the legacy canvas renderer + screens.
//
//   -DWS_C5_35 -DUMBRAVOX_SCAN_ENGINE   builds and wires this in.
//
// This supersedes scan_adapter (SCAN-only) for the engine build: main.cpp drives
// this for all screens. scan_adapter.* is left in the tree but unused by this
// path. Nothing here touches device/ capture/mesh/detection internals.
#pragma once
#include <stdint.h>
#include "../ui/model.h"
#include "../ui/nav.h"
#include "../ui/scan_render.h"     // scanr::GnssFix, scanr::Rec
#include "../ui/scan_present.h"    // scanpres::PresentMetrics

class Arduino_TFT;

namespace engineadapter {

void begin(Arduino_TFT* gfx, int W, int H);

// Transition events for the CURRENT screen. uiScreen is a ui::Screen; enter()
// makes it current, forces a full native repaint and gates touch until done.
void enter(uint8_t uiScreen);
void wake();
void themeChanged();
void leave();                      // left the engine (to legacy/animation): clear panel

bool acceptsTouch();
bool pending();
// True while a screen ENTRY full-repaint is in progress (enter/wake/theme),
// until its final strip lands. The loop blanks the backlight across it so the
// entry appears whole instead of painting in -- a brief blank, not a curtain.
// NOT set by an ordinary touch-burst (a partial repaint does not curtain).
bool entering();

// Project live ui::State into the current screen's Store and commit the stable
// snapshot (fills back buffer, diffs, invalidates changed bands, swaps in).
void snapshot(const ui::Nav& nav, const ui::State& s, const scanr::GnssFix& gps,
              scanr::Rec rec, bool hasCell, int batt, uint32_t now);

// Render at most budgetStrips strips of the current screen; true when a full
// frame just completed.
bool service(int budgetStrips);

// Native touch against the current screen's reviewed hit geometry, in panel
// coordinates. Routes through uinav::Router and writes the result back into
// nav + set + the request flags the device loop already consumes. Returns true
// when the screen changed.
bool dispatchTouch(ui::Nav& nav, ui::Settings& set, const ui::State& s,
                   bool contact, int panelX, int panelY);

void sampleCapture(uint32_t capFrames, uint32_t capUniq, uint32_t bleReports);
const scanpres::PresentMetrics& metrics();
void clearMetrics();

}  // namespace engineadapter
