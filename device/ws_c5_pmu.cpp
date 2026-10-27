#if defined(WS_C5_35)
#include "ws_c5_pmu.h"
#include <Arduino.h>
#include <Wire.h>
#define XPOWERS_CHIP_AXP2101
#include <XPowersLib.h>
#include "board.h"

namespace pmu {
namespace {

XPowersAXP2101 axp;
Batt           cur;
uint32_t       lastMs = 0;
bool           primed = false;

constexpr uint32_t PERIOD_MS = 2000;

void sample() {
    Batt b;
    b.ok   = true;
    b.usb  = axp.isVbusIn();
    b.cell = axp.isBatteryConnect();
    if (b.cell) {
        b.charging = axp.isCharging();
        const int p = axp.getBatteryPercent();
        b.pct = (uint8_t)(p < 0 ? 0 : (p > 100 ? 100 : p));
        b.mv  = axp.getBattVoltage();
    }
    // Log transitions, not samples: plugging a cable in or out is the event
    // worth seeing on the console.
    if (!primed || b.usb != cur.usb || b.cell != cur.cell || b.charging != cur.charging) {
        if (b.cell)
            Serial.printf("pmu: %s, cell %u%% %u mV%s\n", b.usb ? "USB" : "battery",
                          (unsigned)b.pct, (unsigned)b.mv, b.charging ? ", charging" : "");
        else
            Serial.printf("pmu: %s, no cell\n", b.usb ? "USB" : "NO USB?");
    }
    cur = b;
    primed = true;
}

} // namespace

bool begin() {
    // XPowersLib re-runs Wire.begin on the pins it is given; same bus, same
    // pins, so that is harmless. Left at the 100kHz the expander set.
    if (!axp.begin(Wire, AXP2101_SLAVE_ADDRESS, board::PIN_SDA, board::PIN_SCL)) {
        Serial.println("pmu: AXP2101 did not answer -- no battery readout");
        cur = Batt{};
        return false;
    }
    axp.enableBattDetection();
    axp.enableVbusVoltageMeasure();
    axp.enableBattVoltageMeasure();
    sample();
    lastMs = millis();
    return true;
}

const Batt& read(uint32_t nowMs) {
    if (cur.ok && nowMs - lastMs >= PERIOD_MS) {
        lastMs = nowMs;
        sample();
    }
    return cur;
}

} // namespace pmu
#endif // WS_C5_35
