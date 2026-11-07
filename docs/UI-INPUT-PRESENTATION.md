# Native engine input and presentation

Scope: `ws-c5-35-engine`, based on `fa8fb43`. No navigation backlight blanking.
No hardware acceptance is claimed by the host checks or firmware build.

## Presentation

Two RGB565 frames in PSRAM (614,400 bytes total at 480x320) replace direct
visible strip composition on this target. The existing engines still compose
bounded slices into a stable scene. The previous panel contents remain visible
until the complete update is ready. Exact front/back comparison then sends only
changed rectangles: status, body, navigation; MENU's body is partitioned into
complete cells with their surrounding margins. Unchanged chrome is not sent.
Each rectangle is fully composed before its first SPI write; no GPS/application
pass occurs between its rows. An internal padded row protects against the GFX
C5 writer's pair reads for odd-width rectangles. Theme selection is unchanged.

This removes the multi-loop composition curtain, not the physical panel's scanout
or SPI update progression. A worst-case full transfer still needs at least
61.44 ms at 40 MHz, plus overhead and preemption. No page flip or strictly atomic
full-screen change is promised. Allocation failure is logged and falls back to
the old progressive strip path rather than crashing. External animation/wake
invalidates the panel mirror before repaint.

## Input and shared bus

A priority-3 FreeRTOS task samples on a 20 ms schedule independently of the main
loop and renderer. The sensor's per-read controller reopen and tri-state release
behavior are retained. Every runtime Wire client (touch, GPS, PMU, backlight)
owns one recursive, priority-inheriting mutex for its entire transaction. The
task starts only after setup-time bus initialization. No I2C runs in an ISR.

Press edges retain coordinates and a screen-generation token in an eight-event
queue, even if the finger releases before the loop resumes. Navigation cancels
old-generation events without rearming a held finger. Dispatch resolves and owns
the target slot/action/payload/record key while entry is gated, then validates
that target before delivery. A missing/replaced target is cancelled, never
retargeted to whatever now occupies the same coordinates. Wake suppresses the
wake press; post-render navigation establishes entry in that same pass.

20 ms is a scheduling deadline, NOT a proven hard sensor sampling bound. A task
cannot interrupt an atomic GPS transaction. Engine GPS reads use 128 bytes per
125 ms (same nominal 1 KiB/s capacity as 512/500), with a 40 ms aggregate operation
budget; each bus operation receives only the remaining allowance. There is still
driver restart/scheduling overhead. Other bus clients use a 5 ms per-operation
timeout under the guard. PMU logging and GPS parsing occur after bus release.

The periodic `[touch]` diagnostic reports maximum observed sample gap, late
sampling cycles, and queue overflow. An unseen tap entirely within a bus/driver
stall cannot be reconstructed. Hardware acceptance requires checking these
numbers alongside deliberate taps; first-tap reliability is not established by
pure queue tests. If task creation fails, a logged synchronous polling fallback
keeps input usable but does not meet the periodic sampling target.

## Passive build

`radio_policy.h` selects passive-only behavior for WS + native engine. All four
ESP-NOW send sites in the linked core sources are compiled out; mesh initialization
is bypassed. WiFi initializes directly in NULL mode for promiscuous reception,
without starting a station/AP; BLE remains passive scanning. Mesh enrollment and
coordination are unavailable in this target. Other build environments retain
their original radio policy. The engine ELF was checked for absence of both
`esp_now_send` and `esp_now_init`. This is source/link evidence, not an RF survey.

## Validation

- Baseline `make -C sim test`: 77 executables passed.
- Updated `make -C sim test`: 78 executables passed, including ASan/UBSan input and
  presentation tests at 480x320 and 320x240.
- Designer: 206/206 before and after, Emscripten 6.0.11; no skipped tests.
- Pinned `~/.venvs/pio313/bin/pio run -e ws-c5-35-engine`: passed.
- New checks cover completed taps, held-finger/navigation cancellation, generation
  crossing, overflow, owned target keys, stale-target rejection, deadline wrap and
  overrun, aggregate bus budget, invisible composition, exact fake-panel equality,
  unchanged-frame no-op, unchanged chrome, and complete MENU-cell updates.

No flash, push, or tag is part of this change. On glass, confirm navigation has no
blackout, inspect the remaining fast transfer progression, and exercise single
press/release taps during entry with GPS present and absent. RF capture performance
and physical I2C timing remain hardware measurements. The separate boot-stall
investigation is unchanged.
