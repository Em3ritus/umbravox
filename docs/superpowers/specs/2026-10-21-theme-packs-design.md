# Theme Packs — Design

**Date:** 2026-10-21
**Status:** draft for review
**Project:** 1 of 4 in the community-themes roadmap (see "Roadmap context").

## Intent

UMBRAVOX is meant to be the owner's own wardriver, looking how they want. Today
a theme is code: a palette row, a baked-in image and a branch in the draw code,
compiled into the firmware. This project lets anyone make a theme **without a
toolchain** and put it on their CYD: a **theme pack** is a small folder on the SD
card that the device reads at boot, and a **web designer** builds one from a
palette, a picture and a few choices, with a live preview and one-click install.

A pack is **data only**. It cannot run code. It chooses colours, a centrepiece
picture and which of the built-in sweeps and alerts to use; the firmware keeps all
behaviour. This keeps packs safe to share and keeps UMBRAVOX passive: nothing in a
pack can touch the radios, the SD card or any network.

## Roadmap context

Four projects, each shippable alone, in this order:

1. **Theme packs** (this document): the pack format, the loader, the designer, install.
2. Sharing: a public themes repository, CI validation, preview gallery.
3. A sandboxed scripting runtime for the dial, sweeps and alerts.
4. Scripting for the whole UI (layout, every screen, touch).

Project 1 must leave room for 3 and 4 (a versioned format, a theme object that can
later carry a script) but builds none of them.

## Goals

- A pack format that a human can read and edit by hand, and a loader that is safe
  against any file content.
- Installed packs appear in the existing THEME picker on the ASPECT screen, after
  the built-ins, and the choice survives a reboot.
- A web designer that needs no install: start from a template or blank, edit
  colours / centrepiece / motion, see the real device drawing code render it,
  check it, then download or install it.
- **Every limitation is visible to the user** (see "Limitations"), in the designer,
  in the documentation and on the device, before they are surprised by one.
- The built-in themes keep working exactly as today (UMBRAVOX render baselines
  stay byte-identical).

## Non-goals / anti-goals

- No scripting, no custom animations, no custom layouts or fonts (projects 3 and 4).
- No sharing infrastructure (project 2): no repository, gallery or submission flow.
- No downloading on the device. The CYD never joins a network; packs arrive on the card.
- A pack must **never** be able to boot-loop, hang or brick the device, hide or
  disable alerts, or run code. A pack that does any of those is a failure even if
  it "works".
- Not a re-skin of the built-ins: they stay compiled in.

## Constraints (verified in the code)

- 160×120 canvas, blitted 1:2 to a 320×240 panel; single-core ESP32-C5 with **no
  FPU**. Draw is ~7 ms of a ~21 ms frame today; a theme must not change that.
- Colours are RGB565. The font is 5×7, **all-caps ASCII 32..95** (lowercase folds
  to capitals); there are no other glyphs.
- The SD card is mounted in `setup()` before settings are applied
  (`device/main.cpp`: `sdlog::begin()` at setup, `prefs::load` later in the loop),
  so packs can be read in time to apply a saved choice.
- The theme count is hard-wired in a few places: `pal::ThemeId`/`TH_COUNT`,
  `adjustAspect` (theme stepping) and `clampSettings` in `ui/nav.cpp`, and
  `sim/test_*` loops. `Settings.theme` is a `uint8_t`; `Settings` is 12 bytes with
  no padding and a canary test enforces that.
- Today's `Row` (theme table row) holds: name, a sweep bitmask (16-bit), dial
  geometry (`dialY, dialH, dialCx, dialCy, rField, rRunes, rBound, rStar, specY,
  specH`), and 24 colours (`void, panel, grid, edge, edgeLit, dim, text, bright,
  witch, witchHi, bone, sweep, sweepHi, live, liveHi, ramp[4], magenta, ice,
  green, amber, red`).
- The scan screen's centrepiece branches on theme id (`drawDialAt` in
  `ui/screen_scan.cpp`): heptagram, Baphomet seal, or the muffin bitmap. The four
  cosmic alert rites currently call `drawMuffin` directly.
- Alert colour is the *confidence*: `pal::RED` (HIGH), `pal::AMBER` (MED),
  `pal::WITCH` (LOW) edge a card. A theme that sets those near the background
  colour would make alerts hard to see.
- **emscripten is not installed on the development machine.** Node and npm are.

## The pack

A folder `/themes/<folder>/` on the card:

| file | required | purpose |
|---|---|---|
| `theme.json` | yes | the theme's settings, below |
| `emblem.bin` | if `centrepiece` is `emblem` | the centrepiece picture, device format |
| `preview.png` | no | the device ignores it; the designer does not write one (a pack is `theme.json` plus the optional `emblem.bin`) |

