# Changelog

Notable changes to the UMBRAVOX firmware, theme designer and tools. Earlier history is in git and the release notes.

## v1.4.0 (2026-11-18)

### Added
- **Theme layouts (first part of the full UI overhaul).** A pack may carry `layout.json`, which moves, resizes or
  hides the header and footer pieces and Scan's count tiles on the engine screens; every engine screen draws in the
  space they leave, and taps follow the pieces. A layout that breaks a rule (a piece off screen or under 44 px, a
  control hidden, pieces overlapping, too little room) is skipped on its own and logged; the theme still loads.
  The theme designer's new **Layout** step edits it on the live screen: drag, resize, hide, board tabs with the
  other board drafted, starting layouts, and the device's own rules as checks; a radar video is saved at the placed
  radar size. Scan's radar, spectrum, channel readout and badge
  are pieces too, and the radar can be any square size up to today's: rings for radii without a baked table are
  generated on the device with integer arithmetic (identical to the baked tables where both exist), and
  `radar.vid` may be any square from 24 to 102 pixels. Built-in themes draw exactly as before
  (`sim/test_engine_pixels` fingerprints all 220 built-in engine renders).
- **Piece styles, fonts and pictures.** A layout can give pieces their own font, text, fill and edge colours, a framed
  picture and a background; a per-board screen style sets the title, text and number fonts, the highlight colours, a
  background, a framed panel and the menu icons for Mesh, Menu, Codex, Detail, Files and the other screens; up to eight
  PNG/GIF decor pictures sit under the pieces, one per board animated. The designer turns TTF/OTF uploads into the
  device's pixel font format (`.fnt`) and pictures into its image format (`.art`) at the placed size; USB install
  carries the new files.
- **Alert cards and effects.** A theme can place its own LOW, MED and HIGH alert card on each board, with its own font,
  size and a picture that plays in place of the rite; pieces can flash, pulse or shake while an alert shows. The
  designer's Layout step has an alert mode (LOW / MED / HIGH tabs, Play) and warns when a card covers a control, its
  font is under 7 px, or its picture does not fit. The CYD engine build shows the alert takeover.
- **Speed meter.** The designer estimates each board's p95 frame time for the draft from pixel counts, fitted on the
  boards; over the scan limit, Download, Install and Share stay closed; the alert row only warns. `FRAMELOG`,
  `tools/meterprobe.py` and `tools/meterfit.py` measure and refit it (`docs/design-notes.md`).
- **Alert repeats.** Fixed things alert once per **ALERT REPEAT** (1 min / 10 min / 1 h / until reboot, default
  10 min, Settings > ALERTS); a tracker travelling with you re-alerts every 10 min with the time and distance; a drone
  every 20 min; a rise in confidence always shows. History is unchanged. Settings has five tabs:
  SCANNING | RECORDING | ALERTS | BRIDGE | NODES. ALERT REPEAT wraps round in both directions.
- The designer's Layout step folds Fonts and pictures, Other screens and Hidden pieces, each with a one-line summary.

### Changed
- Scan draws faster: no double VOID fill per strip, pictures decode a row run at a time, and a layer under an opaque
  decor that covers the strip is skipped.
- The theme load job lives in PSRAM.

### Fixed
- The must-read contrast check covers every colour those pieces' words take; two decor showing one picture no longer
  both animate; every strip starts from the built-in text state, so alert words never take a theme's font.
- Designer: drafting the other board keeps pieces' styles and effects and the target board's effect cap; a
  whole-screen alert card stays whole on the other board; card cover warnings follow the device's tap order; a
  malformed alert card in an opened file is named as such; the meter never acts on a stale measurement or shows an
  empty run as 0 ms. `tools/meterfit.py` refuses a run that would drop a board's fit (`--drop-missing` allows it).

## v1.3.0 (2026-11-17)

