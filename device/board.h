// Which board this is, in one place.
//
// The pins used to be hardcoded in main.cpp, touch.cpp and sdlog.cpp for the
// NM-CYD-C5 alone. A second board would have meant a second copy of each, so
// they live here instead, selected by the env's -D flag:
//
//   -DNM_CYD_C5   RockBase NM-CYD-C5: 2.8" 320x240 ST7789, XPT2046 on the
//                 panel's SPI bus, LEDC backlight, CH340 console.
//   -DWS_C5_35    Waveshare ESP32-C5-Touch-LCD-3.5: 3.5" 480x320 ST7796,
//                 FT6336 on I2C, backlight and resets on a CH32V00x I/O
//                 expander, AXP2101 PMU, native-USB console.
//
// Only device/ includes this. ui/ never knows which board it is on -- it
// draws a 160x120 canvas (120x160 in portrait) and the device decides where
// that lands.
#pragma once
#include <stdint.h>

namespace board {

#if defined(WS_C5_35)

// The display's SPI bus, shared with the SD card. Same SCK/MOSI/MISO as the
// NM-CYD-C5, which is why the C5 SPI notes in main.cpp still apply.
constexpr int8_t PIN_SCK  = 6;
constexpr int8_t PIN_MOSI = 7;
constexpr int8_t PIN_MISO = 2;
constexpr int8_t PIN_CS   = 8;
constexpr int8_t PIN_DC   = 5;
constexpr int8_t PIN_RST  = -1;   // on the I/O expander (EXIO1), not a GPIO
constexpr int8_t PIN_BL   = -1;   // the expander's PWM register, not a GPIO

constexpr int8_t PIN_SD_CS = 9;

// The shared I2C bus: FT6336 touch, the 0x24 expander, the AXP2101, and the
// RTC/IMU/codec this firmware does not use.
constexpr int8_t PIN_SDA = 27;
constexpr int8_t PIN_SCL = 26;

// No GPS UART. The J3 header's UART holes (U0RXD, GPIO12) are not bonded to
// the chip on boards as shipped -- checked on several units -- so a receiver
// wired there is never heard. The LC76G goes over I2C on this bus instead.

// Native 320x480 portrait; Arduino_GFX rotation 1 lands it landscape. touch.cpp
// applies the same turn to the FT6336's native coordinates.
constexpr uint8_t ROTATION = 1;

// Panel, as the UI sees it after rotation.
constexpr int PANEL_W = 480;
constexpr int PANEL_H = 320;

// The canvas goes up 2x (320x240), centred left-to-right and dropped to the
// bottom, which leaves an 80px strip across the top for the battery. 8/3x
// would fill more glass but is not an integer, and a stretch to fill would
// turn every dial into an oval.
constexpr int CANVAS_X = 80;
constexpr int CANVAS_Y = 80;
constexpr bool HAS_STATUS_STRIP = true;

// PORTRAIT: not on this board (first version). The panel is 320x480 native
// and the status strip, the 2x geometry and the FT6336 turn are all laid out
// for landscape; ui::setPortraitAllowed(false) clamps the setting and ATTUNE
// shows "LANDSCAPE ONLY". The tall constants mirror landscape so nothing
// that reads them can land off the glass.
constexpr bool    HAS_PORTRAIT  = false;
constexpr uint8_t ROTATION_TALL = ROTATION;
constexpr int     CANVAS_X_TALL = CANVAS_X;
constexpr int     CANVAS_Y_TALL = CANVAS_Y;

#elif defined(NM_CYD_C5)

// LCD, touch and the SD card all share one SPI bus. Confirmed against
// RockBase's schematic (Documention/2-sch).
constexpr int8_t PIN_SCK  = 6;
constexpr int8_t PIN_MOSI = 7;
constexpr int8_t PIN_MISO = 2;
constexpr int8_t PIN_CS   = 23;
constexpr int8_t PIN_DC   = 24;
constexpr int8_t PIN_RST  = -1;   // tied to the C5's own reset
constexpr int8_t PIN_BL   = 25;   // NOT 27 -- 27 is the WS2812

constexpr int8_t PIN_TOUCH_CS = 1;
constexpr int8_t PIN_SD_CS    = 10;   // README: "SD CS 10"

// GPS on header P5 (NOT P1 -- GPIO 4 appears on both, and only P5 carries the
// receiver's TX). Confirmed on the board: ~9600 baud, measured from a 98us
// minimum pulse width rather than guessed.
constexpr int8_t PIN_GPS_RX = 4;    // we receive on 4
constexpr int8_t PIN_GPS_TX = 5;
constexpr int    GPS_BAUD   = 9600;

// Native 240x320 portrait; rotation 3 lands it landscape, which is how the
// board is cased and how the UI is laid out.
//
// PORTRAIT (ATTUNE page 2 -> ORIENT) drives the panel at ROTATION_PORTRAIT,
// switched live with gfx->setRotation(). Arduino_GFX's 0 is the panel's own
// orientation; which end the USB-C port is at depends on the case, so if
// portrait comes up upside down, change 0 to 2 here -- one line. FLIP_180
// turns BOTH orientations over (landscape 3 -> 1, portrait 0 -> 2), for a
// board mounted the other way up. Touch follows either automatically
// (device/touchmap.h).
constexpr bool    FLIP_180 = false;
constexpr uint8_t ROTATION_LANDSCAPE = 3;
constexpr uint8_t ROTATION_PORTRAIT  = 0;
constexpr uint8_t ROTATION      = FLIP_180 ? (ROTATION_LANDSCAPE + 2) & 3 : ROTATION_LANDSCAPE;
constexpr uint8_t ROTATION_TALL = FLIP_180 ? (ROTATION_PORTRAIT  + 2) & 3 : ROTATION_PORTRAIT;
constexpr bool    HAS_PORTRAIT  = true;

constexpr int PANEL_W = 320;
constexpr int PANEL_H = 240;

constexpr int CANVAS_X = 0;
constexpr int CANVAS_Y = 0;
constexpr bool HAS_STATUS_STRIP = false;
// Portrait: the 120x160 canvas at 2x is the whole 240x320 panel.
constexpr int CANVAS_X_TALL = 0;
constexpr int CANVAS_Y_TALL = 0;

#else
#error "no board selected: build with -DNM_CYD_C5 or -DWS_C5_35"
#endif

} // namespace board
