# Waveshare ESP32-C5-Touch-LCD-3.5: the J3 back-panel header

From the board schematic (Waveshare's `ESP32-C5-Touch-LCD-3.5.pdf`, page "PinOut"), checked against the silkscreen strip
on the back panel. The header is 2 x 16. Odd pads are the top row of the strip (as printed), even pads the bottom row.

| Pad | Net | Label | Notes |
|---|---|---|---|
| 1 | VBUS | 5V | |
| 2 | VBAT1 | BAT | |
| 3, 4 | GND | G | |
| 5 | SWDIO | SWD | debug line; not used by the firmware |
| 6, 8, 10, 12, 14, 16, 18, 20 | EXIO8 to EXIO15 | E8 to E15 | I/O expander, not ESP32 pins |
| 7, 9 | NC | NC | |
| 11, 13 | NC / GPIO22, NC / GPIO21 | NC | |
| 15 | GPIO0_OUT | 0 | **not connected to the chip** as shipped (camera D0 selector `R55`, see below) |
| 17 | GPIO1_OUT | 1 | **not connected to the chip** as shipped (camera D1 selector `R58`, see below) |
| 19 | GPIO9_OUT | 9 | the SD card chip select (`R57`): not connected to the chip as shipped, and do not use |
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

**Pads 15, 17, 19, 26 and 28 do nothing on boards as shipped.** Each of GPIO0, 1, 9, 11 and 12 sits on a three-pad
0-ohm selector: the chip, the on-board function (camera data, SD chip select, camera clock and sync) and the header. The
0 ohm part is fitted on the on-board side and the header side is left open (`NC`; `R55`, `R58`, `R57`, `R56`, `R59`).
So `U0TXD` and `U0RXD` (pads 26 and 28) reach GPIO11 and GPIO12 only through `R56` and `R59`, and a UART receiver wired to
any of these pads is never heard. `device/board.h` has said this about pads 26 and 28 since the first units were
checked. For the others the evidence is the BSP project's schematic reading and pin probe on two units
([ESP32-C5-BSP, HARDWARE-FINDINGS](https://github.com/slickrick773/ESP32-C5-BSP/blob/main/docs/HARDWARE-FINDINGS.md): zero
edges on GPIO0, 1, 11 and 12 with a good 3.25 V module) and our own bench test: an ATGM336H (powered, LED on) wired to
pad 15 and then pad 17 delivered no valid NMEA sentence on either. We did not inspect the resistors on our unit.

**A UART GPS (for example the ATGM336H, 9600 baud NMEA) cannot be added by wiring alone on this board.** Moving the 0 ohm
part on `R55` or `R58` from the on-board pad to the header pad would connect GPIO0 or GPIO1 to pad 15 or 17 (not tried;
it costs the camera data pin). Without a rework the options are an I2C GPS or an I2C-to-UART bridge on SDA 27 and SCL 26.
The Waveshare build does not read a UART GPS: it polls an LC76G over I2C (SDA 27, SCL 26, addresses 0x50 and 0x54), so
with no LC76G fitted `GPS` shows 0 bytes and `fix NO GPS`.

Pins the firmware already uses: SPI 6 (SCK), 7 (MOSI), 2 (MISO), 8 (LCD CS), 5 (DC), 9 (SD CS); I2C 26 and 27; audio I2S 23, 10 and 25.
