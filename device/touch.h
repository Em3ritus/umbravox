// The touch panel, as canvas coordinates.
//
// Two controllers behind one interface, picked by board.h:
//
//   NM-CYD-C5  XPT2046 resistive, on the display's SPI bus (below).
//   WS_C5_35   FT6336 capacitive, on the shared I2C bus at 0x38. It reports
//              panel pixels; touch.cpp rotates them to match the display and
//              takes off the border the 2x canvas does not cover.
//
// NM-CYD-C5 detail. Two things make this awkward on this board. The controller shares SCK/MOSI/
// MISO with the ST7789 but cannot be clocked anywhere near the panel's 40MHz
// -- 2MHz is its comfortable ceiling -- and there is no IRQ line wired
// (RockBase sets CYD28_TouchR_IRQ = -1), so it has to be polled.
//
// Sharing works because Arduino_GFX runs with is_shared_interface = true: it
// takes the HAL lock and re-applies its own clock divider on every
// beginWrite(). So a 2MHz transaction between two of its 40MHz ones is safe,
// provided we take the same lock -- which SPI.beginTransaction() does.
#pragma once
#include <stdint.h>

namespace touch {

struct Point {
    bool     down = false;        // a press on the canvas
    // Any contact at all, including on the WS_C5_35's border outside the
    // canvas. Wakes a dimmed screen, never navigates. Equals `down` on a
    // board whose canvas fills the panel.
    bool     any  = false;
    int16_t  x = 0, y = 0;        // canvas coordinates (0..159, 0..119)
    uint16_t rawX = 0, rawY = 0;  // straight off the controller, for calibration
    uint16_t z = 0;               // pressure estimate
};

// NM-CYD-C5: the shared SPI bus must already be up (setup() does it, before
// the SD card mounts on it too). WS_C5_35: exio::begin() must have run.
void begin();

// Poll. Cheap enough to call every frame: 5 short SPI transactions on the
// NM-CYD-C5, one 7-byte I2C read (rarely up to three) on the WS_C5_35.
Point read();

} // namespace touch
