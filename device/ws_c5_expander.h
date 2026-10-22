// Waveshare ESP32-C5-Touch-LCD-3.5: the CH32V00x I/O expander at 0x24.
//
// It owns the panel's reset, the touch controller's reset and the panel
// power / backlight PWM -- none of which is a GPIO on this board. Register map
// and bring-up follow Waveshare's Board_IO example, verified on this board.
#pragma once
#include <stdint.h>

namespace exio {

// Opens the shared I2C bus (SDA 27 / SCL 26), powers the panel rail, pulses
// the LCD and touch resets, and leaves the backlight off. Must run BEFORE
// gfx->begin(): the panel needs its rail up and its reset released. Runs once;
// a second call is a no-op, because re-pulsing the LCD reset after the panel
// is initialised would blank it. True if 0x24 answered.
bool begin();
bool present();

// Backlight, 0 = off .. 255 = full, through the expander's PWM register.
void backlight(uint8_t duty);

} // namespace exio
