# Waveshare ESP32-C5-Touch-LCD-3.5: the J3 back-panel header

From the board schematic (Waveshare's `ESP32-C5-Touch-LCD-3.5.pdf`, page "PinOut"), checked against the silkscreen strip
on the back panel. The header is 2 x 16. Odd pads are the top row of the strip (as printed), even pads the bottom row.

| Pad | Net | Label | Notes |
|---|---|---|---|
| 1 | VBUS | 5V | |
| 2 | VBAT1 | BAT | |
| 3, 4 | GND | G | |
| 5 | SWDIO | SWD | the CH32 expander's debug line |
| 6, 8, 10, 12, 14, 16, 18, 20 | EXIO8 to EXIO15 | E8 to E15 | I/O expander, not ESP32 pins |
| 7, 9 | NC | NC | |
| 11, 13 | NC / GPIO22, NC / GPIO21 | NC | |
| 15 | GPIO0_OUT | 0 | free (camera D0 on boards that fit a camera) |
| 17 | GPIO1_OUT | 1 | free (camera D1) |
| 19 | GPIO9_OUT | 9 | the SD card chip select: do not use |
| 21 | GPIO28_OUT | 28 | a C5 strapping pin: avoid |
| 22 | CHIP_PU | RST | |
| 23 | EXIO_ADC | ADC | expander |
| 24 | PWRON | PWR | |
| 25 | ESP32_SCL | SCL | GPIO26, the shared I2C bus (touch, PMIC, expander, codec): do not use |
| 26 | U0TXD | TX | **not connected to the chip** (see below) |
| 27 | ESP32_SDA | SDA | GPIO27, the shared I2C bus: do not use |
| 28 | U0RXD | RX | **not connected to the chip** (see below) |
| 29, 30 | GND | G | |
| 31, 32 | VCC3V3 | 3V3 | |

**TX and RX do nothing on boards as shipped.** `U0TXD` and `U0RXD` reach GPIO11 and GPIO12 only through the selector
resistors `R56` and `R59`, which the schematic marks `0R_NC` (not populated). `device/board.h` has said this since the
first units were checked; a UART receiver wired to pads 26 and 28 is never heard.

**A UART GPS (for example the ATGM336H, 9600 baud NMEA)** needs the module's TXD on a connected GPIO: pad 15 (GPIO0) or
pad 17 (GPIO1), plus 3V3 and GND on pads 30 and 32. The module's RXD stays unconnected because the firmware only listens.
The Waveshare build does not read a UART GPS yet: it polls an LC76G over I2C (SDA 27, SCL 26, addresses 0x50 and 0x54),
so with no LC76G fitted `GPS` shows 0 bytes and `fix NO GPS`.

Pins the firmware already uses: SPI 6 (SCK), 7 (MOSI), 2 (MISO), 8 (LCD CS), 5 (DC), 9 (SD CS); I2C 26 and 27; audio I2S 23, 10 and 25.
