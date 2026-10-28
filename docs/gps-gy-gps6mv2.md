# GY-GPS6MV2 (u-blox NEO-6M) on the NM-CYD-C5

> **Status: tested against recorded-form fixtures only, not on the real module.**
> The parser has host tests built from what a NEO-6M emits at its defaults
> (`sim/test_gnss.cpp`), and the firmware builds. Nobody has yet wired a
> GY-GPS6MV2 to a CYD and watched it get a fix. The bench checklist at the end is
> how to do that.

The GY-GPS6MV2 is a NEO-6M breakout with a ceramic patch antenna, an on-board
3.3 V regulator, a backup cell and a time-pulse LED. Out of the box it sends NMEA at
**9600 baud, 8N1, 1 Hz**, GP talker, which is exactly what UMBRAVOX expects. No
configuration is needed and UMBRAVOX never sends anything to it.

## Wiring

Four pins on the module, three wires to the CYD:

| GY-GPS6MV2 | NM-CYD-C5 | Notes |
|---|---|---|
| VCC | **3V3** (header P5) | The module's regulator also takes 5 V, so the board's 5 V pin works too. 3.3 V is the simplest. |
| GND | **GND** | Must be shared, or nothing parses. |
| TXD | **P5 RX = GPIO 4** | The only signal UMBRAVOX uses. NEO-6M logic is 3.3 V, safe for the C5. |
| RXD | not connected | UMBRAVOX never writes to the GPS. (GPIO 5, the board's TX, can stay unconnected.) |

Use header **P5**, not P1: GPIO 4 appears on both, and only P5 carries the
receive line to the right place (see `device/board.h`).

**Antenna.** The patch antenna's ceramic side faces the sky, with nothing metal
above it. On a dashboard it works; inside a bag or under a metal roof it will
not. Indoors there is usually no fix at all, or one that takes many minutes.

**First fix.** A cold start (no stored almanac, e.g. after the backup cell has
run flat) takes about 30 s outdoors with clear sky, sometimes a few minutes. A
warm start, with the cell charged from a recent run, is a few seconds. On most
GY-GPS6MV2 boards the LED blinks once a second once there is a fix.

## What the unit shows

- **Footer: `NO GPS`** until the receiver reports a solution (GGA quality 1 or
  more: a 2D or 3D fix). **`FIXED`** while that solution is less than 5 s old.
  A single "no solution" second does not drop it (the debounce); five seconds
  without one does, so a tunnel shows `NO GPS` after about 5 s.
- **Log files** are named with the date (`WD-<date>-<time>.csv`) only once an
  RMC sentence has carried a plausible date. The NEO-6M often knows the date a
  little before it has a position. A session started before that is named
  `WD-NNNN.csv`. Dates from an unset clock (1980, 2080, years before 2024) are
  refused as "no date".

## Expected NMEA

Each second, in this order, at 9600 baud (about 450 bytes):

```
$GPRMC,101601.00,A,4717.11399,N,00833.91590,E,0.012,77.52,281026,,,A*58
$GPVTG,77.52,T,,M,0.012,N,0.022,K,A*09
$GPGGA,101601.00,4717.11399,N,00833.91590,E,1,08,1.01,499.6,M,48.0,M,,*57
$GPGSA,A,3,10,07,05,02,29,04,08,13,,,,,1.72,1.01,1.38*08
$GPGSV,3,1,11,10,63,137,37,07,61,098,35,05,59,290,30,08,54,157,30*71
$GPGSV,3,2,11,02,39,223,19,13,28,070,27,26,23,252,,04,14,186,24*79
$GPGSV,3,3,11,29,09,301,24,16,09,020,,36,,,*76
$GPGLL,4717.11399,N,00833.91590,E,101601.00,A,A*64
```

Before a fix the same sentences arrive with empty fields:

```
$GPRMC,,V,,,,,,,,,,N*53
$GPGGA,,,,,,0,00,99.99,,,,,,*48
$GPGSA,A,1,,,,,,,,,,,,,99.99,99.99,99.99*30
$GPGSV,1,1,00*79
$GPGLL,,,,,,V,N*64
```

At power-up there are also a few `$GPTXT` lines
(`$GPTXT,01,01,02,u-blox ag - www.u-blox.com*50`, the firmware version,
`ANTSTATUS=...`). They are counted and otherwise ignored.

## The `GPS` console command

At 115200 on the USB console, type `GPS`. Three lines:

```
gps: uart rx GPIO 4 @ 9600 (module TXD -> P5 RX; TX unused), bytes 45210, last byte 12 ms ago
gps: nmea good 812 bad 0 | RMC 101 GGA 101 GSA 101 GSV 303 TXT 4 other 202
gps: quality 1 mode 3D, sats 8 in GGA, in view 11, hdop 1.01, date yes, fix FIXED, last at 47.28523,8.56527 used 8, 340 ms old
```

- **bytes / last byte**: is anything arriving at all.
- **good / bad**: does it parse. `bad` counts lines with a wrong or missing
  checksum, cut-off lines, overlong lines and junk. A few on a jumper-wired
  bench is normal; a steady climb is a loose wire.
- **RMC/GGA/GSA/GSV**: about one each per second, GSV one to three.
- **quality / mode / sats / hdop**: what the receiver thinks of the sky. Mode
  `none` and HDOP 99.99 is "no fix yet".

## Hints on the console and in `/diag.log`

Once per boot, at most:

- `gps: no data on GPIO 4: check TXD->P5 RX, 3.3V power, GND` if no byte has
  arrived 10 s after boot.
- `gps: N bytes on GPIO 4 but no valid NMEA: baud mismatch? (expecting 9600)`
  if bytes arrive and not one sentence has parsed.

## Troubleshooting

| Symptom | Likely cause |
|---|---|
| `bytes 0`, "no data" hint | TXD not on P5 RX (GPIO 4), TXD and RXD swapped, no power (measure 3.3 V at VCC; the LED only blinks with a fix), or no shared GND. |
| bytes climbing, `good 0`, "baud mismatch" hint | The module was reconfigured to another baud (e.g. by u-center) and saved. Restore 9600 with u-center on a PC; UMBRAVOX does not change the receiver's settings. A floating RX line (TXD unplugged) can also make junk. |
| `good` climbing, `bad` climbing too | Noise: long or loose jumper wires, a shared GND missing, or the module powered from a noisy 5 V. Shorten wires, check GND. |
| everything parses, `quality 0`, `mode none` | No sky. Go outside, antenna up, wait a few minutes. Indoors this is expected. |
| `in view` > 0 but no fix for a long time | Weak signals (check the GSV SNRs), or a cold start with a flat backup cell. Leave it outside for 5-15 minutes once so it collects the almanac. |
| `date no` while FIXED | Should not happen with a NEO-6M (RMC is in its default set). Check the RMC counter. |

## Bench checklist (not yet done on real hardware)

1. **Wire** VCC to 3V3, GND to GND, module TXD to P5 RX (GPIO 4). Leave RXD
   unconnected. Power the CYD.
2. **`GPS` on the USB console** (115200). `bytes` should be climbing and
   `last byte` under 1000 ms. If not, see the "no data" row above.
3. **Expected counters** after ~10 s: `good` climbing by 6 to 8 a second,
   `bad` 0 or a handful, RMC, GGA and GSA each about one a second, a few `TXT`
   from boot. No hint line in `DIAG`.
4. **Wait for a fix** outdoors, antenna up: `quality 1`, `mode 2D` then `3D`,
   `sats` 4 or more, HDOP falling below about 2. Typically within ~30 s cold;
   on most boards the LED starts blinking once a second.
5. **Footer** changes from `NO GPS` to `FIXED`. Cover the antenna with a hand
   or a tin for 10 s: it should go back to `NO GPS` after about 5 s, and return
   once uncovered.
6. **Log file dated**: with `date yes`, start logging (or let AUTO LOG start it)
   and check with `SDLS` or the FILES screen that the new session file is named
   `WD-<date>-<time>.csv` with today's UTC date, not `WD-NNNN.csv`.
