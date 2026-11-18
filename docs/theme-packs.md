# Theme packs

A **theme pack** is a small folder on the SD card that changes how UMBRAVOX looks:
its colours, the centrepiece picture on the scan screen, which sweeps it offers and
which alert rite it uses. The device reads packs at boot. A pack is **data only**:
two files (a third, optional radar video on the Waveshare), no code. Nothing in a pack can touch the radios, the SD card or any
network, so UMBRAVOX stays passive however a pack is written.

You do not need a toolchain. The [theme designer](https://em3ritus.github.io/umbravox/designer/)
builds a pack in the browser, with a live preview drawn by the device's own `ui/`
code compiled to WebAssembly. This page is the reference for the format, the limits
and everything a pack cannot do.

## Bundled palettes and header names

Ten themes are available without an SD card. The general palettes are
UMBRAVOX, EMBER, GLACIER, OBSIDIAN, CYBER, and ULTRAVIOLET.
The additions use the clean radar and require no bitmap artwork. General
palettes keep **UMBRAVOX** as the header; Appearance still shows each palette's
name. Crew themes retain their individual mastheads.

Existing built-in IDs 0–7 are unchanged; the six new palettes occupy IDs 8–13.
The twelve-slot SD pack/override limit is separate from the built-in count and
has not increased. All bundled palettes remain available even when that SD
limit is reached. Older EMBER SD overrides also retain the UMBRAVOX masthead.

## What a pack cannot do

- No scripting and no custom animation. The format is data only and stays that way
  in this version.
- No Gaze sweep and no Summon rite. They belong to the Baphomet seal.
- A layout (`layout.json`, the designer's Layout step) moves, resizes, hides and restyles the pieces the device draws
  on the engine screens, with pixel fonts and pictures, but cannot add new kinds of piece.
- No hiding or disabling alerts.
- It cannot use the Baphomet seal bitmap (a pack can choose a heptagram instead).

## Folder layout

```
/themes/                  at the root of the FAT32 SD card
  midnight-ember/         one folder per pack
    theme.json            required
    emblem.bin            required only when "centrepiece" is "emblem"
    radar.vid             optional: a looping video for the Waveshare's radar
    alert.snd             optional: your own alert sounds for the Waveshare's speaker (LOW, MED, HIGH)
    layout.json           optional: where the pieces of the engine screens sit (the designer's Layout step)
```

The designer writes these files. Any other file in a pack folder
(a `preview.png`, a readme) is ignored by the device.

## theme.json

```json
{
  "format": 1,
  "name": "MIDNIGHT EMBER",
  "author": "@yourhandle",
  "header": "MIDNIGHT EMBER",
  "dial": "large",
  "centrepiece": "emblem",
  "sweeps": ["radar", "galaxy", "orbit"],
  "sweep": "galaxy",
  "alert": "supernova",
  "colors": { "void": "#09071C", "panel": "#16103A", "grid": "#...", "...": "..." }
}
```

| key | required | type and values | default when omitted |
|---|---|---|---|
| `format` | yes | the integer `1`. A higher number is skipped as "newer than this firmware". | none |
| `name` | yes | 1-20 characters from `A-Z 0-9` space `-` `.` (upper case). It is also the key the choice is saved by. | none |
| `author` | no | up to 24 printable ASCII characters; informational. | empty |
| `header` | no | 1-18 characters in the font (ASCII 32..95: capitals, digits, space, common punctuation); lowercase is shown as capitals. Shown in the scan-screen header. | the name, truncated to 18 characters |
| `dial` | no | `compact` (UMBRAVOX geometry, with the spectrum strip) or `large` (BAPH0MAU5 / COSMIC geometry, no strip). | `compact` |
| `centrepiece` | no | `heptagram` (UMBRAVOX's drawn figure, lattice, range rings and rune ring) or `emblem` (a picture from `emblem.bin` filling the dial, as COSMIC MUFFIN does). | `heptagram` |
| `heptagram` | no | `on` or `off`. `off` stops the drawn heptagram figure being painted: on the CYD's UMBRAVOX-style dial that is the lattice, range rings, rune ring, circles and figure, so the dial is left empty (the CYD plays no `radar.vid`; use `emblem` to show a picture there); on the Waveshare wide radar only the heptagram is dropped, so it no longer sits over a `radar.vid` clip. The alert takeover is not affected. Firmware from before this key ignores it. | `on` |
| `contacts` | no | `on` or `off`. `off` stops the coloured contact dots being drawn on the radar. Contacts are still collected and logged; the sweep and needle still draw. Older firmware ignores it. | `on` |
| `sweeps` | yes | a non-empty list from `radar pulse star candle galaxy orbit starfall pulsar comet aurora`. | none (a pack with no `sweeps` is skipped) |
| `sweep` | no | the starting sweep; must be one of `sweeps`. | the lowest-numbered sweep offered, in the order listed above |
| `alert` | no | `ward`, `supernova`, `vortex`, `comet` or `lockon`. Any alert works with either centrepiece. | `ward` |
| `colors` | yes | an object with all 20 roles below, each `"#RRGGBB"`. A missing or malformed colour skips the pack. | none |

The 20 colour roles: `void panel grid edge edgeLit dim text bright witch witchHi
bone sweep sweepHi live liveHi magenta ice green amber red`.

Rules the device enforces, and the reason a pack is skipped if broken:

- Unknown keys are ignored, so a later format can add some.
- A key that appears twice skips the pack.
- `name`, `header` and `author` cannot contain a double quote (`"`) or a backslash
  (`\`). The device does not read string escapes, to keep the parser simple and
  bounded. The designer removes these characters for you.
- The four names `UMBRAVOX`, `BAPH0MAU5`, `CVLT OF BAPH0MAU5` and `COSMIC MUFFIN`
  are reserved (every name a built-in displays). `COSMIC MVFFIN`, the spelling in
  the scan header only, is not reserved. A name must also be unique among packs.
- A pack whose `centrepiece` is `emblem` but whose `emblem.bin` is missing or
  invalid is skipped.
- Status and alert-edge colours (`amber`, `red`, and the confidence edge colours)
  that are too close to the background are replaced with safe defaults; the pack
  still loads. See limitation 8.

## emblem.bin

A little-endian binary picture in indexed colour:

| bytes | content |
|---|---|
| 4 | `U` `V` `E` and the number 1 |
| 1 each | width, height, number of colours `ncol`, and one reserved byte (0) |
| `ncol` x 2 | the palette, RGB565, little-endian |
| width x height | one palette index per pixel, row by row |

Width and height are at most 96, `ncol` is 2 to 16, and the file is at most 9.5 KB
(9,728 bytes). The file must be exactly the size the header declares (trailing data is
rejected), and every pixel index must be lower than `ncol`. The designer makes this file from any PNG: it crops, shrinks and
reduces the picture to 16 colours.

## alert.snd

Optional. Replaces the built-in alert beeps on the Waveshare (the only board with a speaker; the CYD ignores it). One
sound per alert level, any of them may be missing, and a level without one keeps the built-in beep:

```
bytes 0-3   'U' 'V' 'S' 'N'
byte  4     version, 1
byte  5     reserved, 0
bytes 6-7   sample rate, 16000 (u16 little-endian)
bytes 8-19  sample counts for LOW, MED, HIGH (three u32 little-endian, each 0 to 96000; not all zero)
then        the 16-bit little-endian mono samples of LOW, MED, HIGH, back to back
```

The length must match the counts exactly. A file that fails any check is not used: the pack still loads, without its
sounds, and `/diag.log` says why when its theme is selected ("alert sounds not used: ..."); `sim/umbravox --check-pack`
reports the same problem up front. The sounds are read from the card when the pack's theme is selected, not at boot, so
any number of packs may carry them; the file's size counts in the boot-loop guard's total. The ALERT SOUND setting (Settings,
recording tab: OFF, QUIET, NORMAL, LOUD) scales a pack's sounds as it scales the built-in beeps; NORMAL plays the
samples as stored, so the designer normalizes each sound to 70% of full scale.

The designer makes the file: on the Waveshare board a card under the preview takes any audio file per level
(wav, mp3, ogg, m4a: whatever the browser decodes), resamples it to 16 kHz mono, trims silence at both ends, cuts it at
6 seconds and plays it back for you. A recording longer than that gets a **Start at** slider (tenths of a second) to
pick which part is used. It travels with the theme like the video (card reader, USB, zip). Over USB the
file is under 600 KB, so under a minute. `tools/sdput.py PORT put <folder>` sends an `alert.snd` too.

## radar.vid

An optional looping clip that the Waveshare 3.5" plays inside its radar, under the contacts and the sweep. The CYD
ignores it. It is a little-endian binary like `emblem.bin`:

| bytes | content |
|---|---|
| 4 | `U` `V` `R` `V` |
| 1 each | the number 1 or 2 (version), width, height (equal: any square from 24 to 102; firmware before theme layouts accepts only 102), fps (1 to 30) |
| 2 | frame count, little-endian (1 to 150) |
| 2 | reserved, both 0 |
| version 2 only: 32 | a palette: 16 colours as RGB565, little-endian |
| frames x (width x height / 2) | 4-bit pixels, two per byte (high nibble first), row by row. Version 1: grey levels, 0 black to 15 white, painted in the theme's colours. Version 2: an index into the palette, so the clip keeps its own colours |

The file must be exactly the length its header declares (at most 780,312 bytes and 150 frames, so a 102x102 version 2 clip holds at most 149 frames). The board draws each frame 2x
inside the radar disc, so a clip as wide as the radar's radius fills it: 102 pixels for the built-in radar, smaller
for a radar a layout has made smaller (a larger clip is masked to the disc). The designer makes this file from any video the browser can play
(`tools/video2radar.py` does the same on a command line): it crops a square, shrinks it, keeps 12 frames a second
and drops the audio. With **Keep colours** on (the default) it picks the best 16 colours for the whole video (median cut,
with light ordered dithering that does not shimmer between frames) and writes version 2; off, it writes 16 greys as
version 1. `tools/video2radar.py` writes version 1 only.

A `radar.vid` that fails any check skips the **whole pack**, never half of it (the reason is in `/diag.log`, like a
bad emblem). The board keeps at most **2** clips in memory (each is up to 780 KB of PSRAM); a third pack with a clip
is skipped with "no room for another clip". The clip counts in the pack's size for the boot-loop guard, so editing
it gives a pack that crashed another try. While a clip plays the radar repaints in full 12 times a second: measured
on a Waveshare the scan screen ran at about 14 frames a second instead of about 25.

## layout.json

`layout.json` moves, resizes or hides the pieces of the shared header and footer, and Scan's three count tiles, on
the engine screens (the Waveshare 3.5" at 480x320 and the CYD engine build at 320x240). The default CYD release
draws the older 160x120 screens and ignores this file, as does firmware older than the release that adds it;
`theme.json` is unchanged, so on those the theme still shows its colours, centrepiece, sweep and alert.

```json
{ "format": 1,
  "waveshare": { "stop": [288, 276, 96, 44], "nav.poi": [384, 276, 96, 44], "wordmark": "hidden" },
  "cyd": { } }
```

- `format` is required and must be `1`.
- `waveshare` and `cyd` are each optional. A board the file leaves out keeps the built-in layout.
- Each piece is `[x, y, width, height]` in that board's pixels, or `"hidden"`. A piece the file leaves out stays
  where it is built to be. The pieces are `menu`, `wordmark`, `status.gps`, `status.nodes`, `status.power`,
  `bridge`, `sd`, `nav.scan`, `nav.domain`, `nav.codex`, `nav.poi`, `stop`, `count.wifi`, `count.5g`,
  `count.ble`, and Scan's radar parts `radar`, `spectrum`, `channel` and `badge`. Other names are ignored (later
  versions add pieces); a key named `script` is refused.
- `radar` is the radar's square box, which includes a 2 px margin for the contact markers: radius = (width - 5) / 2.
  Any size from 53x53 (radius 24) up to the built-in box (209x209 on the Waveshare, 115x115 on the CYD). The rune
  and star rings show only from radius 42. `spectrum` is at least 42x22; `channel` at least its built-in size
  (100x24 / 60x16); `badge` at least 24x24 (the emblem is drawn centred, radius = the smaller side / 2 - 4). The four
  may overlap each other, as the built-in layout's do, but not the header, footer or count tiles.
- Every screen other than Scan draws in the area the header and footer pieces leave free: the device finds the
  largest free rectangle and centres the screen in it, at its usual size. Scan's radar and any count tile you do
  not place move with it.

### Styles, the style block and decor

A piece may also be an object that gives its rect and a style:

```json
"menu": { "rect": [0, 0, 44, 44], "font": "f0", "scale": 2, "ink": "bright", "fill": "#102030",
          "edge": "edge", "frame": "a2", "slice": [4, 4, 4, 4], "bg": "a3" }
```

- `rect` is required; every other key is optional. Keys this firmware does not know are ignored.
- `font` names a pack font file (`"f0"` to `"f7"` for `f0.fnt` to `f7.fnt`); `scale` is 1 to 4.
- `ink` (the piece's text), `fill` (its surface) and `edge` (its outline) are a colour role from `theme.json`'s
  `colors` (`"bright"`, `"panel"`, ...) or `"#rrggbb"`. Status colours (amber, red, the band colours) are never
  replaced.
- `frame` is a picture (`"a0"` to `"a15"` for `a0.art` to `a15.art`) drawn as a nine-slice frame over the piece:
  `slice` gives its left, top, right and bottom borders in pixels, which are kept; the middle is repeated.
  `bg` is a picture drawn behind the piece at its exact size.

Each board may also have a `style` block for every other screen, and up to eight pictures with no function:

```json
"style": { "title": "f0", "body": "f1", "numbers": "f2", "bg": "a0", "panel": "a1", "panelSlice": [3, 3, 3, 3],
           "highlight": { "fill": "witch", "edge": "witchHi" }, "icons": ["a4", null, "a5"] },
"decor.0": { "rect": [10, 60, 40, 30], "art": "a7" }
```

- `title`, `body` and `numbers` are the fonts for those kinds of text; `bg` the screen background (the board's
  full size); `panel` and `panelSlice` the nine-slice frame for panels; `highlight` the colours of a selected row or
  tab; `icons` up to six pictures, one per Menu entry in order (`null` for none), shown above the label when the
  tile has room.
- `decor.0` to `decor.7` are drawn in that order, under every other piece, at exactly their rect's size.
  Pictures cost drawing time, and the Waveshare redraws the radar and an animated decor every frame. Each picture
  costs its area wherever that part of the screen is redrawn; the one saving is that where a redraw falls entirely
  inside an opaque decor, nothing under that decor is drawn there. A transparent picture never saves anything under
  it. A pack at every art limit at once (`tools/stress-pack.mjs`) runs the Waveshare's Scan screen at about 11
  frames a second.
- Fonts and pictures are read from the card when the theme is selected. Until they have loaded, and if one cannot
  be read, the theme draws with the built-in font and no picture. A font or picture the layout names that is missing
  from the folder, or over its limit (a font 12,800 bytes, a picture 196,608 bytes, one board's pictures together
  393,216 bytes), skips the layout. If the device resets while reading them, the pack is skipped from then on until
  one of `theme.json`, `emblem.bin`, `radar.vid`, `alert.snd` or `layout.json` changes size (fonts and pictures are
  not measured at boot).
- Over USB (`tools/sdput.py`, the designer's USB install) `layout.json`, `f0.fnt`-`f7.fnt` and `a0.art`-`a15.art`
  are sent too.

### Alerts: cards and effects

Each board may place and style the alert card for each level, and give pieces an effect that plays during an alert:

```json
"alert.card.low":  { "rect": [90, 80, 300, 160], "font": "f2", "scale": 2, "clip": "a6" },
"alert.card.high": { "rect": [0, 0, 480, 320] },
"count.ble": { "rect": [320, 44, 160, 60], "effect": "pulse", "speed": "fast" }
```

- `alert.card.low`, `alert.card.med` and `alert.card.high` are overlays, not pieces: they are drawn on top of
  everything and may cover other pieces. `rect` is required; without a card key the built-in card is used (LOW and
  MED over the radar area, HIGH the whole screen). A HIGH alert is dismissed by a tap anywhere. A LOW or MED card
  is dismissed by a tap on the card, wherever it is; the rest of Scan stays visible and live, and a tap outside the
  card reaches the piece there. So a LOW or MED card placed over STOP, MENU or the nav makes a tap there dismiss the
  alert first (the designer warns about this). The CYD engine build shows the alert card too (the release CYD
  build ignores `layout.json`).
- `font` and `scale` set the card's words. A font shorter than 7 px (its own height; `scale` does not count) is
  not used: the card then draws with the built-in font.
- `clip` is a picture (`"a0"` to `"a15"`, still or animated) played in place of the alert rite. It is never scaled;
  it shares the board's picture budget, and it plays only if it fits inside the card and its area is at most
  25,600 px on the Waveshare (10,000 px on the CYD); otherwise the rite plays.
- `effect` is `none`, `flash`, `pulse` or `shake`; `speed` is `slow`, `medium` (the default) or `fast` (one cycle
  every 1000, 500 or 250 ms). Flash and pulse tint the piece toward the alert's colour, shake moves it sideways by
  up to 2 px. Effects play only while an alert is on screen, never draw over the alert card, and are capped at 4
  pieces on the Waveshare and 2 on the CYD: each one repaints its piece every frame of the alert.

In the designer, the Layout step's tabs above the screen switch between Scan and Alert LOW, MED and HIGH. An alert
tab shows that alert over the dimmed scan; drag its card, and set the card's font, size and picture beside it ("Use
the built-in card" removes it). A picture is shrunk to fit the card and the moving-picture area. Each piece's
"During an alert" row sets its effect and speed; at the board's limit the effect list is disabled and says why.
Play runs the alert from its start, looping every 5 s like the console `TEST`. The checks warn when a LOW or MED card
covers STOP, MENU or the nav, when a card font is under 7 px, and when an opened pack's card picture does not fit.

What makes the device refuse a layout (it then uses the built-in layout and keeps the rest of the theme; the reason
is in `/diag.log` and counts toward the skipped number on the ASPECT screen):

- a piece off the screen;
- a piece you can tap (everything except `wordmark` and `sd`) smaller than 44 px either way;
- hiding `menu`, `stop`, `bridge`, a `status.*` cell or a `nav.*` button (only `wordmark`, `sd` and the
  `count.*` tiles can be hidden);
- two pieces overlapping, or a count tile over the radar;
- too little room left for the screens (the free area must be at least 464x216 on the Waveshare and 304x136 on the CYD);
- a file over 16,384 bytes, or one that is not valid layout JSON (duplicate keys, a value that is not four whole
  numbers, `"hidden"` or a piece object with a `rect`, nesting deeper than four levels);
- a font or picture reference outside `f0`-`f7` / `a0`-`a15`, a colour that is not a role name or `#rrggbb`, a
  `decor` key other than `decor.0` to `decor.7`, a decor piece off the screen;
- a `fill` on `menu`, `stop`, `bridge` or a `nav.*` button that its words cannot be read on (under 3:1): its `ink`, or
  else the theme's `text` and `bright`; for `stop` also `red` and `amber`, the colours of its label while recording.
- an alert card off the screen or smaller than 240x120 on the Waveshare (160x96 on the CYD), an `effect` or `speed`
  that is not one of its words, or effects on more than 4 pieces on the Waveshare (2 on the CYD).

### Speed: how fast the board can draw it

The board shares one CPU between drawing and listening to the radios, so a layout that takes too long to draw costs
captures. The Layout step's speed meter shows, for each board, the scan screen and the slowest alert level, as the
time of a **slow frame** (the 95th percentile: 1 frame in 20 is slower; the very slowest is mostly noise):

- it replays the layout with the device's own drawing code (the same code the board runs, built into the designer),
  counting the pixels each frame draws and the pixels it sends to the screen;
- it turns those counts into milliseconds with formulas measured on both real boards (`js/meter-fit.json`), one for
  the scan screen and one for alerts;
- limits: a scan frame at most the built-in theme's own (as the meter reads it) plus 15%, per board; an alert frame
  at most 400 ms on both boards. `js/meter-fit.json` records the ones in force.

From 80% of a limit the row turns amber, which is a warning only (the built-in theme itself sits at about 87%, so
amber on the scan row is normal). Past 100% on a scan row the designer closes Download, Install and Share for that
layout and says which board is too slow. The alert row is a rough estimate: past its limit it turns amber and warns,
but never blocks. The usual causes of a slow layout are a large animated picture, effects on large pieces, or a
full-screen card with an effect. A designer with no measurements
yet shows "not calibrated" and blocks nothing.

## Limits

These numbers are the same ones the device and the designer use
(`web/designer/js/limits.json`, and `ui/themepack.h`).

| what | limit |
|---|---|
| packs the device loads | 12 (the first 12 by folder name) |
| folders examined under `/themes` | 32 (the first 32 by name); further folders are not read, but are logged to `/diag.log` and counted as skipped. Folders with invalid names are also logged and counted. |
| folder name | 1-31 letters, digits, `-` or `_`; anything else is skipped |
| `name` | 20 characters, unique, not one of the four reserved names. The settings screen shows only the first 12, so two long names that start the same look identical there (the designer warns). |
| `header` | 18 characters |
| `author` | 24 characters |
| `theme.json` | at most 4 KB (4,096 bytes) |
| `layout.json` | at most 16 KB (16,384 bytes); every tappable piece at least 44x44 px |
| `emblem.bin` | at most 96x96 pixels, 16 colours, 9.5 KB (9,728 bytes) |
| `radar.vid` | square, 24x24 to 102x102 (older firmware: exactly 102x102), 1 to 30 fps, 1 to 150 frames, at most 780,312 bytes; read from the card when the pack's theme is selected (one clip in memory at a time) |
| `alert.snd` | 16 kHz mono 16-bit, at most 96,000 samples (6 s) per level, at most 576,020 bytes; read from the card when the pack's theme is selected (one set in memory at a time) |
| whole pack | 64 KB (65,536 bytes) is the designer's figure; the per-file limits above (4,096 + 9,728 bytes) are what actually binds, and the device does not check 64 KB |
| alert edge contrast | 3:1 against the background, or a safe colour is drawn instead |

## Make a pack in the designer

1. Open <https://em3ritus.github.io/umbravox/designer/>. It needs a recent browser.
   It uses features available from Chrome or Edge 123, Safari 17.2 and Firefox 138
   (the page loads its code as modules that import JSON). Those minimums come from
   the feature tables, not from testing each browser. The page shows a message if
   yours is too old.
2. Start from a template (UMBRAVOX, BAPH0MAU5 or COSMIC MUFFIN), a blank theme,
   or open an existing `theme.json`. Under the templates, pick a **starting layout**
   (Standard, Radar left, Big radar, Numbers or Bottom-up); change it later in step 4. The BAPH0MAU5 template is a heptagram in the
   seal's colours, because the seal bitmap is not available to packs.
3. Edit the identity, the palette (there is a "make a palette from one colour"
   helper), the centrepiece (drop a PNG, or choose the heptagram; two checkboxes hide the
   heptagram and the contact dots so your own image or video shows clean) and the motion
   (sweeps, starting sweep, alert). The preview shows Scan, Mesh, Alert and Menu with
   colours after RGB565 clamping. The checks explain anything that is wrong.
   Pick the board to preview above the screen: **CYD 2.8"** or **Waveshare 3.5"**
   (see *Designing for the Waveshare 3.5"* below).
4. **Layout** (optional): the board's engine screen drawn by the device code, with every piece outlined. Click a
   piece to select it, drag it to move it, pull a corner to resize it, arrow keys nudge it 1 px (Shift: 8 px); the
   side panel shows its exact numbers and has **Hide** and **Back to default**; hidden pieces wait in a tray. Two tabs:
   **Waveshare 3.5"** and **CYD 2.8"** (the CYD's engine build at 320x240). The first board you change is drafted
   onto the other when you open its tab: check it there. The checks run the device's own layout rules, so the
   designer never saves a layout the board would refuse. If the theme has a radar video and you made the Waveshare
   radar smaller, the pack's `radar.vid` is made at the new size when you save (your draft keeps the original).
   A theme with a layout cannot be shared yet.
5. Choose **Download pack** (a `.zip` of the folder) or **Install to SD card**. The
   Install page has two routes: write to the card in a card reader, or send the pack
   over the board's USB cable with the card left in the board.

### Designing for the Waveshare 3.5"

A pack is the same two files for every board, and the Waveshare reads it with the
same loader as the CYD, so one pack can serve both. What changes is how the board draws it,
and the designer previews that with the board's own drawing code:

- **Board switch.** The preview card has a **CYD 2.8"** / **Waveshare 3.5"** switch. The Waveshare
  preview is the full 480x320 engine screen (not a scaled-up CYD picture). It adds an **Aspect**
  tab, and on the Alert tab a **Low / Medium / High** switch: Low and Medium take over the radar
  area, High takes the whole screen. The choice is remembered in the browser.
- **Same code as the board.** The preview is `ui/preview_ws.h` compiled to WebAssembly, the same
  projection and compositor the firmware runs. `web/designer/test/wasm-parity.test.mjs` holds it to
  the host render byte for byte (built-ins, both sample packs, every sweep, every alert style and
  all three alert levels).
- **Radar video.** On the Waveshare, **Radar video** takes a video file and plays it in the radar as the board
  would draw it: cropped to a square (the Crop slider picks which part of a tall video), 102x102, 12 frames a
  second, 16 grey levels, looping, at most 12 seconds, audio dropped. A GIF works too (uneven frame delays are
  resampled to 12 fps); it needs a browser with `ImageDecoder` (Chrome, Edge, Safari), and `tools/video2radar.py`
  reads a GIF through ffmpeg the same way. The clip is part of the theme: it is
  saved as `radar.vid` beside `theme.json`, checked, downloaded in the zip and installed with the pack, and
  an opened pack brings its `radar.vid` back. Remove the video and the next install has none. Over USB a clip takes
  minutes to send (about 650 KB; the page allows the time); a card reader is instant. The USB route cannot delete,
  so a video taken out of a pack you installed before stays on the card until you delete it with
  `tools/sdput.py PORT del /themes/<folder>/radar.vid` or use the card route, which removes it.
  The Scan tab never shows the demo alert (that is the Alert tab's job).
- **Dial size does nothing there.** *Compact* and *Large* are CYD layout presets; the Waveshare
  lays its radar out itself. The page says so when the Waveshare is selected.
- **Gaze and Summon** stay unavailable to packs (they belong to the Baphomet seal), as on the CYD.
- **Install over USB** filters the browser's port list to the Waveshare's native USB (an Espressif
  "USB JTAG/serial debug unit", USB ID 303a:1001). The Waveshare's console is the chip's own USB
  port, so the port disappears and comes back whenever the board restarts. That is normal.
  **Not yet run against a real Waveshare with a card in it:** the protocol was checked only as far
  as the board answering `SDLS` (it replied `sdlog: no card`), because no card was inserted.

### Install in the browser

**Install to SD card** uses the File System Access API, which exists only in desktop
Chromium browsers (Chrome, Edge, Opera). You pick the card; the page writes
`/themes/<folder>/` and reads it back to verify. If the page finds a folder with that
name already on the card it asks whether to replace it or to use a different folder
name. It does not offer the "different folder" choice when the existing folder holds
the same theme name, because the device would then skip one of two same-named packs.
Folder names come from the theme name lowercased, with each run of other characters
turned into `-`, so `A.B`, `A-B` and `A B` all become `a-b`; the install page checks
the card for exactly this clash. Then eject the card properly and reboot the device.

### Install over USB in the browser (card stays in the CYD)

The Install page's second route, **Install over USB**, sends the pack down the CYD's
USB cable using Web Serial, speaking the same protocol as `tools/sdput.py` (below).
It is shown only where the browser has Web Serial: desktop Chrome or Edge (Web
Serial arrived in version 89; the designer uses features available from Chrome 123 anyway). Firefox and
Safari do not have it; the page says so and points to the card route or the zip.

1. Close any serial monitor (Arduino IDE, PlatformIO, `screen`): only one program can
   use the port at a time.
2. Plug the CYD in and choose **Connect to the CYD...**. The browser lists CH340
   ports (USB ID 1a86:7523); tick *show every serial port* if yours is not listed.
   The page opens it at 115200 8N1 and lowers DTR and RTS at once. **Connecting may
   reset the board**: it restarts in a few seconds, and the page listens for the boot
   banner and waits for the firmware's main loop before it sends anything.
3. Choose **Send over USB**. The page first lists `/themes` (`SDLS /themes`). If a
   folder with that name is already there it asks, with the same choices as the card
   route: overwrite, install under another folder name (`<name>-2`...), or cancel.
   Over USB only folder names are read, so the page cannot tell which theme an
   existing folder holds; it never overwrites without asking.
4. It sends `radar.vid` (if any), `emblem.bin` (if any) and then `theme.json`, each chunk acknowledged, with
   a progress bar. Success is claimed only when the device has answered `DONE` with
   the size and CRC-32 the page computed, for every file. The failure table under
   *Installing over USB without removing the card* says what the card holds if it
   stops part-way; the page shows the same.
5. The pack loads at the **next boot**. The page does not restart the board: press its
   reset (RST) button, or unplug and replug the cable.

By design the web page sends only `SDLS /themes`, `SDPUT`, `D`, `END` and `ABORT`.
It never sends `SDDEL`, `SDRMDIR` or `REBOOT`: those act without any confirmation on
the device, and a web page is not where they belong (see the note at the end of the
next section). To delete a pack or reboot from a computer, use `tools/sdput.py`.
Device output is shown as plain text only, lines are bounded at 512 characters, and
the page buffers at most 16 KB of console output.

The protocol client is tested against a fake device (lost replies, corrupted chunks,
banner noise, a silent or flooding port) and against the real `device/sdcmd.h` state
machine through `sim/sdcmd_console`. **It has not yet been run in a real browser
against a real CYD**; treat it as experimental until that is recorded.

### Install by hand (any browser)

1. Download the pack zip and unzip it. You get a folder such as `midnight-ember/`
   containing `theme.json` (and `emblem.bin` for an emblem pack).
2. On the SD card, create a folder named `themes` at the root if there is not one.
3. Copy the pack folder into `/themes/`, so you have `/themes/midnight-ember/theme.json`.
4. The card must be FAT32, the same as for logging.
5. Eject the card properly from your computer (not just unplug it), put it in the
   CYD and reboot. Open ASPECT and step the THEME row past the built-ins.

### Installing over USB without removing the card

The CYD can also take a pack over its **USB cable**, through the same serial
console as `DIAG` (CH340, 115200 baud). This is a **wired-console feature only**:
nothing is sent over Wi-Fi, BLE or ESP-NOW, and it adds no network access. It is
**not yet verified on hardware**; treat it as experimental until it has been
tested on a real unit.

```sh
# from the repository, with the CYD on /dev/ttyUSB0
~/.platformio/penv/bin/python tools/sdput.py /dev/ttyUSB0 put midnight-ember/
~/.platformio/penv/bin/python tools/sdput.py /dev/ttyUSB0 reboot      # packs load at boot
~/.platformio/penv/bin/python tools/sdput.py /dev/ttyUSB0 ls /themes/midnight-ember
~/.platformio/penv/bin/python tools/sdput.py /dev/ttyUSB0 del /themes/midnight-ember/emblem.bin
~/.platformio/penv/bin/python tools/sdput.py /dev/ttyUSB0 rmdir midnight-ember   # only when empty
```

`put` uploads `radar.vid` and `emblem.bin` (if the folder has them) and then `theme.json` into
`/themes/<name>/`, where `<name>` is the folder's name made safe the way the
designer makes it (lower case, runs of other characters become `-`). The tool reads
only regular files that really sit in that folder: a `theme.json`, `emblem.bin` or `radar.vid`
that is a symlink (even to another file in the pack), a hard link (a file with
more than one link, which could be a second name for a file elsewhere), a FIFO, a
device or a directory is refused, so a booby-trapped pack zip cannot make it upload some other
file from your computer. The folder you name may itself be a symlink; it is
followed, and the pack is named after the folder as you typed it.

Each file goes to `<file>.tmp` first. The device checks the size and the CRC-32 by
reading the card back, and only then swaps it in: the old file is renamed to
`<file>.old`, the temp to `<file>`, and the `.old` is deleted. A pack still needs a
**reboot** to load: `sdput.py PORT reboot` (it writes the logging session's pending
rows first; logging resumes into a new file).

What is left on the card when something goes wrong:

- **The host stops, sends bad data, or Ctrl-C is pressed** (the tool sends `ABORT`),
  or no accepted chunk arrives for **10 s**, or the upload is still running **120 s**
  after it began (a `radar.vid` over 9.5 KB gets 300 ms more per KiB, about 350 s for the biggest): the temp is removed and the old file is untouched. If the upload
  had created the pack folder and it is still empty, the folder is removed too.
- **The power is cut during the upload**: the old file is untouched, and a
  `<file>.tmp` is left behind. The loader ignores it; the next upload of that file
  replaces it, or delete it with `del`.
- **The power is cut during the swap at the end** (a window of two renames), or
  both the swap and its undo fail: the old file may be left as `<file>.old` with no
  `<file>`, so that pack is skipped at boot (or shown without its emblem). The next
  `put` of that pack renames `.old` back before it starts, so the old file returns
  even if that upload fails too; you can also `del` the leftover.
- **Replacing a pack** sends `radar.vid` and `emblem.bin` before `theme.json`. If the last file
  fails, the card has the new files with the old `theme.json`. That is cosmetic
  (the old theme with the new picture); run `put` again.

What the serial commands may touch, and nothing else:

- write: `/themes/<folder>/theme.json` (at most 4,096 bytes) and
  `/themes/<folder>/emblem.bin` (at most 9,728 bytes) and `/themes/<folder>/radar.vid` (at most 780,312 bytes); `<folder>` follows the
  folder-name rule above and cannot be a DOS device name (`CON`, `NUL`, `COM1`...).
- delete: those three files (and a `.tmp` or `.old` an interrupted upload left), an
  empty pack folder, and old session logs `/WD-*.csv`, **never the one being
  logged now**. Never `diag.log`, `perf.csv` or anything else.

Each change is written to `/diag.log` (`INFO sd: put /themes/x/theme.json 758 bytes`,
`INFO sd: del /WD-0003.csv`). The tool opens the port with DTR and RTS held low so
it does not reset the board; if your board still resets when the port opens, run
`stty -F /dev/ttyUSB0 -hupcl` once first. The protocol is described at the top of
`device/sdcmd.h`.

Known limitation: `SDDEL`, `SDRMDIR` and `REBOOT` act without a confirmation on the
device. Anything that can write to the port can delete pack files and old logs or
restart the unit. The designer's Web Serial route therefore leaves all three out
(it sends only `SDLS /themes`, `SDPUT`, `D`, `END` and `ABORT`, and refuses anything
else before writing it); only `tools/sdput.py`, run by you on your own computer,
can delete or reboot.

## Share a theme

A theme can be offered to everyone through the themes gallery, a separate public repository
(`Em3ritus/umbravox-community-themes`) with its own page. **This route is built but not yet live:** the
repository and the gallery do not exist until the first release that carries this section's
designer, and none of it has been run end to end.

To share:

1. Finish the theme in the designer so the checks show no errors, then press **Share...**.
2. The designer saves the pack zip (the same one as Download pack) and opens a submission form on
   GitHub with the theme's name and author filled in. You need a GitHub account.
3. Drag the saved zip into the form, tick the two boxes and submit. A web page cannot attach the
   file for you.

What happens next:

- The zip is unpacked under strict limits (one folder, only `theme.json`, `emblem.bin`, `radar.vid`
  and `alert.snd`, 2 MB at most), checked with the device's own pack loader, and drawn to two
  pictures, one for the CYD and one for the Waveshare. If it fails, a comment on your submission
  says why.
- A theme appears in the gallery only after the owner approves it. A theme name or folder the
  gallery already has is refused, so a changed copy of someone else's theme needs a new name; it
  keeps a "based on" credit.
- Everything in a shared theme is public. Share only pictures, video and sound you made or have
  the right to share. Themes are checked for format, not for content, and can be removed at any
  time.

To use a shared theme, press **Open in designer** in the gallery. The designer opens with that
theme loaded (its address ends in `?theme=<folder>`), and from there it is changed, downloaded or
installed like any other. Only a theme the gallery lists is fetched, and only the four pack files,
each within its size limit.

## How the device treats a pack

- Packs are read once, at **boot**, after the card mounts. After adding, changing or
  removing a pack, reboot.
- Folders are read in sorted name order. The first 32 folders are examined; of those,
  the first 12 valid packs load. A pack that fails any rule is skipped whole, never
  partly applied.
- Packs appear in the THEME picker on ASPECT after the built-ins. The choice is saved
  by name, so adding or removing other packs does not change what is selected.
- If the non-volatile storage (NVS) is unavailable, no packs load, because the boot-loop guard cannot be kept; this is logged.
- If the saved pack is missing or invalid at boot, the device falls back to UMBRAVOX.
- Every admitted emblem is held in memory for the session (at most 12 packs, 114 KB of
  emblem data). A pack's radar clip and alert sounds are NOT: they are read from the card when its theme is selected
  (see "Clip and sounds load on select" below).
- **Clip and sounds load on select.** At boot the device reads only each pack's `theme.json` and emblem. When a pack's
  theme has stayed selected for 0.4 s (so stepping through the theme list loads nothing), its `radar.vid` and `alert.snd`
  are read into memory and the previous pack's are dropped; the saved theme's are read at boot. Only one pack's clip and
  sounds are in memory at a time, so the number of packs with a clip is limited only by the 12-pack cap. Reading takes
  about 2 seconds for a large pack (roughly 460 KB/s) and the screen waits for it; until then the theme shows without video
  and with the built-in beeps. If an alert sound is playing, the switch waits for it to finish. A clip or sound that cannot
  be used is logged and left out; the pack still works without it.
- Skipped packs are logged to `/diag.log` with the reason. ASPECT shows the count: the
  words `N SKIPPED` to the left of the dial preview and `DIAG LOG` to the right of it.
- **Boot-loop guard.** Before loading a pack the device records its folder and size in
  non-volatile storage and clears the record afterwards. If the board resets while the
  record is set, that pack is added to a list of bad packs and skipped on every later
  boot, and the failure is logged. Editing the pack so its size changes gives it another
  try. Removing the folder does too, but only after the device has completed one boot
  with the folder absent: a same-size pack put straight back stays on the list. The guard
  covers loading only: at boot, and again when a pack's clip and sounds are read on selecting its theme. A pack that
  crashes the loader is skipped on the next boot; a pack
  that loads fine but faults later, when drawn, is not covered (see Troubleshooting).
- The Baphomet seal's own sweeps and rite are not in packs; alerts always show.

## The 19 limitations

These are the 19 numbered items below. `web/designer/js/limits.json` has 20 entries,
because the characters rule (no `"` or `\` in name, header or author) is its own entry
there but is folded into item 4 here, and the USB route entries appear in both.

1. **Reboot.** A new or changed pack needs a reboot to appear. The device reads the SD
   card at boot only.
2. **Twelve packs.** At most 12 packs load, chosen as the first 12 by folder name, and only
   the first 32 folders (by name) are examined at all. Folder names are letters,
   digits, `-` and `_` only. A theme name must be unique and cannot be UMBRAVOX,
   BAPH0MAU5, CVLT OF BAPH0MAU5 or COSMIC MUFFIN; otherwise that pack is skipped.
3. **Centrepiece picture.** At most 96x96 pixels and 16 colours. Photos are reduced and
   will look posterised; expect pixel art, not photographs.
4. **Header text.** Uppercase only (lowercase is shown as capitals), limited to letters,
   digits, space and common punctuation, 18 characters at most. The name, header and
   author cannot contain a double quote or a backslash, because the device does not
   read escapes and would skip the pack.
5. **RGB565.** Colours are stored as RGB565, so fine differences between near colours
   disappear. The designer previews the colours the panel will really show.
6. **Built-in pieces.** A layout moves, resizes, hides and restyles the pieces the device draws, and adds pictures
   with no function; it cannot add new kinds of piece. Fonts are the built-in pixel fonts or a TTF/OTF you upload,
   turned into pixels at the sizes used (at most 8 fonts and 16 pictures per theme). Only one picture per board moves.
7. **Built-in sweeps and alerts.** Sweeps and alerts come from the built-in set. Gaze and
   the Summon rite are not available to packs; they belong to the Baphomet seal.
8. **Alerts stay visible.** A theme cannot hide or disable alerts. Status and alert-edge
   colours that are too close to the background are replaced with safe defaults.
9. **Saved by name.** The selected theme is saved by name. Renaming a pack makes the
   device fall back to UMBRAVOX until you select it again.
10. **Browser support.** Installing straight from the page needs desktop Chrome, Edge or
    Opera. Other browsers use the zip download; copy the folder to the card by hand.
    The designer itself uses features available from Chrome or Edge 123, Safari 17.2 and
    Firefox 138 (taken from feature tables, not measured on each browser).
11. **FAT32.** The SD card must be FAT32, the same as for logging.
12. **Skipped packs.** A pack that fails to load is skipped, never partly applied. The
    ASPECT screen shows the skipped count as `N SKIPPED` and `DIAG LOG`, split around the
    dial preview, and the reason is in `/diag.log`.
13. **Format, not taste.** Packs are checked for format, not for taste or origin: nothing
    here signs, vets or moderates a pack. Only install packs from people you trust.
14. **USB route: browsers.** Installing over USB needs desktop Chrome or Edge (Web
    Serial, from version 89; this page uses features available from 123 anyway). Firefox and Safari
    cannot; use the SD card route or the zip there.
15. **USB route: reset on connect.** Connecting over USB may reset the board. It
    restarts in a few seconds, and the page waits for it to finish booting before it
    sends anything.
16. **USB route: next boot.** A pack sent over USB is on the card at once but loads at
    the next boot. The page never restarts the board; press its reset button or unplug
    and replug the cable.
17. **USB route: one program.** Only one program can use the USB serial port at a time.
    Close any serial monitor (Arduino IDE, PlatformIO, screen) before connecting, and
    disconnect here before opening one.
18. **Sharing.** Sharing needs a GitHub account. Shared themes are public, and you must have
    the right to share any picture, video or sound in them. A theme with a layout cannot be shared yet.
19. **Layouts change the engine screens only.** A layout applies to the Waveshare 3.5" and the CYD's engine build.
    The default CYD release draws its older screens and ignores it, as does firmware older than the release that adds
    layouts; the theme's colours still show there.

What is tested and what is not. The pure rules are host-tested: the theme.json and
emblem.bin parsers, pack admission, the boot-loop guard's decisions, the ASPECT drawing
and the contrast rule. The SD card scan (`device/themeload.cpp`) and its NVS plumbing are
build-checked for every board, and have been run on one real card: the Waveshare 3.5".
There, packs loaded at boot, the boot-loop guard skipped a pack that was mid-load at a
reset and kept skipping it across reboots, and removing the folders for one full boot
cleared it. The same board took a pack over its native USB port (the designer's own
installer, driven from Node: size and CRC read back, then `loaded ... as theme 15` after a
reboot). Not run: the install from a real browser over Web Serial, the card-reader route
on the Waveshare, and any of it on the CYD. Treat those as unverified until recorded.

One trap found in that test: opening the serial port resets the board. If that happens
while it is loading packs, the guard counts it as a crash and puts the pack on the bad
list. After a boot with packs on the card, wait about 15 seconds before opening the port.

## Check a pack on your computer

The simulator builds with `make -C sim`. It runs the device's real parser:

```
sim/umbravox --check-pack path/to/midnight-ember
```

It prints `OK <name>` or the reason the pack would be skipped. Pass the folder by its
real name or path. `--check-pack .` and `--check-pack ..` fail, because the folder-name
rule is applied to the last path component and `.` is not a valid folder name.

## Troubleshooting

| symptom | likely cause |
|---|---|
| The pack is not in the THEME picker | You did not reboot; the folder is not directly under `/themes`; it is beyond the first 12 packs or first 32 folders by name; or it was skipped (check ASPECT for `N SKIPPED` and `/diag.log`). |
| `N SKIPPED` shows on ASPECT | Read `/diag.log` for the reason. Run `sim/umbravox --check-pack <folder>` for the same message. |
| Skipped: duplicate or reserved name | The `name` is already used by an earlier pack or is one of the four reserved names. |
| Skipped: name, header or author | A double quote, a backslash, or a character outside the allowed set. |
| Skipped: emblem | `centrepiece` is `emblem` but `emblem.bin` is missing, too big, over 96x96 or over 16 colours. |
| Skipped: radar.vid | `radar.vid` is over 780,312 bytes. (A clip that is malformed, not square from 24 to 102, or the wrong length does not skip the pack: it loads without video, and `/diag.log` says "radar clip not used: ..." when its theme is selected.) |
| Skipped: missing sweeps or colors | `sweeps` or one of the 20 colours is missing or malformed. |
| Colours look different from the design | RGB565 rounding, or a status or alert-edge colour was replaced for contrast. |
| The device went back to UMBRAVOX | The saved pack was renamed, removed or skipped. Select it again. |
| A pack stays skipped after it crashed the loader | Edit it so its size changes, or remove the folder, boot once with the folder absent, then reinstall and reboot. |
| The device resets repeatedly after you select a pack | Take the SD card out and boot. Choose another theme (the selection is saved by name), then remove or fix the pack. |
| Folder not found after copy | The card is not FAT32, or `themes` is not at the card root. |
| The designer page will not load | The browser is older than the minimums in limitation 10. |

## Native radar and header badge extension

The native renderer accepts `"centrepiece": "radar"` for a seal-free field:
three rings, centre cross and the native fading radar sweep. Use `"sweeps":
["radar"]` for this style; other native clean-field sweep variants are not yet
implemented. Existing heptagram/emblem packs and their defaults are unchanged.
The legacy canvas omits the centrepiece and retains its existing search animation.

Optional `"badge": "diamond"` draws an outlined diamond before the native header
wordmark. Default is `"none"`; unknown values and duplicate keys are rejected.
These options apply by pack data, not by theme name. The current browser designer
has no controls for these extensions yet; author them in JSON. Older firmware
rejects the unknown `radar` centrepiece rather than drawing the wrong artwork.
