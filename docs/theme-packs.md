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
| `name` | 20 characters, unique, not one of the four reserved names. The settings screen shows only the first 12, so two long names that start the same look identical there (the designer warns). |
| `header` | 18 characters |
| `author` | 24 characters |
| `theme.json` | at most 4 KB (4,096 bytes) |
| `emblem.bin` | at most 96x96 pixels, 16 colours, 9.5 KB (9,728 bytes) |
| whole pack | 64 KB (65,536 bytes) is the designer's figure; the per-file limits above (4,096 + 9,728 bytes) are what actually binds, and the device does not check 64 KB |
| alert edge contrast | 3:1 against the background, or a safe colour is drawn instead |

## Make a pack in the designer

1. Open <https://em3ritus.github.io/umbravox/designer/>. It needs a recent browser.
   It uses features available from Chrome or Edge 123, Safari 17.2 and Firefox 138
   (the page loads its code as modules that import JSON). Those minimums come from
   the feature tables, not from testing each browser. The page shows a message if
   yours is too old.
2. Start from a template (UMBRAVOX, BAPH0MAU5 or COSMIC MUFFIN), a blank theme,
   or open an existing `theme.json`. The BAPH0MAU5 template is a heptagram in the
   seal's colours, because the seal bitmap is not available to packs.
3. Edit the identity, the palette (there is a "make a palette from one colour"
   helper), the centrepiece (drop a PNG, or choose the heptagram) and the motion
   (sweeps, starting sweep, alert). The preview shows Scan, Mesh, Alert and Menu with
   colours after RGB565 clamping. The checks explain anything that is wrong.
4. Choose **Download pack** (a `.zip` of the folder) or **Install to SD card**. The
   Install page has two routes: write to the card in a card reader, or send the pack
   over the CYD's USB cable with the card left in the CYD.

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
4. It sends `emblem.bin` (if any) and then `theme.json`, each chunk acknowledged, with
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

`put` uploads `emblem.bin` (if the folder has one) and then `theme.json` into
`/themes/<name>/`, where `<name>` is the folder's name made safe the way the
designer makes it (lower case, runs of other characters become `-`). The tool reads
only regular files that really sit in that folder: a `theme.json` or `emblem.bin`
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
  after it began: the temp is removed and the old file is untouched. If the upload
  had created the pack folder and it is still empty, the folder is removed too.
- **The power is cut during the upload**: the old file is untouched, and a
  `<file>.tmp` is left behind. The loader ignores it; the next upload of that file
  replaces it, or delete it with `del`.
- **The power is cut during the swap at the end** (a window of two renames), or
  both the swap and its undo fail: the old file may be left as `<file>.old` with no
  `<file>`, so that pack is skipped at boot (or shown without its emblem). The next
  `put` of that pack renames `.old` back before it starts, so the old file returns
  even if that upload fails too; you can also `del` the leftover.
- **Replacing a pack** sends `emblem.bin` before `theme.json`. If the second file
  fails, the card has the new emblem with the old `theme.json`. That is cosmetic
  (the old theme with the new picture); run `put` again.

What the serial commands may touch, and nothing else:

- write: `/themes/<folder>/theme.json` (at most 4,096 bytes) and
  `/themes/<folder>/emblem.bin` (at most 9,728 bytes); `<folder>` follows the
  folder-name rule above and cannot be a DOS device name (`CON`, `NUL`, `COM1`...).
- delete: those two files (and a `.tmp` or `.old` an interrupted upload left), an
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
  boot, and the failure is logged. Editing the pack so its size changes gives it another
  try. Removing the folder does too, but only after the device has completed one boot
  with the folder absent: a same-size pack put straight back stays on the list. The guard
  covers loading only. A pack that crashes the loader is skipped on the next boot; a pack
  that loads fine but faults later, when drawn, is not covered (see Troubleshooting).
- The Baphomet seal's own sweeps and rite are not in packs; alerts always show.

## The 17 limitations

These are the 17 numbered items below. `web/designer/js/limits.json` has 18 entries,
because the characters rule (no `"` or `\` in name, header or author) is its own entry
there but is folded into item 4 here, and the USB route entries appear in both.

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
| A pack stays skipped after it crashed the loader | Edit it so its size changes, or remove the folder, boot once with the folder absent, then reinstall and reboot. |
| The device resets repeatedly after you select a pack | Take the SD card out and boot. Choose another theme (the selection is saved by name), then remove or fix the pack. |
| Folder not found after copy | The card is not FAT32, or `themes` is not at the card root. |
| The designer page will not load | The browser is older than the minimums in limitation 10. |
