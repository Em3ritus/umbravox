# Alert repeats: once for fixed things, on a clock for things that follow you (design)

Owner request, 2026-11-18. Short design (a small-to-medium change).

## Why
The same device alerts over and over. An alert stays up while its last hit is under 6 s old; when the device drops
out for 6 s and is heard again, the whole alert fires again (card, rite, tone, screen wake). Driving past a body cam
or a Flock camera you are already aware of it after the first time. A tracker or a drone that keeps turning up is
different: the repeats are the signal, and you want to know how long it has been with you, but not constantly.

## Goals
- Fixed things (cameras, Flock/ALPR, body cams, glasses, skimmers, Raven, pentest kit, deauth/rogue, a plain
  "tracker nearby" sighting): alert once, then quiet for that device for the **ALERT REPEAT** time.
- A tracker travelling with you (`TH_FOLLOW`, today's GPS rule: 8 min and 500 m): the first follow alert always gets
  through; while it is still with you it re-alerts at most every **10 min**, each time with the updated time and
  distance (`WITH YOU 50 MIN 4.2 KM` or similar, fitting the 20-byte name/why fields).
- A drone (`TH_DRONE`, Remote ID): re-alerts at most every **20 min** while the same drone is still being seen;
  the reason line says how long it has been around (`STILL HERE 40 MIN`). No GPS needed.
- A rise in confidence for the same device (LOW to MED, MED to HIGH) always gets through.
- Nothing is lost: the OMENS history keeps logging every sighting exactly as today (it already merges repeats within
  60 s); serial/elog lines unchanged.

## Non-goals / anti-goals
- No time-only "following" rule without GPS: that comes with the GPS work (owner, 2026-11-18).
- Never suppress at the source (the scanners): that would drop repeats from the history and logs.
- The tracker and drone clocks are fixed and do not follow ALERT REPEAT, so setting ALERT REPEAT to "until reboot"
  can never silence a tracker that is following you.
- No persistence of the cooldown table across reboots.

## Approaches (owner picked A)
- **A. Extend the alert gate into a small cooldown table (picked).** A host-tested header in `ui/` (grown from
  `ui/alert_gate.h`'s idea, keyed on threat + device address) remembers the last 16 alerted devices: when each last
  alerted, at what confidence, and when it first alerted. One place, no hardware in it, fully testable.
- B. Cooldown inline in `device/main.cpp` where the alert is picked: board-only, no host tests without extracting it,
  which ends up as A.
- C. Stop repeats in the scanners: cheapest, but loses history and logs (anti-goal).

## Decided
- ALERT REPEAT: a Settings row, choices 1 min / 10 min / 1 h / until reboot, default 10 min, saved with the other
  settings (`device/prefs.cpp`, NVS) and shown with the alert settings (`ui/settings_controls.h`).
- Key: threat + device address (`AlertRec.mac` + `address`); the displayed name only when there is no address (a
  follow alert's name changes as the distance grows, so the name is not a stable key).
- An "episode" starts when a device's alert rises after being absent (today's 6 s rule). The cooldown is judged when
  an episode starts; inside the cooldown the whole episode stays off screen. A tracker/drone re-alert is a new
  episode even though the device never left.
- Tap-dismiss (`ui/alert_gate.h`) keeps working and also starts that device's cooldown.
- The TEST sequence (console TEST, OMENS test button) bypasses the table.

## Left to the builder
- Where the decision is applied: the alert pick in `device/main.cpp` (~line 1910), so the card, the tone and the
  screen wake (all keyed on a new alert, ~line 1940) stay quiet together, while `historyAlert` still goes to the
  history. Today the tone/wake fire on a change of threat or name, so a same-device re-alert must be made to count as
  new (for example an episode counter), or it will rise silently.
- How blescan raises the 10-min follow re-alerts (today `e.following` makes it fire once) and how the drone duration
  is tracked (the table's first-alert time is enough).
- The table's eviction (oldest first is fine), signed time arithmetic (`(int32_t)(now - then)`), and its RAM: 16
  small entries in internal RAM is fine, but check the boot heap stays above 55 KB.
- Text for the reason/name lines within the existing field sizes.
- Tests first: the table (fixed device quiet inside the cooldown and back after it, "until reboot", confidence rise,
  follow every 10 min, drone every 20 min, eviction, wrap-around of millis), the settings row and its save/load, and
  a hardware gate (bootprobe) after the firmware change.
