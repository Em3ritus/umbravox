// scan_adapter -- drives the SCAN engine (ui/scan_render.h + ui/scan_present.h)
// on the Waveshare panel: a native 480x320 composite, no 2x canvas and no
// letterbox. Device-only glue around the pure core; defined only when the
// engine is opted in, so the default WS build and the whole NM path are byte
// for byte unchanged.
//
//   -DWS_C5_35 -DUMBRAVOX_SCAN_ENGINE   builds and wires this in.
#pragma once
#include <stdint.h>
#include "../ui/model.h"
#include "../ui/nav.h"             // ui::Nav
#include "../ui/scan_render.h"     // scanr::GnssFix, scanr::Rec
#include "../ui/scan_present.h"    // scanpres::PresentMetrics

class Arduino_TFT;

namespace scanadapter {

// The panel, and the logical size the engine composes at (board PANEL_W/H).
void begin(Arduino_TFT* gfx, int W, int H);

// Transition events. Each discards any partially serviced frame, forces a
// full repaint, and swallows touches until that repaint has finished.
void enter();          // the SCAN screen just became current
void wake();           // the screen woke from dim/off
void themeChanged();   // the palette/artwork changed under us
void leave();          // left SCAN: drop the pending frame and clear the panel

bool acceptsTouch();   // a tap must not dispatch against a half-painted screen
bool pending();        // a redraw is mid-flight (not ready for a new snapshot)

// Build and COMMIT the stable snapshot the sliced redraw reads (fills the back
// buffer from live state, diffs it against the front, invalidates the changed
// bands, swaps it in). Call only when !pending().
void snapshot(const ui::State& s, const scanr::GnssFix& gps, scanr::Rec rec,
              bool hasCell, int batt, uint32_t now);

// Render at most budgetStrips strips to the panel and account the time
// (compose vs transfer, worst slice). Returns true when a full frame just
// completed this call.
bool service(int budgetStrips);

// Native touch dispatch against the REVIEWED scanHits geometry, in panel
// coordinates. Mutates nav (screen/filter/log toggle); returns true when the
// screen changed. No detail/threat target is ever fabricated.
bool dispatchTouch(ui::Nav& nav, bool down, int panelX, int panelY);

// Instrumentation for /perf and the console. sampleCapture feeds the capture
// counters (including BLE reports) for accumulated window deltas.
void sampleCapture(uint32_t capFrames, uint32_t capUniq, uint32_t bleReports);
const scanpres::PresentMetrics& metrics();
void clearMetrics();

}  // namespace scanadapter
