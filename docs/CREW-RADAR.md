# Cvlt and Baph0Mau5 radar artwork

The existing built-in theme at ID 1 is now displayed as `CVLT OF BAPH0MAU5`.
Its ID is unchanged. The native engine draws the approved violet ritual seal,
seven satellite marks, interrupted outer ring and green sweep. Labels are brighter.

`packs/z-baph0mau5/theme.json` adds the distinct `BAPH0MAU5` theme using the
`mascot` centrepiece. The prefix puts this folder after the existing `slick`
folder. Saved pack preferences continue to resolve by name. No new built-in ID
is inserted. The subtle purple mascot has curled horns, X eyes, teeth and a
small green flame. Both artworks leave contact markers above the artwork,
with a dark one-pixel backing to separate marks from linework.

Native SCAN spectrum amplitude is 16 pixels; all 21 bins remain. Compact radar
radius is 39 pixels, full-size radius 85. The same geometry drives redraw bounds.
Long Cvlt and Cosmic Muffin names occupy two lines inside the existing title
region. Other long names retain bounded truncation. No touch geometry changes.

Artwork is baked into row-sorted, palette-indexed horizontal spans in flash.
Device drawing uses no dynamic allocation, polygon rasterization or trigonometry
for these artworks. Regenerate from the repo root:

```
c++ -std=c++17 -O2 -o /tmp/gen-crew tools/gen_crew_art.cpp ui/palette.cpp ui/themepack.cpp ui/muffinseal.cpp ui/motif.cpp ui/canvas.cpp ui/font5x7.cpp
/tmp/gen-crew > /tmp/crew_art.h
cmp /tmp/crew_art.h ui/crew_art.h
```

The host baker deliberately uses floating-point geometry and temporary vectors;
none of that source is part of the device build. `sim/crew_png.cpp` produces
native-size previews using the actual compositor. `test_clean_radar` covers
both sizes and all 72 sweep phases for radar, Cvlt and mascot styles, comparing
partial repaints with complete frames. The existing theme parser, palette,
render, and contrast checks remain in the host suite.

Portrait orientation remains outside this change. No RF scheduling or bridge behavior changes.

## Complete crew set

- UMBRAVOX: unchanged original identity.
- CVLT OF BAPH0MAU5: violet ritual seal (built-in ID 1).
- COSMIC MUFFIN: supplied NEBULA DRIFT palette and native planet/orbit design
  (built-in ID 2; the legacy canvas retains its muffin bitmap).
- SLICK: crowned-dog logo, muted ruby/bronze; `packs/slick/`.
- BAPH0MAU5: subtle mascot; `packs/z-baph0mau5/`.
- EM3RITUS: supplied GRIMOIRE palette and upright ceremonial sigil;
  `packs/z-em3ritus/`.
- EMBER: renamed orange/gold clean radar; `packs/z-ember/`.
- SKULD: supplied Viking/ring identity, cyan/violet; `packs/z-skuld/`.

The two logo packs use `centrepiece: "radar-emblem"` and require a valid
`emblem.bin`. The existing bounded loader validates it and keeps its bytes in
PSRAM. Black RGB565 pixels are transparent over the radar. Optional `artOpacity`
(0..100; default 100) and `artScale` (25..100 percent of radius; default 82) let
packs choose a watermark without changing the contact markers or UI labels.
SLICK uses 40% intensity and 64% radius. Other packs retain defaults.

`tools/png_to_emblem.py` uses Pillow on the host to convert an already-square
source to 96x96 indexed RGB565, at most 16 colors. Logo texture and tiny ring
lettering are necessarily reduced at compact resolution. Native artwork rendering
is clipped to the current strip and uses a fixed 16-color local palette, not a
new framebuffer. Missing or invalid logo binaries cause the pack to be skipped.

No new built-in IDs were inserted. Saved SD-pack selections resolve by name.
The new SLICK replaces the old SLICK identity; its former design remains available
as EMBER. No display orientation, capture timing or bridge behavior changes.