### `theme.json`

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
  "colors": { "void": "#09071C", "panel": "#16103A", "...": "..." }
}
```

- `format`: integer; this document defines `1`. A pack with a higher number is skipped
  with a clear reason. Unknown keys are ignored (so later projects can add some).
- `name`: 1–20 characters, A–Z 0–9 space `- .`; also the key the choice is saved by. `name`, `header` and `author` cannot contain a double quote or a backslash (the parser reads no string escapes).
- `author`: 0–24 printable ASCII; informational.
- `header`: 1–18 characters from the font (uppercase, digits, space and punctuation in
  ASCII 32..95); shown in the scan-screen header. Lowercase is folded to capitals.
- `dial`: `compact` (UMBRAVOX's geometry, with the spectrum strip) or `large`
  (BAPH0MAU5/COSMIC's geometry, no strip). Presets only.
- `centrepiece`: `emblem` (a picture from `emblem.bin`) or `heptagram` (UMBRAVOX's
  drawn figure). The choice sets the **whole dial look**: `emblem` is COSMIC MUFFIN's
  (the picture fills the dial, no lattice, ring or runes); `heptagram` is UMBRAVOX's
  (lattice, range rings, rune ring and the heptagram). The Baphomet seal is not
  offered: it is built around its own face art.
- `sweeps`: which sweeps the theme offers, from `radar pulse star candle galaxy orbit
  starfall pulsar comet aurora`. `gaze` is not offered (it needs the seal's eyes).
  At least one. `sweep` must be one of them.
- `alert`: `ward` (UMBRAVOX's), `supernova`, `vortex`, `comet`, `lockon`. `summon` is
  not offered (it is the Baphomet seal's own rite). Any alert may be paired with either
  centrepiece (see "Motif"). For the four cosmic rites the pack fixes the choice (the
  per-user `alertStyle` setting remains for COSMIC MUFFIN only).
- `colors`: the **20 roles** a person picks (`void panel grid edge edgeLit dim text
  bright witch witchHi bone sweep sweepHi live liveHi magenta ice green amber red`), as
  `#RRGGBB`. The built-in theme table has 24 colours; the other 4 are the shading ramp,
  which only the Baphomet seal reads and packs cannot use it, so a pack theme just sets
  the ramp to `void, edge, dim, bright` as UMBRAVOX's row does. A missing required
  colour skips the pack.

### `emblem.bin`

Little-endian: bytes `'U' 'V' 'E' 1`, then `w` (u8), `h` (u8), `ncol` (u8), one
reserved byte (0), then `ncol` RGB565 values (u16 each), then `w*h` palette indices
(u8 each, row-major). Limits: `w`,`h` ≤ 96; `ncol` 2..16; file ≤ 9.5 KB. The built-in
muffin uses the same pixel/palette representation (`ui/muffin.h`), so the muffin is
just an emblem and one drawing routine serves both.

### Limits

| what | limit |
|---|---|
| packs the device loads | 8: the first 8 by folder name; the rest are ignored and logged |
| folder name | letters, digits, `-` `_`, 1-31 characters; anything else is skipped |
| `name` | unique among packs and not a built-in's name (`UMBRAVOX`, `BAPH0MAU5`, `CVLT OF BAPH0MAU5`, `COSMIC MUFFIN`: every name a built-in displays) |
| `theme.json` | ≤ 4 KB |
| `emblem.bin` | ≤ 96×96, ≤ 16 colours, ≤ 9.5 KB |
| whole pack | ≤ 64 KB |
| `name` / `header` / `author` | 20 / 18 / 24 characters |

## Device behaviour

- **Scan.** At boot, after the card mounts, read `/themes/*`. Parse each `theme.json`
  into a runtime theme row (the compiled table stays `constexpr`; packs fill a small
  RAM table appended after it). All admitted emblems (at most 8 x 9.5 KB) are kept resident in
  PSRAM at boot: it avoids a load callback inside the shared UI code.
- **Pick.** The THEME row on ASPECT steps built-ins then packs; `adjustAspect` and
  `clampSettings` use a dynamic theme count. Sweeps offered follow the pack's list; a
  chosen sweep the theme does not offer resets to RADAR as today.
- **Persist.** Save the pack **name**, not an index, so adding or removing other packs
  does not change what is selected. If the saved pack is missing or invalid at boot,
  fall back to UMBRAVOX and log it.
- **Draw.** The centrepiece draws the pack's emblem through the generalised emblem
  routine. Colours come from the pack.
