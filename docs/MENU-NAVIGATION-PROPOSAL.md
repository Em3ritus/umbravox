# Approved native menu navigation

Implemented from the approved renders. Hardware baseline `86ebc16` remains saved separately. Native boot-strip cleanup is `738185a`. This navigation increment is host-tested/build-tested; hardware validation waits for an authorized flash. Legacy 160×120 screens are unchanged.

## Shared chrome

Header: **MENU → theme title → GPS → MESH → BRIDGE → BAT**. MENU is a 44×44 target with a three-line icon, label and active underline. Theme title is draw-only branding, no hidden Home action. Existing status order is preserved; compact omits MESH and retains mesh access in MENU. Bridge and recording keep their existing states and underline feedback.

Footer: **SCAN → DOMAIN → CODEX → POI → REC**. SCAN is explicit Home. DOMAIN, CODEX, POI and REC retain their relative order. POI and REC are actions, not destinations. No new outlines or recording-state changes.

Header/footer stay 44 px. Body stays `(8,52,W−16,H−104)`. At 480, header widths are `44 / 164 / 56 / 48 / 64 / 104`; footer cells are 96 px. At 320, header widths are `44 / 104 / 48 / [mesh omitted] / 60 / 64`; footer widths are `60 / 80 / 68 / 52 / 60`. Wider compact DOMAIN/CODEX cells retain the two-times font. Theme names use existing integer font fitting and ellipsis policy without moving controls.

## Secondary destinations

Reading order: **PROBES, MESH, ALERTS, FILES, SETTINGS, DISPLAY** in plain-label mode. Brand mode preserves ECHOES/OMENS/ATTUNE/ASPECT names. No duplicated SCAN/CODEX/DOMAIN menu launchers. FILES opens the existing WiGLE file browser (not a claim that PCAP/GPX/client/probe sidecars are listed). Opening it clears the previous selection, scroll and delete-confirmation state.

480: 3 columns × 2 rows, 6 px gaps and descriptions. 320: 2 columns × 3 rows, 2 px gaps, no descriptions, 44 px tall tiles. Cell starts use `index*(body+gap)/count`; next start minus gap gives cell end. Hits and drawing share these rectangles. The entire heading/subtitle group is centered.

Labels now lives in DISPLAY beneath the artwork preview, with a 44 px target and current PLAIN/BRAND value. Its existing setting and persistence are retained. Theme, Sweep and Alert steppers retain their 44×44 controls. Labels and values in each stepper row occupy separate lines; long values fit with ellipsis. Label changes invalidate the Display body and the Menu projection updates on return.

## Reproduce and verify

`make -C sim menu_layout_preview`, then `sim/menu_layout_preview <existing-output-directory>` renders production SCAN, MENU and DISPLAY at both sizes in UMBRAVOX and COSMIC MUFFIN themes. Data are fixtures, not live telemetry.

`sim/native-hit-contract.json` records the approved independent chrome rectangles across the 20 SCAN/DOMAIN state/size cases; `gen_oracle.py` generates the test include. Host coverage includes all-screen shared hits, no wordmark hit, six launcher bindings, file-selection reset, nonoverlap/minimum targets, Labels projection/dispatch/dirtiness, and partial repaint equality. Radio, SD, GPS and touch scheduling are unchanged.
