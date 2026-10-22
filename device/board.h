// Which board this is, in one place.
//
// The pins used to be hardcoded in main.cpp, touch.cpp and sdlog.cpp for the
// NM-CYD-C5 alone. A second board would have meant a second copy of each, so
// they live here instead, selected by the env's -D flag:
//
//   -DNM_CYD_C5   RockBase NM-CYD-C5: 2.8" 320x240 ST7789, XPT2046 on the
//                 panel's SPI bus, LEDC backlight, CH340 console.
//
// Only device/ includes this. ui/ never knows which board it is on -- it
// draws a 160x120 canvas and the device decides where that lands.
#pragma once
#include <stdint.h>

namespace board {

#if defined(NM_CYD_C5)

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
constexpr uint8_t ROTATION = 3;

constexpr int PANEL_W = 320;
constexpr int PANEL_H = 240;

constexpr int CANVAS_X = 0;
constexpr int CANVAS_Y = 0;
constexpr bool HAS_STATUS_STRIP = false;

#else
#error "no board selected: build with -DNM_CYD_C5"
#endif

} // namespace board
