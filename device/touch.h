// XPT2046 resistive touch, sharing the display's SPI bus.
//
// Two things make this awkward on this board. The controller shares SCK/MOSI/
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
    bool     down = false;
    int16_t  x = 0, y = 0;        // canvas coordinates (0..159, 0..119)
    uint16_t rawX = 0, rawY = 0;  // straight off the controller, for calibration
    uint16_t z = 0;               // pressure estimate
};

void begin();

// Poll. Cheap enough to call every frame; does 5 short SPI transactions.
Point read();

} // namespace touch