- **Motif.** Every alert rite draws the active theme's *motif* where it needs a
  picture of the theme: the emblem blit (with the rite's brightness lift) for an
  `emblem` theme, a heptagram stroke in the rite's colour for a `heptagram` theme. The
  four cosmic rites call it instead of `drawMuffin`; the ward calls it in place of its
  faint figure. This makes every centrepiece/alert pair defined, with no forbidden
  combinations. The built-in themes keep drawing exactly what they draw today.
- **Sweep fallback.** A chosen sweep the theme does not offer resets to the pack's own
  starting `sweep` (not RADAR, which a pack need not offer).
- **Show failures.** A skipped pack is logged in `/diag.log` with the reason. ASPECT
  shows the skipped count when any pack was skipped, so a failure is visible without a
  computer. As shipped the text is split around the dial preview: `N SKIPPED` to its
  left and `DIAG LOG` to its right (no full-width strip exists without shrinking the
  preview); the reason is in `/diag.log`.
- **Reload.** Packs are read at boot only; a new or changed pack needs a reboot.
- **Ordering and clashes.** Folders are read in sorted name order so the same 8 always
  win. A folder name outside `[A-Za-z0-9_-]{1,31}` (including `.`/`..` and anything with
  a path separator) is skipped. A `name` equal to a built-in's, or to an earlier pack's,
  is skipped, and each skip is logged with its reason.

## Safety

- **Parse defensively.** Bounded reads (never trust a length), strict types, range
  checks, no allocation sized by file contents, no recursion. The parser gets the same
  hostile-input treatment as the BLE advertisement parser (`ble_ad.h`): a host
  fuzz/ASan test over malformed, truncated, oversized and adversarial files.
- **No boot-loop.** Before parsing or loading a pack the device records "loading X" in
  NVS and clears it afterwards. If the board resets while it is set, the next boot
  skips that pack and logs it. A custom theme therefore cannot trap the device.
- **Alerts stay visible.** Contrast is enforced **where a colour is used as an alert
  edge**, not by rewriting palette roles. When `drawAlert` picks the confidence edge
  (`red` HIGH, `amber` MED, `witch` LOW), it checks that colour against `void` and
  `panel`; if the contrast is below the minimum (about 3 : 1) it draws a fixed safe
  colour at that edge instead (fixed constants, not derived from the pack), so chrome
  that shares the role is untouched. The status-only roles `amber` and `red` are also
  replaced outright with UMBRAVOX's values when they fail, with a log line; the pack
  still loads. Built-in themes pass the check, so their output is byte-identical.
  Themes choose how an alert looks, never whether it shows.
- **No code, no I/O.** A pack is never executed and cannot cause any file or radio access.
- **Bounded cost.** A theme cannot change per-frame work beyond the built-in sweeps,
  rites and one emblem blit, so the frame budget is unchanged.

## The designer (web)

A static site, hosted with the existing flasher on GitHub Pages (`web/designer/`),
no backend, no account. Pages: **Start** (the three built-ins as templates, a blank
theme, open an existing `theme.json`/folder), **Design**, **Install**. Mockups:
`private/theme-designer/1-start.png`, `2-design.png`, `3-install.png`.

- **Design** edits identity, the palette (grouped by role, with a "make a palette from
  one colour" helper), the centrepiece (drop any PNG; the page crops/shrinks it and
  reduces it to ≤ 16 colours exactly as `tools/gen_muffin.py` does) or the heptagram,
  and motion (which sweeps, the starting sweep, the alert).
- **Live preview** runs the **real device drawing code** (`ui/`) compiled to
  WebAssembly, with tabs for Scan, Mesh, Alert and Menu, so it cannot drift from the
  device. The preview shows colours *after* RGB565 clamping.
- **Checks** run live and explain themselves: all colours set, text/background and
  status/background contrast (warn, with the ratio), emblem size and colour count,
  pack size against the limit, header text fits and uses supported characters.
- **Export:** *Download pack* (a `.zip` of the folder) always works; *Install to SD
  card* uses the browser's File System Access API to write
  `/themes/<name>/` onto a card the user picks, then **reads it back and verifies it**.
  That API needs desktop Chrome, Edge or Opera; elsewhere the page says so and offers
  the download with copy-it-yourself instructions. *Share...* is a placeholder for
  project 2 and is not built here.

## Limitations the user must be told

These are requirements, not documentation extras. Each must appear **where the user
meets it** and in a single reference page:

1. A new or changed pack needs a **reboot** to appear; the card is read at boot.
2. At most **8 packs** are loaded (the first 8 by folder name). Folder names are
   letters, digits, `-` and `_` only, and a theme name must be unique and cannot be a
   built-in's name (`UMBRAVOX`, `BAPH0MAU5`, `CVLT OF BAPH0MAU5`, `COSMIC MUFFIN`);
   otherwise that pack is skipped. Only the first 32 folders by name are examined.
3. The centrepiece is **≤ 96×96 and ≤ 16 colours**; photos are reduced and will look
   posterised. Expect pixel-art, not photographs.
4. Header text is **uppercase only** (lowercase shows as capitals), limited to
   letters, digits, space and common punctuation, **18 characters** max. Name, header
   and author cannot contain a double quote or a backslash.
5. Colours are **RGB565**: fine differences between near colours disappear. The
   designer previews the clamped result.
6. **Presets only** for layout: two dial sizes. No custom screen layout, fonts or
   animations yet (those are later projects).
7. Sweeps and alerts come from the **built-in set**. Two are not available to packs:
   *Gaze* and the *Summon* rite (they belong to the Baphomet seal).
8. A theme **cannot hide or disable alerts**; very low-contrast status colours are
   replaced with safe defaults.
9. The selection is saved by **name**: renaming a pack makes the device fall back to
   UMBRAVOX until it is selected again.
10. **Installing from the page** needs desktop Chrome/Edge/Opera. Other browsers use the
    zip download and copy the folder to the card by hand.
11. The SD card must be **FAT32** (as for logging).
12. A pack that fails to load is **skipped**, not partially applied; the reason is in
    `/diag.log` and the count is shown on ASPECT (`N SKIPPED` and `DIAG LOG`, split
    around the dial preview).
13. Packs are checked for **format, not taste or origin**: nothing here signs, vets or
    moderates a pack. Only install packs from people you trust (moderation is project 2).

Where each appears: the designer shows 3, 4, 5, 6, 7, 8 and the check results inline
and 1, 2, 10, 11 on the Install page; `docs/theme-packs.md` lists all of them plainly;
the README links to it; on the device, 12 is shown on ASPECT and logged.

## Testing

- **Parser:** host tests for valid packs, every limit exactly at and one past its bound,
  missing/extra/duplicate keys, wrong types, bad hex, oversized files, truncated
  `emblem.bin`, malformed JSON; run under ASan/UBSan with a mutation fuzz loop.
- **Theme table:** a pack theme resolves to a complete row; the dynamic count works in
  stepping and clamping; saved-by-name resolution including missing/renamed packs;
  the `Settings` canary still passes.
- **Safety:** contrast clamp (low-contrast status colours replaced, pack still loads);
  boot-loop guard (set flag at boot → pack skipped); 8-pack cap.
- **Draw:** the generalised emblem routine draws inside the dial and nowhere else (as
  `test_muffin` does); the cosmic rites draw the active emblem; **UMBRAVOX render
  baselines unchanged**; `test_trig` still passes for pack themes.
- **Designer:** the converter (PNG → `emblem.bin`) is tested against `gen_muffin.py`'s
  output for the same input; the exported pack round-trips through the host parser;
  the checks fire on crafted bad themes. WebAssembly preview output is compared with
  the simulator's PNG for the same theme and frame.
- **Hardware:** install a pack on a real card, select it, reboot, confirm it persists;
  a deliberately bad pack is skipped and reported on ASPECT and in the diag log.

## What is left to the builder

- Runtime-table design (how packs are held beside the `constexpr` built-ins) and how
  `Settings` represents "a pack" (a marker plus the name stored in NVS) while keeping
  the 12-byte canary or updating it deliberately.
- The exact contrast threshold and the derivation of the shading ramp.
- Getting the UI code into WebAssembly. **emscripten is not installed here**; the
  builder installs it (or the CI-only route) and reports the cost. If compiling `ui/`
  to WebAssembly proves impractical, **stop and ask** rather than substituting a
  separate re-implementation of the renderer: the "preview is the real code" property is
  a requirement.
- Zip creation and the File System Access write/verify in the browser; the page's
  styling follows the mockups and the existing flasher page.

## Decisions made here (the author's calls; flag any that are wrong)

- JSON for `theme.json` (hand-editable, no extra tool); a small binary for `emblem.bin`.
- Folder `/themes/<folder>/`; 8-pack cap; the limits table above.
- Derived shading ramp rather than asking people for four more colours.
- Dial **presets** instead of free geometry, to keep the dial and spectrum strip
  consistent and the frame budget fixed.
- `gaze` and `summon` withheld from packs because they are tied to the Baphomet seal.
- The built-in muffin becomes "an emblem" (one drawing routine) but the built-in themes
  stay compiled in rather than becoming packs.
- Contrast-clamping status colours rather than rejecting the pack, so a pretty theme with
  one faint red still loads.
- Reboot-to-apply rather than hot reload.
- Contrast is enforced at the point an alert edge is drawn, not by rewriting a palette
  role, so a pack's chrome keeps its look and built-in renders do not change.
- Any alert pairs with either centrepiece through a "motif" (emblem or heptagram), so
  there are no forbidden combinations and no extra limitation for users.
- `emblem` means COSMIC's whole dial look and `heptagram` means UMBRAVOX's; the dial
  size preset (`compact`/`large`) is independent of that choice.
- Clash rules: sorted folder order, first 8 win, reserved built-in names, safe folder
  names, duplicate names skipped.
