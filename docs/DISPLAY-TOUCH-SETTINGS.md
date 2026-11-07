# Display and touch settings

Working baseline: c581160. Native Waveshare UI: Menu → Display, then Display / Touch / Appearance tabs.

Display owns the existing brightness and idle preferences (same NVS keys; no migration/reset). Brightness opens a large live stepper with 1, 3, 5, 10, then 10-percent increments. Idle After selects Never, 15/30/60/120/300 seconds; When Idle selects Dim, Off or Animation separately. Values save through the existing two-second preference debounce. Idle dimming cannot increase a 1-percent brightness setting. Orientation remains explicitly read-only, landscape: native rotation has not been implemented. General Settings keeps radio, recording, bridge-app and node controls, with their original action IDs.

Touch Test draws up to 32 sampled points with mapped coordinates and contact/release status. It reads the sampler's cached output (including its existing short-failure bridge), not a second I2C poll. Pad touches have no navigation actions. Clear empties the trail; Done returns. Header/footer navigation remains available outside the pad. First touch after idle remains wake-only.

Waveshare FT6336 coordinates remain factory-mapped. Sensitivity edits a bounded threshold (8–30, step 2; lower is more sensitive; default 12). Changes remain staged until Done. Done writes the controller under the existing shared-bus guard and saves to the separate `touch/threshold` NVS key after an ACK. On controller or NVS failure, the editor stays open with a retry message; an NVS failure can leave the new threshold active for this boot but not persisted. Navigation away discards unapplied edits. Default selects 12; Done applies it. This is a threshold adjustment, not coordinate calibration, and its useful range still needs testing with the user's panel/case.

Appearance owns theme, sweep, alert style, and label mode. Theme and sweep selections use the existing selection functions. Theme-dependent alert styles are non-actionable when unavailable. Theme-pack loading and authorship are unchanged.

Shared native layouts render at 480×320 and 320×240. The actual native device adapter is currently Waveshare-only. NM retains its existing legacy UI and hardcoded resistive calibration. The earlier compact calibration-wizard proposal is **not implemented or exposed** by this change; it requires the NM adapter integration. Compact renders of this implementation accurately show factory mapping, not a working NM calibration wizard.

Verification: sanitizer-enabled geometry/hit isolation, bounds, control stepping, capped trail, and region-vs-full repaint tests across both native sizes; full host regression suite; native WS and legacy WS/NM build checks. Hardware startup and interactive acceptance are separate checks. No automatic touch injection or recording is used during validation.
