#if defined(WS_C5_35)
#include "ws_c5_expander.h"
#include <Arduino.h>
#include <Wire.h>
#include "board.h"

namespace exio {
namespace {

constexpr uint8_t ADDR       = 0x24;
constexpr uint8_t MODE_REG   = 0x02;   // direction
constexpr uint8_t OUTPUT_REG = 0x03;   // output latch
constexpr uint8_t PWM_REG    = 0x05;   // backlight PWM
constexpr uint8_t TP_RST     = 0;      // EXIO0 -> FT6336 reset
constexpr uint8_t LCD_RST    = 1;      // EXIO1 -> ST7796 reset
constexpr uint8_t POWER      = 5;      // EXIO5 -> panel power / backlight

uint8_t outLatch = 0;
bool    ready    = false;

bool write(uint8_t reg, uint8_t value) {
    Wire.beginTransmission(ADDR);
    Wire.write(reg);
    Wire.write(value);
    return Wire.endTransmission() == 0;
}

bool set(uint8_t pin, bool level) {
    if (level) outLatch |= (uint8_t)(1u << pin);
    else       outLatch &= (uint8_t)~(1u << pin);
    return write(OUTPUT_REG, outLatch);
}

} // namespace

bool present() { return ready; }

// A soft reset (esptool's, a panic, the watchdog) restarts the C5 but not the
// chips on its I2C bus. If it landed mid-read -- touch is read every frame --
// a slave can be left holding SDA low, waiting for clocks that never come.
// Clock it out and finish with a STOP before the controller takes the pins.
static void recoverBus() {
    pinMode(board::PIN_SDA, INPUT_PULLUP);
    pinMode(board::PIN_SCL, OUTPUT_OPEN_DRAIN);
    digitalWrite(board::PIN_SCL, HIGH);
    delayMicroseconds(5);
    for (int i = 0; i < 9 && digitalRead(board::PIN_SDA) == LOW; i++) {
        digitalWrite(board::PIN_SCL, LOW);  delayMicroseconds(5);
        digitalWrite(board::PIN_SCL, HIGH); delayMicroseconds(5);
    }
    // STOP: SDA low -> high while SCL is high.
    pinMode(board::PIN_SDA, OUTPUT_OPEN_DRAIN);
    digitalWrite(board::PIN_SDA, LOW);  delayMicroseconds(5);
    digitalWrite(board::PIN_SCL, HIGH); delayMicroseconds(5);
    digitalWrite(board::PIN_SDA, HIGH); delayMicroseconds(5);
}

bool begin() {
    if (ready) return true;

    recoverBus();

    // 100kHz: the bus runs over the panel flex and is shared by four chips.
    Wire.begin(board::PIN_SDA, board::PIN_SCL, 100000u);

    // Outputs first, then the rail up TOGETHER with both resets released.
    // Resetting the touch chip while its rail is off leaves the FT6336 deaf.
    bool ok = write(MODE_REG, 0xFF) &&
              write(OUTPUT_REG, 0x00) &&
              write(PWM_REG, 0x00) &&
              write(MODE_REG, (uint8_t)((1u << TP_RST) | (1u << LCD_RST) | (1u << POWER)));

    outLatch = (uint8_t)((1u << TP_RST) | (1u << LCD_RST));
    ok = ok && write(OUTPUT_REG, outLatch); delay(50);
    outLatch = 0x00;
    ok = ok && write(OUTPUT_REG, outLatch); delay(50);
    outLatch = (uint8_t)((1u << TP_RST) | (1u << LCD_RST) | (1u << POWER));
    ok = ok && write(OUTPUT_REG, outLatch); delay(50);

    // Rail up; now pulse each reset (active low).
    ok = ok && set(LCD_RST, false); delay(20);
    ok = ok && set(LCD_RST, true);  delay(120);
    ok = ok && set(TP_RST, false);  delay(20);
    ok = ok && set(TP_RST, true);   delay(300);   // FT6336 boot time

    // Backlight stays dark until setup() has cleared the panel, so the first
    // thing on the glass is not a frame of uninitialised RAM.
    ready = ok;
    Serial.printf("exio: I/O expander 0x24 %s\n", ok ? "ready" : "NOT FOUND");
    return ok;
}

void backlight(uint8_t duty) {
    if (ready) write(PWM_REG, duty);
}


} // namespace exio
#endif // WS_C5_35
