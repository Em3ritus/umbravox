# Alert sounds (Waveshare 3.5" only)

A new alert beeps once, with a pattern per confidence level so it can be told by ear:

| Level | Sound |
|---|---|
| LOW | one beep, 1047 Hz, 220 ms |
| MED | two beeps, 1175 Hz, 110 ms each |
| HIGH | six notes alternating 1568 and 988 Hz, 140 ms each |

A pack can replace the beeps with its own sounds (`alert.snd`, see `docs/theme-packs.md`); a level it leaves out keeps the built-in beep.
Only a *new* alert sounds (the same device re-detected holds its card and stays quiet, as it does for waking the screen).
The OMENS test button walks all three levels. Nothing plays when alerts are OFF.

Volume: Settings, recording tab, ALERT SOUND: OFF, QUIET, NORMAL (default), LOUD. Changing it plays a sample. OFF is a mute.

## How it works
- `ui/alert_tone.h`: the patterns as 16 kHz mono samples, integers only, host-tested by `sim/test_alert_tone.cpp`.
- `device/sound.cpp`: ES8311 codec on the shared I2C bus (0x18) and the I2S output (BCLK 23, LRCLK 10, DOUT 25, no MCLK; the
  codec takes its clock from BCLK). Pins and codec address come from Waveshare's factory code for this board. The amplifier
  enable is expander bit 5, which `ws_c5_expander.cpp` already sets at boot. Register sequence: Espressif's es8311 driver.
- A cold start needs about 0.85 s before sound comes through (0.3 s lost the front of every beep; 1 s was clean on the
  board). So the first alert after a quiet spell starts ~0.85 s late, and the task then keeps the clocks running on
  silence for 15 s so the next alerts play at once. While it lingers it holds ~7 KB of internal RAM (DMA + task stack).

## Gotchas
- `LOW` and `HIGH` are Arduino macros: never name an array that in a header the firmware includes.
- LOW used to be 880 Hz for 120 ms and was inaudible on this speaker. Keep notes above ~1 kHz and over ~100 ms.
- Boot heap: the I2S driver cost 1 KB at link time; `device/active_discovery.cpp` now reads scan results into PSRAM
  instead of a 3 KB static, which put boot heap at 57 KB.
- Not verified: the 2.8" board (speaker kept disabled), the CYD (no-op), loudness choices (AMP 12000, codec 70%).
