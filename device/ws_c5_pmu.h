// Waveshare ESP32-C5-Touch-LCD-3.5: the AXP2101 PMU, for the battery.
//
// The board's MX1.25 LiPo header is optional, and the whole point of reading
// a PMU rather than a voltage divider is that it can tell the two cases apart:
// a cell is either connected or it is not, and USB is either present or it is
// not. A divider on a board running from USB reads a voltage regardless, and
// that number is a battery level made up on the device's behalf.
//
// So: `cell` gates everything about the battery. With no cell, pct and mv are
// never filled and nothing downstream may show them.
#pragma once
#include <stdint.h>

namespace pmu {

struct Batt {
    bool     ok       = false;   // the AXP2101 answered at all
    bool     usb      = false;   // VBUS present
    bool     cell     = false;   // a cell is connected
    bool     charging = false;   // only meaningful with `cell`
    uint8_t  pct      = 0;       // only meaningful with `cell`
    uint16_t mv       = 0;       // only meaningful with `cell`
};

// On the shared touch bus, which exio::begin() has already opened.
bool begin();

// Cached; re-reads the chip at most every two seconds, because each read is
// several I2C transactions on the bus the touch panel also lives on.
const Batt& read(uint32_t nowMs);

} // namespace pmu
