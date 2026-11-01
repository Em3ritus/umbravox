// Shared native UI device adapter for Waveshare and NM-CYD-C5 engine targets.
// Every screen uses layout(W,H), full-panel hit geometry and bounded rendering.
// Legacy targets without UMBRAVOX_SCAN_ENGINE retain their canvas renderer.
// Physical display, GPS, backlight and touch controllers remain board-specific.
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
void cancelInput();
void invalidatePanel();
void leave();                      // left the engine (to legacy/animation): clear panel

bool acceptsTouch();
bool pending();

// Project live ui::State into the current screen's Store and commit the stable
// snapshot (fills back buffer, diffs, invalidates changed bands, swaps in).
void snapshot(const ui::Nav& nav, const ui::State& s, const scanr::GnssFix& gps,
              scanr::Rec rec, bool hasCell, int batt, uint32_t now, bool pmuOk, bool charging);

// Render at most budgetStrips strips of the current screen; true when a full
// frame just completed.
bool service(int budgetStrips);

// Drain retained press events against the current screen's hit geometry, in panel
// coordinates. Routes through uinav::Router and writes the result back into
// nav + set + the request flags the device loop already consumes. Returns true
// when the screen changed.
bool dispatchTouch(ui::Nav& nav, ui::Settings& set, const ui::State& s);

void serviceSettings(); // apply staged hardware settings outside the input-dispatch stack

void sampleCapture(uint32_t capFrames, uint32_t capUniq, uint32_t bleReports);
const scanpres::PresentMetrics& metrics();
void clearMetrics();

}  // namespace engineadapter
