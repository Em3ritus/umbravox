# SCAN latency: evidence and changes

Baseline: `fcd0324`. Owner reports curtain eliminated, first-tap and STOP working,
and passive operation verified. Those mechanisms are retained.

## Device evidence before this change

The supplied `fcd0324-scan-serial.txt` contains two reporting windows:

| Metric | First window | Second window |
|---|---:|---:|
| Completed frames | 3 | 4 |
| Compose total | 7,425,277 us | 9,227,648 us |
| Transfer total | 189,818 us | 198,452 us |
| Max slice | 1,530,105 us | 520,045 us |
| Render per mille | 722 | 925 |
| Maximum touch sample gap (since boot) | 45 ms | 48 ms |
| Late touch cycles (since boot) | 5 | 13 |
| Dropped touch events | 0 | 0 |

Composition is over 97% of measured rendering time. Dividing window composition
by completed frames gives approximately 2.48 and 2.31 seconds/frame (window totals
can include work on an unfinished frame). Transfer averages are about 63 and
50 ms. This localizes the dominant observed latency to composition, not input
loss or SPI alone. The current capture has no `WARN loop: stalled` line and no
numeric loop-gap report. It cannot establish a distinct recurring 2.5-second
stall or rule out one outside this short capture.

Older captures show max render slices around 2.45 seconds alongside 2.5-second
loop warnings. They are not the current baseline. Do not attribute those warnings
to setup's SD/theme scans: the loop-gap clock excludes setup. The measured render
cost itself is sufficient to explain a seconds-long first-frame wait.

## Localized work and fix

The SCAN compositor repeated three 1,440-point rings, each using double-precision
sine and cosine, for EVERY 16-row strip. Most computed pixels were outside that
strip. At 480x320, a six-contact fixture executed 173,320 trig calls/frame.

- Precomputed integer ring/star geometry for both native profiles preserves the
  exact old raster, including its rounding. Other logical sizes retain a formula
  fallback. `tools/gen_radar_geometry.py` reproduces the tables.
- Text, field artwork and emblem work is culled/clipped to the current strip.
- Composition uses the internal SRAM strip buffer, then sequentially copies to
  the PSRAM staging frame. The panel still sees only completed updates.
- Frame comparison is limited to the staged row span; unchanged chrome remains
  untouched. No blackouts or larger blocking entry budget were introduced.
- The old native sweep ray was fixed-angle, not clock-animated. Device SCAN now
  opts into a 20 Hz phase clock with a 3.6-second turn. Pure static fixtures retain
  their old appearance. Sweep/contact-only changes invalidate radar rows, not the
  entire body. The phase clock is a target cadence, not a claimed hardware FPS.

## Measured host comparison (not device timings)

Same compiler, O2, 20 full-frame iterations, six-contact static fixtures. All six
pixel hashes match the pre-change renderer exactly.

| Profile / theme | Before compose us/frame | After compose us/frame | Before trig calls/frame | After |
|---|---:|---:|---:|---:|
| 480x320 / 0 | 653 | 55 | 173,320 | 120 |
| 480x320 / 1 | 663 | 53 | 173,320 | 120 |
| 480x320 / 2 | 819 | 55 | 173,040 | 120 |
| 320x240 / 0 | 461 | 28 | 129,990 | 48 |
| 320x240 / 1 | 466 | 28 | 129,990 | 48 |
| 320x240 / 2 | 485 | 28 | 129,780 | 48 |

Reproduce the optimized benchmark with `make -C sim bench_scan` followed by
`sim/bench_scan`. These host results establish reduced work, not C5 speedup.

## Device measurement after the owner's flash

No hardware was flashed or accessed for this change. After-values for loopGap,
maxSliceUs, rpm, touch gaps and navigation latency are therefore **not measured**.
The build now provides the missing evidence rather than inventing these values:

- `[eng]` retains fr/sl/compose/xfer/maxsl/rpm and adds `diff`, `entry`, `gap`,
  `wifi`, `ble`. `entry` is maximum enter-to-completed-presentation latency in the
  reporting window, not sensor-to-action latency. `gap` is the maximum loop-start
  interval. WiFi/BLE fields are received-event deltas, not transmissions.
- Diff/comparison/mirror-copy time is now included in render totals. `maxsl` now
  measures the whole service call, not separately counting compose and each
  region transfer. `sl` counts service calls. These definitions are more complete
  than the old metrics; use compose and transfer separately for direct comparison.
- `[touch]` is unchanged. Its gap/miss/drop values are cumulative since boot.
- Section timers now also cover touch, preferences load/write, PMU, and render
  service (including suspended drain). Calls exceeding 250 ms identify themselves.

Check the same SCAN navigation and steady-state interval after flashing. Keep any
remaining stall as a separate finding until the section timer identifies it.

## Validation and limits

- Before: 78 host executables; designer 206/206.
- After: 79 host executables; designer 206/206; pinned engine build successful.
- Both baseline and updated host runs exit successfully but emit the same existing
  UBSan diagnostic in the theme-pack test's invalid `Admit(99)` case at
  `ui/themepack.cpp:436`. This is unchanged, outside the render fix; the new
  sanitizer-backed latency tests report no sanitizer errors.
- New sanitizer-backed tests compare ring lookup pixels to the old formula,
  preserve six static render hashes, exercise 72 sweep phases at both sizes and
  all three themes, and compare partial redraws with full reference frames.
- Existing input retention, navigation cancellation, STOP and passive code paths
  are unchanged. Engine ELF remains free of `esp_now_send` and `esp_now_init`.
- SPI wire time and visible GRAM constraints remain; this is not a panel page flip.
  Device latency, capture throughput, and observed animation FPS require the next
  hardware capture. No claim of hardware acceptance is made here.
