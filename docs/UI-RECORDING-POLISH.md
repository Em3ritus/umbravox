# Recording controls and presentation follow-up

Baseline: `5e9c103`, confirmed usable on the Waveshare by its owner. This follow-up
is host-tested and compile-tested only; it has not been flashed.

## Recording

The native control describes its action: RECORD when idle, STOP with a steady
recording mark when active. It controls SD logging, not passive radio capture.

The WS engine calls `requestStop()`, which does no SD I/O, drops the persisted
recording intent immediately, and exposes SAVING. Repeated toggles during saving
are ignored; the saving scene excludes the record hit. Navigation remains usable.
The logger waits 250 ms before its first drain batch to give feedback a chance to
paint, then writes at most 64 rows per loop pass. This interval is not a guarantee
of display latency. The deadline is three seconds from the request, checked before
each batch. Individual SD calls remain synchronous and can overrun the deadline.

Successful completion returns to RECORD. Unwritten exportable rows produce the
existing diagnostic with their count, plus an amber UNSAVED label under RECORD.
The warning remains until another session starts. Recording starts a new file;
it is not a dedicated retry of the previous file. Capture tables are retained
unless an explicit reset was pending. Reboot retains the existing blocking drain;
legacy/NM callers retain their previous stop semantics.

## Header

GPS, mesh and battery use the same 1x status face and baseline. The wordmark
retains its existing size. GPS WAIT and GPS STALE fit both status cells; the detailed
GPS projection still retains age/fix information for its callers. Themes and theme-selected artwork
are unchanged.

## Presentation

Each composed SRAM strip is compared against the visible PSRAM mirror while it
is staged. Row extents retain the changed area, avoiding a later pixel-by-pixel
comparison between two PSRAM frames. Unchanged rows use `memcmp`; changed rows
locate the first and last changed pixels. MENU keeps its cell presentation regions.
No partially composed frame is presented, and no blackout is introduced.

Fixed row metadata costs 1,280 bytes; no additional framebuffer or heap allocation.
Profiles taller than 320 rows fall back to the existing comparison path. `diff`
metrics include staging comparison/copy time as well as presentation bookkeeping;
`compose` excludes that work. Total render timing continues to include both.

## Verification

- Logging tests cover no I/O on request, 64-row batches, normal completion,
  repeated requests, wraparound, deadline loss reporting, card removal, failed
  writes, a pending table reset, and reboot during saving.
- All recording-state transitions repaint correctly at 480x320 and 320x240.
- Staged presentation is checked against a simulated panel over 100 changing
  frames at each size with both MENU and ordinary regions, invalidation,
  unchanged frames, and a one-pixel update.
- Existing radar geometry, sweep-phase and theme checks remain. Static image
  hashes were refreshed for the intentional header/record-control changes.
- The complete host suite and designer suite pass. The existing invalid-enum
  sanitizer diagnostic in the theme-pack test remains, as on the baseline.
- The pinned `ws-c5-35-engine` build succeeds. No flash or hardware acceptance
  was performed; hardware timing and SD responsiveness remain to be measured.
