# Engine alert takeover (Waveshare 480x320) - design

## Intent
On the CYD an alert takes over the dial with a theme-specific animation (ward, CVLT summoning, muffin supernova / vortex /
comet strike / lock-on) and shows the threat name, detail and confidence. The engine UI (Waveshare 3.5") draws none of this:
an alert only sets the hit key. Goal: the Waveshare shows alerts the way the CYD does, plus a bigger moment for HIGH alerts.
Why: the alert is the one thing the device exists to show; today it is invisible on the new board.

## What the owner decided (this session)
- LOW and MEDIUM confidence: take over where the radar and the spectrum graph are (the hero area), as on the CYD. Counts,
  header and nav stay visible.
- HIGH confidence: take over the WHOLE screen (header and nav included) with a more in-depth animation.
- Tapping the alert clears it (dismiss), at either level.
- Waveshare first; the CYD already has its alerts. Speed matters most: gate on the board's `[eng]` numbers.

## Goals
- The six CYD rites exist on the wide engine panel, chosen exactly as on the CYD (`ui/screen_scan.cpp` `drawAlertIn`: CVLT
  -> summoned, muffin -> `s.set.alertStyle`, packs -> their `rite`, everything else -> the ward).
- Colour is the confidence (LOW witch, MED amber, HIGH red), with the CYD's `safeEdge` contrast rule.
- The threat name resolves out of marks, then detail/why/confidence arrive (CYD `alertNameAndDetail`).
- Animation runs from `alertAge` (ms since this alert first appeared), same as the CYD.

## Non-goals / anti-goals
- No change to the CYD or to 320x240 panels (pinned; the alert path there stays as it is).
- Nothing transmit-side. No new alert sources: the device logic in `device/main.cpp` still decides what is an alert.
- Must not drop the frame rate while no alert is showing, and must never leave stale pixels when an alert ends or is dismissed.

## Decisions (mine; owner to confirm)
- HIGH "more in-depth": the theme's own rite grown to 480x320, a much larger name, and a longer sequence; exact choreography
  is left to the builder, shown to the owner as renders before it goes on the board.
- Tap = dismiss, NOT open details (details stay reachable from OMENS via the alert log). Today a tap on an active alert
  emits the `OpenThreatDetail` hit (matchKey "alert"); that hit is replaced by a dismiss while an alert is showing.
- Dismissal holds until a DIFFERENT alert (threat id or name changes) or until the alert has ended and a new one starts.
  The device re-detects the same threat every few seconds (active while a hit is < 6 s old), so a plain "clear" would
  reappear at once. The dismissed flag lives next to the alert-age logic in `device/main.cpp` (~line 1860).
- LOW/MED takeover rectangle = the hero area (radar + spectrum column) on the wide panel.

## Parts (suggested order, each ends with tests + board numbers)
1. Scene + plumbing: alert fields on `scanr::ScanScene` (active, threat, conf, name, why, age, style/rite, dismissed),
   filled in `ui/project.h`; dismiss hit and state; dirty-region rule (alert on/off and each animation frame).
2. LOW/MED takeover for all six rites in the hero rectangle, integer maths only (no FPU; use `scanr::isin/icos`).
3. HIGH full-screen takeover (all six rites), including the nav/header bands in the dirty regions.
4. Tap-to-dismiss end to end, and the hold-until-different rule.

## Constraints and traps
- C5 has no FPU: no float/double per frame. Strips are 16 rows; every draw must clip to the strip and column window.
- Dirty regions: max 8, `Engine::commit` in `ui/scan_present.h`; a region list must cover everything that changes
  (the sweep-style work found contact halos and diagonal corners missed). A full-screen frame is the simple, correct HIGH case.
- Pinned items that move with layout/hit changes: `sim/native-hit-contract.json` -> `python3 sim/gen_oracle.py ...`,
  `sim/test_layout_engine.cpp`, `sim/test_scan_latency.cpp` goldens (alert off must leave them unchanged).
- `alertAge` is a uint16 capped at 60000 ms; the CYD rites assume that.
- Reference renders: `sim/umbravox out.png --theme N --alert STYLE --frame F --scale 2`
  (sheet: `private/renders/themes-ws/cyd-alert-rites.png`).

## Left to the builder
Rite choreography at 480x320, the big-name font scaling, exact HIGH sequence length, how the hero takeover animates in/out,
test fixtures. Tests first, show RED; engine tests drive the real Engine + panel mirror (see `sim/test_sweep_styles.cpp`).
