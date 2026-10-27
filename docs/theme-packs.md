# Theme packs

A **theme pack** is a small folder on the SD card that changes how UMBRAVOX looks:
its colours, the centrepiece picture on the scan screen, which sweeps it offers and
which alert rite it uses. The device reads packs at boot. A pack is **data only**:
two files, no code. Nothing in a pack can touch the radios, the SD card or any
network, so UMBRAVOX stays passive however a pack is written.

You do not need a toolchain. The [theme designer](https://em3ritus.github.io/umbravox/designer/)
builds a pack in the browser, with a live preview drawn by the device's own `ui/`
code compiled to WebAssembly. This page is the reference for the format, the limits
and everything a pack cannot do.

## What a pack cannot do

- No scripting and no custom animation. The format is data only and stays that way
  in this version.
- No Gaze sweep and no Summon rite. They belong to the Baphomet seal.
- No custom fonts and no custom screen layout. Layout is two presets (dial sizes).
- No hiding or disabling alerts.
- It cannot use the Baphomet seal bitmap (a pack can choose a heptagram instead).

## Folder layout

```
/themes/                  at the root of the FAT32 SD card
  midnight-ember/         one folder per pack
    theme.json            required
    emblem.bin            required only when "centrepiece" is "emblem"
```

The designer writes exactly these two files. Any other file in a pack folder
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

## Limits

These numbers are the same ones the device and the designer use
(`web/designer/js/limits.json`, and `ui/themepack.h`).

| what | limit |
|---|---|
| packs the device loads | 8 (the first 8 by folder name) |
| folders examined under `/themes` | 32 (the first 32 by name); further folders are not read, but are logged to `/diag.log` and counted as skipped. Folders with invalid names are also logged and counted. |
| folder name | 1-31 letters, digits, `-` or `_`; anything else is skipped |
| `name` | 20 characters, unique, not one of the four reserved names |
| `header` | 18 characters |
| `author` | 24 characters |
| `theme.json` | at most 4 KB (4,096 bytes) |
| `emblem.bin` | at most 96x96 pixels, 16 colours, 9.5 KB (9,728 bytes) |
| whole pack | at most 64 KB (65,536 bytes) |
| alert edge contrast | 3:1 against the background, or a safe colour is drawn instead |

## Make a pack in the designer

1. Open <https://em3ritus.github.io/umbravox/designer/>. It needs a recent browser:
   Chrome or Edge 123+, Safari 17.2+ or Firefox 138+ (the page loads its code as
   modules that import JSON). The page shows a message if yours is too old.
2. Start from a template (UMBRAVOX, BAPH0MAU5 or COSMIC MUFFIN), a blank theme,
   or open an existing `theme.json`. The BAPH0MAU5 template is a heptagram in the
   seal's colours, because the seal bitmap is not available to packs.
3. Edit the identity, the palette (there is a "make a palette from one colour"
   helper), the centrepiece (drop a PNG, or choose the heptagram) and the motion
   (sweeps, starting sweep, alert). The preview shows Scan, Mesh, Alert and Menu with
   colours after RGB565 clamping. The checks explain anything that is wrong.
4. Choose **Download pack** (a `.zip` of the folder) or **Install to SD card**.

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

### Install by hand (any browser)

1. Download the pack zip and unzip it. You get a folder such as `midnight-ember/`
   containing `theme.json` (and `emblem.bin` for an emblem pack).
2. On the SD card, create a folder named `themes` at the root if there is not one.
3. Copy the pack folder into `/themes/`, so you have `/themes/midnight-ember/theme.json`.
4. The card must be FAT32, the same as for logging.
5. Eject the card properly from your computer (not just unplug it), put it in the
   CYD and reboot. Open ASPECT and step the THEME row past the built-ins.

## How the device treats a pack

- Packs are read once, at **boot**, after the card mounts. After adding, changing or
  removing a pack, reboot.
- Folders are read in sorted name order. The first 32 folders are examined; of those,
  the first 8 valid packs load. A pack that fails any rule is skipped whole, never
  partly applied.
- Packs appear in the THEME picker on ASPECT after the built-ins. The choice is saved
  by name, so adding or removing other packs does not change what is selected.
- If the non-volatile storage (NVS) is unavailable, no packs load, because the boot-loop guard cannot be kept; this is logged.
- If the saved pack is missing or invalid at boot, the device falls back to UMBRAVOX.
- Every admitted emblem is held in memory for the session (at most 8 packs, 76 KB of
  emblem data), so switching themes does not read the card.
- Skipped packs are logged to `/diag.log` with the reason. ASPECT shows the count: the
  words `N SKIPPED` to the left of the dial preview and `DIAG LOG` to the right of it.
- **Boot-loop guard.** Before loading a pack the device records its folder and size in
  non-volatile storage and clears the record afterwards. If the board resets while the
  record is set, that pack is added to a list of bad packs and skipped on every later
  boot, and the failure is logged. Editing the pack (its size changes) or removing the
  folder gives it another try. A pack therefore cannot trap the device in a reset loop.
- The Baphomet seal's own sweeps and rite are not in packs; alerts always show.

## The 13 limitations

1. **Reboot.** A new or changed pack needs a reboot to appear. The device reads the SD
   card at boot only.
2. **Eight packs.** At most 8 packs load, chosen as the first 8 by folder name, and only
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
6. **Presets only.** Layout comes from two dial sizes. There is no custom screen layout,
   font or animation yet.
7. **Built-in sweeps and alerts.** Sweeps and alerts come from the built-in set. Gaze and
   the Summon rite are not available to packs; they belong to the Baphomet seal.
8. **Alerts stay visible.** A theme cannot hide or disable alerts. Status and alert-edge
   colours that are too close to the background are replaced with safe defaults.
9. **Saved by name.** The selected theme is saved by name. Renaming a pack makes the
   device fall back to UMBRAVOX until you select it again.
10. **Browser support.** Installing straight from the page needs desktop Chrome, Edge or
    Opera. Other browsers use the zip download; copy the folder to the card by hand.
    The designer itself needs Chrome or Edge 123+, Safari 17.2+ or Firefox 138+.
11. **FAT32.** The SD card must be FAT32, the same as for logging.
12. **Skipped packs.** A pack that fails to load is skipped, never partly applied. The
    ASPECT screen shows the skipped count as `N SKIPPED` and `DIAG LOG`, split around the
    dial preview, and the reason is in `/diag.log`.
13. **Format, not taste.** Packs are checked for format, not for taste or origin: nothing
    here signs, vets or moderates a pack. Only install packs from people you trust.

What is tested and what is not. The pure rules are host-tested: the theme.json and
emblem.bin parsers, pack admission, the boot-loop guard's decisions, the ASPECT drawing
and the contrast rule. The SD card scan (`device/themeload.cpp`) and its NVS plumbing are
only build-checked: they compile for the device, but they have not been run on a real
card, and the hardware pass is still pending. Treat card-side behaviour as unverified
until that pass is recorded.

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
| The pack is not in the THEME picker | You did not reboot; the folder is not directly under `/themes`; it is beyond the first 8 packs or first 32 folders by name; or it was skipped (check ASPECT for `N SKIPPED` and `/diag.log`). |
| `N SKIPPED` shows on ASPECT | Read `/diag.log` for the reason. Run `sim/umbravox --check-pack <folder>` for the same message. |
| Skipped: duplicate or reserved name | The `name` is already used by an earlier pack or is one of the four reserved names. |
| Skipped: name, header or author | A double quote, a backslash, or a character outside the allowed set. |
| Skipped: emblem | `centrepiece` is `emblem` but `emblem.bin` is missing, too big, over 96x96 or over 16 colours. |
| Skipped: missing sweeps or colors | `sweeps` or one of the 20 colours is missing or malformed. |
| Colours look different from the design | RGB565 rounding, or a status or alert-edge colour was replaced for contrast. |
| The device went back to UMBRAVOX | The saved pack was renamed, removed or skipped. Select it again. |
| A pack stays skipped after it crashed the loader | Edit it (its size must change) or remove the folder, then reboot. |
| Folder not found after copy | The card is not FAT32, or `themes` is not at the card root. |
| The designer page will not load | The browser is older than the minimums in limitation 10. |
