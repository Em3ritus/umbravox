# Native shared header

The header is composed from each screen's stable scene snapshot. Bridge, PMU availability and charging changes invalidate the status band, not the body. No new PMU polling was added; the existing shared-bus guard and two-second cache remain.

| Region | 480 wide (x, width) | 320 wide (x, width) |
|---|---|---|
| Theme title | 8, 128 | 8, 96 |
| GPS | 136, 68 | 104, 52 |
| Mesh | 204, 60 | MENU → MESH |
| Bridge | 264, 112 | 156, 92 |
| Battery | 376, 96 | 248, 64 |

All controls have 44-pixel-high hit regions. The bridge's inset outline is visual only; its whole cell toggles the selected bridge app on/off on every native screen. App selection remains in SETTINGS. There is no SCAN-body bridge button.

The title follows `pal::themeName(theme)`: UMBRAVOX, BAPH0MAU5, COSMIC MUFFIN and SD theme names. Integer font scaling fits the fixed title area; names exceeding its one-times capacity end with an ellipsis. Controls never move. SLICK is reserved as a future theme name, not registered as a duplicate theme or assigned invented artwork.

GPS shows 3D, 2D, FIX, STALE or ACQ using the existing validity/age contract. Battery shows BAT / percent, CHG / percent, BAT / NO BAT when the responding PMU reports no cell, or BAT / N/A when unavailable. Invalid percentage reads remain unavailable (255 sentinel) rather than being clamped to a fabricated 0% or 100%. PMU availability is still the existing driver's initialization state; this increment does not establish per-transaction health or battery hardware acceptance.

Verification includes both hit-oracle sizes, every native screen's bridge target, battery and bridge transitions compared against a full repaint, and previous SCAN body/navigation pixel goldens. The latter exclude the removed body button and the deliberately changed header. Both firmware targets are compile checks; the 320 native mockup does not enable the engine in the legacy NM firmware.

No flashing is part of this change. Bridge startup, phone connection and battery hardware checks remain pending.
