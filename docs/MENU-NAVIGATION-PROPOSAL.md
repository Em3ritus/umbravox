# Menu navigation proposal — review only

Working hardware baseline: `86ebc16`. The startup-strip cleanup is independent of this proposal. No navigation implementation or routing changes are included.

## Shared chrome

- Header: **MENU → theme title → GPS → MESH → BRIDGE → BAT**. MENU is a 44×44 target with a three-line icon, label, and active underline. No outlined button. Existing readout order stays intact. Bridge retains its ON underline. Theme title becomes branding only; the explicit SCAN footer is Home.
- Compact header retains the existing mesh fallback: omit MESH, access it through MENU. This is not a loss of mesh functionality.
- Footer: **SCAN → DOMAIN → CODEX → POI → REC**. The relative order of DOMAIN, CODEX, POI and REC is unchanged. SCAN replaces the previous implicit theme-title/Home interaction. POI and REC remain actions, not destinations; recording states still need to use their existing labels/highlights in implementation.
- Header/footer height remains 44 px. Body remains `(8,52,W−16,H−104)`. No change to the SCAN content layout.
- At 480, header widths are `44 / 164 / 56 / 48 / 64 / 104`; footer cells are 96 px each. At 320, header widths are `44 / 104 / 48 / [mesh omitted] / 60 / 64`; footer widths are `60 / 80 / 68 / 52 / 60`. This keeps compact DOMAIN/CODEX at the same two-times font size as the other footer labels.
- Theme title uses the existing integer font fitting. Both UMBRAVOX and COSMIC MUFFIN are rendered in the review. Custom titles will still need the existing length/ellipsis policy when implemented.

## Six secondary destinations

Reading order: **PROBES, MESH, ALERTS, FILES, SETTINGS, DISPLAY**.

Remove redundant SCAN/CODEX/DOMAIN menu launchers because their footer controls are always visible. Add direct FILES access (currently the existing WiGLE file browser; do not imply the new CSV sidecars are listed there). Move the existing Labels toggle into DISPLAY, with its current plain/brand functionality preserved. SETTINGS continues to hold radio and capture controls. These are proposed changes, not yet wired.

At 480: 3 columns × 2 rows, 6 px gaps, descriptive subtitles. At 320: 2 columns × 3 rows, 2 px gaps, no subtitles, 44 px tall tiles. For a body dimension B, cell starts use `index*(B+gap)/count`; the next start minus the gap is the cell end. All targets derive from these same rectangles. Pixel labels are vertically centered, with wide subtitles included in the centered group. The source asserts minimum 44 px menu/footer/status targets.

These PNGs use the real firmware font, palette and SCAN compositor. Only the proposed chrome/menu are host-drawn. Counts and GPS/bridge states are fixtures, not device telemetry. No implementation approval is implied by generating these pictures.

## Regenerate

From the repository: `make -C sim menu_layout_preview`, then `sim/menu_layout_preview <existing-output-directory>`.

The host-only generator renders SCAN and MENU at 480×320 and 320×240, in UMBRAVOX and COSMIC MUFFIN themes. The production layout, hit regions and touch routing are unchanged pending review.
