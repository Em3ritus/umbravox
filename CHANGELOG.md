# Changelog

Notable changes to the UMBRAVOX firmware, theme designer and tools. This file starts with the work below; earlier
history is in git and the release notes.

## Unreleased

### Added
- **Theme layouts (first part of the full UI overhaul).** A pack may carry `layout.json`, which moves, resizes or
  hides the header and footer pieces and Scan's count tiles on the engine screens; every engine screen draws in the
  space they leave, and taps follow the pieces. A layout that breaks a rule (a piece off screen or under 44 px, a
  control hidden, pieces overlapping, too little room) is skipped on its own and logged; the theme still loads.
  Written by hand for now; the designer's Layout step comes later. Built-in themes draw exactly as before
  (`sim/test_engine_pixels` fingerprints all 220 built-in engine renders).
- **Sharing a theme (designer side; the gallery is not live yet).** The designer's Share button saves the pack zip
  and opens a pre-filled submission form on the themes repository; a designer address ending in `?theme=<folder>`
  opens that gallery theme, fetching only a listed theme's four pack files within their size limits.
  `tools/pack-preview.mjs` draws a pack to `cyd.png` and `waveshare.png` with the device's drawing code, for the
  gallery's checks. Not yet run end to end: the themes repository does not exist yet.
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
- Beacon security parsing no longer reads the four checksum bytes at the end of a received frame as an information
  element. About one beacon in 65,000 from an open network could be tagged WPA2. Applies to the handheld and the node.
- The installer page's XIAO download link and the docs' and README's release links no longer go through GitHub's
  "latest" release, which skips pre-releases and pointed at a release with no files (the download was a 404). The
  download is now the ZIP the installer site hosts.
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