### Added
- **Sharing a theme.** The designer's Share button saves the pack zip and opens a pre-filled submission form on the
  [community themes](https://github.com/Em3ritus/umbravox-community-themes) repository (a GitHub account is needed);
  automatic checks run the device's theme loader and draw previews, and a maintainer approves a theme before it shows
  in the gallery. A designer address ending in `?theme=<folder>` opens a gallery theme, fetching only a listed theme's
  four pack files within their size limits. `tools/pack-preview.mjs` draws a pack to `cyd.png` and `waveshare.png`
  with the device's drawing code. Themes with a layout cannot be shared yet.

## v1.2.1 (2026-11-17)

### Fixed
- Beacon security parsing no longer reads the four checksum bytes at the end of a received frame as an information
  element. About one beacon in 65,000 from an open network could be tagged WPA2. Applies to the handheld and the node.
- The installer page's XIAO download link and the docs' and README's release links no longer go through GitHub's
  "latest" release, which skips pre-releases and pointed at a release with no files (the download was a 404). The
  download is now the ZIP the installer site hosts.
- The mesh node and coordinator wait for a send result with a wrap-safe timer.

## v1.2.0 (2026-11-16)

### Added
- **Theme art switches.** `theme.json` accepts optional `heptagram` and `contacts` keys, each `"on"` (default) or
  `"off"`. `heptagram: off` skips the drawn heptagram so a pack's own image or radar clip shows clean (on the wide
  Waveshare radar the heptagram was drawn over a clip); `contacts: off` skips the coloured contact dots. Contacts are
  still collected and logged, and the sweep and needle still draw. Emblem and video are never affected, and the alert
  takeover is unchanged. Existing packs and built-in themes render exactly as before. Older firmware ignores the keys.
  The theme designer has two checkboxes, "Show the heptagram" and "Show the contact dots", and its preview honours them.
  Checked on a Waveshare 3.5" with a test pack; the designer was checked in a desktop browser only.
- **Client and probe CSV rollover.** A file that reaches 16 MiB now continues in numbered part files (`-2` to `-8`)
  instead of stopping the export; every part has the header and rows are never split. Rolled-over parts show in the
  Files browser and can be deleted.
- **Change-only client and probe rows.** A record is written the first time a sweep sees it and again only when it
  changed, which keeps long recordings small (an 89-minute recording used to hit the 16 MiB limit).
- **Probe evidence table 1,024 records** (was 512), about 35 KB more PSRAM.
- **Radio coexistence A/B builds** (`ws-c5-35-engine-coexble`, `-coexbal`) for the BLE under-reporting field test.
  Opt-in, not the default; not yet flashed or driven.
- **Tools:** `tools/sdpull.py` (read text files from the card over USB; refuses binary files), `tools/bootprobe.py`
  now reports the engine frame timing, and `tools/perf_report.py` splits `perf.csv` by boot.
- Docs: lessons 16 to 19, a Waveshare J3 pad table, and the design and plan for the art switches.

### Changed
- Active discovery pauses while a session records (the saved setting is untouched and applies again after STOP);
  DISCOVERY TX shows PAUSED REC.
- `SDLS` sizes are 10 characters wide so every size keeps a leading space.

### Fixed
- `perf.csv` `rec` column now reads the recording state (it was always 0); write failures are logged as warnings and
  the perf file is flushed on REC start and stop.
- Boot freeze, probe noise, export failure reason and recording state in the logs (outing fixes).
- A value for `heptagram` or `contacts` that is not the string `"on"` or `"off"` skips the pack with a reason naming
  the key.

### Documentation corrections
- `docs/WAVESHARE-35-J3.md`: pads 15, 17 and 19 are not connected to the chip as shipped (selectors `R55`, `R58`,
  `R57`), the same as pads 26 and 28. A UART GPS cannot be added to this board by wiring alone; the board's GPS path is
  an LC76G over I2C.

### Not yet verified on hardware
- CSV rollover past 16 MiB (host-tested with a lowered cap only), the 1,024-record probe table filling, the two
  coexistence builds, sweep time with a lit screen, and the art switches on the CYD.
