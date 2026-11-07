# SLICK clean radar and compact spectrum

Production landscape implementation; portrait remains a host design study.
Baseline 12bd983 and preceding preview a488840 remain available. Flash authorized for the primary camera board after validation.

- Optional native diamond badge, independently selected by theme data.
- Radar centrepiece style: no seal, three rings, crosshair, existing contact markers.
- A 45-degree eight-level fading wedge, with a bright leading ray.
- Existing 72-phase, 20 Hz native animation cadence; no scan-timing changes.
- Spectrum capped at 168 px, centred beneath the field, amplitude capped at 32px.
  All 21 channel bins and the 13/8 band split are retained. No aggregation or loss
  of channel data. This compact spectrum also applies to the other native themes.

Implementation avoids per-pixel transcendental functions: triangle half-plane and
circle tests use integers, fade interpolation uses bounded 32-bit arithmetic.
Drawing clips to the current strip, uses eight RGB565 fade entries on the stack,
and adds no persistent framebuffer or bitmap. Existing radar-only invalidation
and unchanged-frame suppression remain in use. Host tests compare partial repaint
with complete repaint across all 72 phases; no on-device timing claims are made.

Next improvements worth measuring: cache fade colors/direction per scene if profiling
shows the per-strip setup matters; evaluate marker overlap under heavy traffic;
consider decaying/peak channel amplitudes while preserving raw telemetry. Do not
change RF dwell or BLE duty cycle as part of appearance work.

Portrait implementation still needs the native engine geometry, orientation setting,
frame stride/touch mapping, pagination and dialog review described in
PORTRAIT-DIRECTION.md. The theme does not switch display orientation.

Approved proportion revision: landscape centre (311,137), radius 85 (170px
diameter); original counter dimensions retained. Labels use the top corners
outside the circle; the spectrum stays beneath it. Compact radar retains its
27px radius, shifted up 2px to clear taller bars. Precomputed ring points now
include radii 28/56/85, avoiding the fallback trig loop at the larger size.
Dirty bounds use the same new centre and radius so the enlarged sweep erases
correctly. Portrait host preview uses a 224px diameter at 320x480.
