# Waveshare ESP32-C5-Touch-LCD-2.8 port

Build: `pio run -e ws-c5-28-engine`. This is a separate board target; do not use
its image on the 3.5-inch board or use the 3.5-inch image on this board.
The shared native UI runs at 320x240 landscape. Portrait is not enabled.

## Hardware reference

Waveshare release v1.2.0, source 77a7dceac38a1bd491173b3906205217a382c08c:
https://github.com/waveshareteam/ESP32-C5-Touch-LCD-2.8/tree/v1.2.0

Verified against `schematic/ESP32-C5-Touch-LCD-2.8.pdf`, Arduino
`06_lvgl_demo` and `03_SD_Card`:

| Function | Wiring / implementation |
| --- | --- |
| LCD | ST7789, native 240x320, rotation 1, RGB565, inversion on |
| Shared SPI | SCK 6, MOSI 7, MISO 8; conservative 40 MHz display clock |
| LCD select / command | CS 10, DC 9 |
| SD | CS 23; deselected before display initialization; no auto-format |
| Shared I2C | SDA 0, SCL 1; 100 kHz initially to retain existing GPS timing |
| Touch | CST3530, address 0x58; IRQ on GPIO5 (configured input, currently polled) |
| Expander | 0x24, touch reset EXIO0, LCD reset EXIO1, PWM register 0x05 |
| Speaker | EXIO3 held low; audio not enabled |
| Charger | ETA6098; battery divider feeds expander ADC, not AXP2101 |

The official display initialization registers are supplied to Arduino_GFX;
its address windows and rotation are reused. Reset delays follow the vendor
example. The 3.5-inch EXIO5 power sequence is deliberately not used.

CST3530 reads use four-byte register addresses with repeated START and an
end-read acknowledgement. Failed/short/invalid packets remain distinct from a
valid zero-contact release. The existing sampler, press queue, bus mutex, and
90 ms failure-only bridge are shared. Only the first contact is routed to the UI.
No per-read controller restart is assumed for this different touch controller.

Battery telemetry stays unavailable until the expander ADC/presence contract is
validated. No battery percentage is inferred from USB power. Sensitivity is
shown as FACTORY and is not interactive: the FT6336 threshold register does not
apply to CST3530. Touch test, backlight, and appearance remain available.

## Acceptance before calling this hardware-supported

- Verify colors, orientation and full-screen boundaries.
- Check corners/center, press/release, repeated taps and scrolling.
- Check brightness, idle/wake and screen transitions.
- Confirm SD mount, recording and export without modifying existing files.
- Confirm Wi-Fi/BLE discovery and phone bridge identity (Umbravox_WS_C5_28).
- With GPS absent, confirm UI stays responsive; test LC76G separately when wired.
- Check themes and sustained operation for resets or lost input.

Builds and host tests establish software consistency only. Display orientation,
CST3530 transport behavior, SD and concurrent GPS operation require board testing.
