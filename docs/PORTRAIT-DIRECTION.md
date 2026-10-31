# Portrait direction / SLICK

Design preview, not yet a device implementation. Baseline remains 12bd983;
no display rotation, touch mapping, firmware, or board state changed here.

320x480 Waveshare target; 240x320 compact study. Shared visual rules:
- Identity row: 44px, Menu left, SLICK, icon-only SD/battery right.
- Status row: 44px, GPS / Nodes / Bridge; three equal touch regions.
- Footer: 44px, Scan / Domain / Codex / POI / Record; equal widths >=48px.
- SCAN: three equal count tiles across, live field underneath, spectrum bottom.
- DOMAIN: compact north-up map and range controls above full-width AP records.
- CODEX: full-width rows, complete BSSID, security/channel/RSSI, detail on tap.
- MENU: two columns, three rows of secondary destinations.

The host study uses the real font, palette, theme parser and drawing primitives.
Its scene values are fixtures, and it does not claim runtime hit-testing or touch
rotation validation. The theme pack is usable independently of portrait.

Implementation work must generalize native engine geometry (not enable the old
120x160 portrait canvas), carry rotated dimensions into pagination and gestures,
reuse existing frame allocations safely, and reset pending touch epochs when turning.
Add orientation to Display, separate from Appearance, and preserve landscape.
Validate every screen and touch region at 320x480 and compact 240x320, including
settings editors, details, alerts and file dialogs. Round radar stays round.

The Waveshare board currently declares HAS_PORTRAIT=false and aliases tall rotation
to landscape. These must change together with FT6336 mapping, not in isolation.
The 12bd983 firmware leaves only 30,784 bytes in its current app partition. Check
final image size before any device build is accepted; do not silently repartition.

Reproduce previews (host only):

```sh
c++ -std=c++17 -O2 -Wall -Wextra -fsanitize=address,undefined sim/portrait_theme_preview.cpp ui/palette.cpp ui/themepack.cpp ui/muffinseal.cpp ui/motif.cpp ui/canvas.cpp ui/font5x7.cpp -o /tmp/portrait-theme-preview -lm
mkdir -p /tmp/slick-preview
/tmp/portrait-theme-preview packs/slick/theme.json /tmp/slick-preview
```

SLICK visual revision: orange diamond and clean radar now replace the initial
electric-blue heptagram direction. See SLICK-CLEAN-RADAR.md.
