// The State each screen last drew, for its dirty tracking.
//
// Every screen keeps one (2.3 KB each, eleven screens = 25 KB). They used to
// be function-local statics, which on the device is internal RAM -- the
// scarce kind: Wi-Fi, BLE and the heap they need all live there. Here they
// are one array, placed in PSRAM on the board (allocated on first use) and a
// plain static array on a host (sim, tests, the wasm designer). Behaviour is
// unchanged: each screen still has its own slot.
#pragma once
#include "model.h"

namespace ui {

// The slot for screen `scr` (a ui::Screen). Never fails: if neither PSRAM nor
// internal heap can hold the array, every screen shares one static spare,
// which only costs a few full blits (a screen switch forces one anyway).
State& prevState(uint8_t scr);

} // namespace ui
