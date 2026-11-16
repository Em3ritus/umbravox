# Theme art switches: hide the heptagram and the contact dots (short design)

Status: approved by owner 2026-11-15. Branch `art-switches`, stacked on `csv-dedupe`.

## Intent and why
Owner uploads their own image or radar video in the theme designer. On the Waveshare wide radar the heptagram is drawn
after the video (`ui/scan_render.h` around 1127-1146), so it covers the clip, and the coloured contact blips sit over the
art. The owner wants the art clean and a switch for each.

## Goals
- Two optional `theme.json` keys, `heptagram` and `contacts`, each `"on"` (default) or `"off"`.
- `heptagram: off` skips the drawn heptagram figure (and, on the CYD heptagram path, its lattice, range rings and rune
  ring, so only the user's art remains). Emblem and video are never affected.
- `contacts: off` skips the contact blips (and their halos/flare) on the scan radar. Sweep and needle stay.
- Designer: two checkboxes in the Centrepiece card, preview honours them, round-trips through load/save, checks accept them.
- Existing packs and built-in themes are unchanged (defaults on).

## Non-goals / anti-goals
- No scripting, no new file types, no transmit-side anything. Contact data is still collected and logged; only the
  drawing is skipped.
- Do not hide the alert takeover or the header glyph (separate surfaces; not what the owner described).

## Constraints
- `format` stays 1; unknown keys are ignored by old firmware, so an old board shows heptagram and dots until reflashed
  (say so in the docs and the designer note).
- Strings, not JSON booleans: the on-device parser only skips `true/false` and reads strings for enums.
- A bad value ("maybe") skips the pack like other bad enums. Duplicate key skips the pack.
- Hardware gate after firmware change: `tools/bootprobe.py`; scan draw median must stay near 12 ms on the CYD.

## Approaches considered
1. Two switches (chosen by the owner): full control, needs firmware + designer + docs.
2. Video auto-hides heptagram plus a contacts switch: smaller, loses control.
3. One "clean art" switch: simplest, no separate control.

## Decided
Approach 1.

## My calls (correct me)
- Both boards (CYD and Waveshare) honour the switches, since the packs are shared.
- Key names `heptagram` / `contacts`, values `"on"`/`"off"`.
- Designer shows the heptagram checkbox always (it also applies to a video on the heptagram centrepiece) and the contacts
  checkbox always.

## Left to the builder
Exact draw-code edits, parser struct field names, preview wasm plumbing, test names. Tests first, RED shown.
