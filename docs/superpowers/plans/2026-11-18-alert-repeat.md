# Alert repeats Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** the same device stops re-raising the alert takeover; trackers that travel with you re-alert every 10 min
and drones every 20 min with how long they have been around; a new ALERTS settings tab holds ALERT REPEAT.

**Architecture:** a pure, host-tested cooldown table (`ui/alert_repeat.h`) decides, each loop, whether the picked
alert is shown and which "episode" it belongs to. `device/main.cpp` asks it before raising `s.alertActive`, so the
card, the tone and the screen wake (all keyed on a new episode) stay quiet together, while the OMENS history still
gets every sighting. blescan re-raises a follow alert every 10 min. Settings gains `alertRepeat` and a 5th tab.

**Tech Stack:** C++17 headers shared by firmware and host (`sim/`), Arduino/ESP-IDF firmware (PlatformIO), NVS prefs.

**Spec:** `docs/superpowers/specs/2026-11-18-alert-repeat-design.md`

## Global Constraints

- ALERT REPEAT choices 1 min / 10 min / 1 h / until reboot (`alertRepeat` 0/1/2/3), default 10 min (1). NVS key
  `alertrep`; an older store without it reads 10 min.
- Tracker following (`TH_FOLLOW`): re-alert at most every 10 min (600000 ms) while still with you. Drone
  (`TH_DRONE`): at most every 20 min (1200000 ms) while still seen. Neither follows ALERT REPEAT.
- An episode ends after 6000 ms with no sighting of that device (today's `ALERT_MS`).
- A higher confidence than the device last alerted at always shows.
- The OMENS history (`alertlog::push`) and serial/elog lines are unchanged.
- TEST (console, OMENS button) bypasses the table.
- Key: threat + `AlertRec.mac` when `address != AlertAddress::Missing`, else threat + name.
- Table: 16 slots, oldest-touched evicted, in internal RAM (it is small); boot heap stays above 55 KB.
- Time arithmetic: signed differences `(int32_t)(now - then)` everywhere.
- Settings tabs on screen: SCANNING | RECORDING | ALERTS | BRIDGE | NODES. ALERTS holds ALERT LEVEL, ALERT SOUND,
  ALERT REPEAT and one empty slot; RECORDING keeps AUTO RECORD and RAW PCAP.
- Commits: trailer `Co-Authored-By: Claude <noreply@anthropic.com>` only.

## Review Focus

1. **millis() wrap:** a cooldown that straddles the 49.7-day wrap must still end on time (Task 1 test
   `cooldown_survives_wrap`).
2. **Two alerts at once:** the newest alert is quiet but an older one (the other radio) is due: the due one shows
   instead of nothing (Task 4: pick among both candidates; Task 1 test `two_devices_independent`).
3. **Table full:** a 17th device evicts the oldest-touched; the evicted device alerts again as new, never crashes
   (Task 1 `evicts_oldest`).
4. **A drone that never leaves:** with no 6 s gap there is no new episode, yet it must re-alert at 20 min (Task 1
   `drone_present_realerts_at_20min`).
5. **Re-alert of the same device must ring and wake:** today's tone/wake fire on a change of threat/name; a re-alert
   keeps both, so it must be keyed on the episode (Task 4 step "episode drives tone/wake").

---

### Task 1: The cooldown table

**Files:**
- Create: `ui/alert_repeat.h`
- Create: `sim/test_alert_repeat.cpp`; Modify: `sim/Makefile` (add `test_alert_repeat` to `TESTS` and a rule like
  `test_alert_gate`'s)

**Interfaces:**
- Produces (namespace `alertrepeat`):
  - `constexpr uint32_t EPISODE_GAP_MS = 6000, FOLLOW_MS = 600000, DRONE_MS = 1200000; constexpr int SLOTS = 16;`
  - `uint32_t repeatMs(uint16_t setting)` → 60000, 600000, 3600000, and `UINT32_MAX` for 3 (until reboot); values
    above 3 are treated as 1.
  - `struct Verdict { bool show; uint32_t episode; uint32_t firstMs; bool repeat; };` — `episode` is a number that
    changes exactly when a new shown episode starts; `firstMs` is when this device first alerted in its current
    stay; `repeat` is true for a shown episode that is not the device's first (a re-alert).
  - `struct Table { Verdict see(const ui::AlertRec& a, uint32_t nowMs, uint16_t repeatSetting); void dismissCurrent(uint32_t nowMs); void clear(); };`
  - `bool followDue(uint32_t lastFollowAlertMs, uint32_t nowMs)` → `(int32_t)(now - last) >= FOLLOW_MS`.
  - `void reason(char* out, int n, uint8_t threat, uint32_t minutes)` → `"STILL HERE %u MIN"` for TH_DRONE,
    `"WITH YOU %u MIN"` for TH_FOLLOW, fits `ui::State::alertWhy` (20 bytes incl. NUL; clamp minutes to 999).

Rules for `see()` (call once per loop for each active candidate):
- Unknown device: new entry, show, `repeat=false`, `firstMs=now`.
- Known device, gap since its last `see()` > `EPISODE_GAP_MS`: a new episode. Shows if conf > the entry's last shown
  conf, or `(int32_t)(now - lastShownMs) >= cooldown(threat)` (cooldown: FOLLOW_MS, DRONE_MS, else
  `repeatMs(setting)`; `UINT32_MAX` never elapses). If the gap was longer than `cooldown(threat)` (and the cooldown
  is not "until reboot"), `firstMs` resets to now.
- Known device, same episode (gap <= 6 s): keeps its decision, except: conf rises above the shown conf → a new shown
  episode; TH_DRONE with `(int32_t)(now - lastShownMs) >= DRONE_MS` → a new shown episode (`repeat=true`).
- `dismissCurrent(now)`: the entry most recently returned with `show=true` stops showing for the rest of its
  episode and its `lastShownMs = now` (the cooldown starts at the dismiss).

- [ ] **Step 1: Write the failing tests** in `sim/test_alert_repeat.cpp` (same `expect()` style as
  `sim/test_alert_gate.cpp`). Helper `rec(threat, conf, macLast, name)` builds an `AlertRec` with
  `address = BlePublic` (or `Missing` when `macLast < 0`). Tests, each with fixed `now` values:
  - `first_sighting_shows`: see(camera) at t=0 → show, repeat false.
  - `same_episode_keeps_showing`: t=0, 2000, 5000 → all show, same `episode`.
  - `fixed_device_quiet_inside_cooldown`: show at 0; absent; see at 0+7000 → show false; at 600000+7000 after a
    gap → show true (setting 1 = 10 min); with setting 0 back at 60000+.
  - `until_reboot_never_again`: setting 3, back after 10 h → show false; conf rises LOW→MED → show true.
  - `conf_rise_breaks_through`: MED shown at 0; HIGH at 8000 → show, new episode; HIGH again at 16000 → quiet.
  - `follow_every_10_min`: TH_FOLLOW at 0 show; at 6 min (after a gap) quiet; at 10 min show, repeat true.
  - `follow_ignores_setting`: setting 3; TH_FOLLOW back at 10 min → show.
  - `drone_present_realerts_at_20min`: TH_DRONE seen every 3 s from 0 to 20 min: show throughout the first
    episode, then a new episode (`episode` changes, repeat true) at >= 1200000; `firstMs == 0` still.
  - `drone_back_after_long_absence_is_fresh`: drone at 0, absent 30 min, back → show, repeat false, firstMs = now.
  - `dismiss_starts_cooldown`: camera shown at 0, `dismissCurrent(3000)`, see at 4000 → show false (same episode);
    back after a gap at 3000+600000+1 → show.
  - `key_is_address_not_name`: TH_FOLLOW with the same mac and names "AA 0.5KM" then "AA 1.2KM" → one entry.
  - `name_key_without_address`: two `Missing`-address alerts with different names → independent.
  - `two_devices_independent`: camera A quiet in cooldown does not stop camera B showing.
  - `evicts_oldest`: 17 devices; the first (oldest-touched) seen again after a gap → show, repeat false.
  - `cooldown_survives_wrap`: show at 0xFFFFFFFF - 1000, back at 600000 - 1000 (wrapped) → show.
  - `reason_text`: `reason(b, 20, TH_DRONE, 40)` == "STILL HERE 40 MIN"; TH_FOLLOW 50 → "WITH YOU 50 MIN";
    minutes 5000 → "STILL HERE 999 MIN"; output always NUL-terminated within n.
  - `follow_due`: `followDue(0, 599999)` false, `followDue(0, 600000)` true, across wrap true.
- [ ] **Step 2: Run** `make -C sim test_alert_repeat && ./sim/test_alert_repeat` → fails to compile (no header).
- [ ] **Step 3: Implement** `ui/alert_repeat.h` (header-only, `#include "signatures.h"`, no allocation, no statics).
  Entry: `{bool used; uint8_t threat; uint8_t mac[6]; bool byName; char name[ui::ALERT_NAME]; uint8_t shownConf;
  bool showing; uint32_t lastSeenMs, lastShownMs, firstMs, episode;}`; a table-wide episode counter.
- [ ] **Step 4: Run** the same command → `all passed`. Also `make -C sim test` → no new failures.
- [ ] **Step 5: Commit** `feat(alerts): a cooldown table for repeated alerts (host-tested)`.

### Task 2: ALERT REPEAT setting and the ALERTS tab

**Files:**
- Modify: `ui/model.h` (`Settings`: append `uint16_t alertRepeat = 1;` after `sound`, with a comment: 0 1 min,
  1 10 min, 2 1 h, 3 until reboot), `ui/nav.cpp` (`clampSettings`: > 3 → 1; `applyKnobStep`: new knob 15 steps
  0..3 like ALERT SOUND, no wrap), `device/prefs.cpp` (save/load `alertrep`, and the settings dump line),
  `ui/settings_controls.h` (5 tabs, card table, editor 16), and any doc that lists the settings cards (`grep -rn
  "ALERT SOUND" docs/ README.md`).
- Test: `sim/test_settings.cpp`, `sim/test_settings_controls.cpp` (and any test that indexes BRIDGE/NODES by tab
  2/3; they become 3/4).

**Interfaces:**
- Produces: `ui::Settings::alertRepeat` (Task 4 reads it); knob 15; editor id 16 (payload 26).
- `settings_controls.h`: replace `id = tab*4 + i` with `inline int cardId(int tab, int i)` from the table
  `{{0,1,2,3},{4,5,-1,-1},{6,7,16,-1},{8,9,10,11},{12,13,14,15}}` (-1 = empty slot: no hit, drawn as a plain
  panel). Tabs `{"SCANNING","RECORDING","ALERTS","BRIDGE","NODES"}`, hit names add `"settings-alerts"`;
  `tabRect(w,i)` divides by 5; tab payloads 0..4. `validEditor` accepts 16; `knob(16) == 15`; `title(16)` "ALERT
  REPEAT", `hint(16)` "Quiet time before the same device alerts again", `value` "1 MIN" / "10 MIN" / "1 HOUR" /
  "UNTIL REBOOT"; DEFAULT resets it; `sameSettings` compares it.

- [ ] **Step 1: Write the failing tests:** in `test_settings.cpp`, default `alertRepeat == 1`, clamp 9 → 1, the
  existing no-padding check still passes with the new field; knob 15 steps 1→2→3→3 and 1→0→0. In
  `test_settings_controls.cpp`: 5 tab hits with `"settings-alerts"` third; on tab 2 the cards are ALERT LEVEL
  (payload 16), ALERT SOUND (17), ALERT REPEAT (26) and no 4th hit; tab 1 has exactly 2 card hits; opening 26 then
  plus moves `alertRepeat` 1→2 and `value` shows "1 HOUR"; DEFAULT returns 10 MIN; BRIDGE is now tab 3 and NODES
  tab 4 with their old card ids.
- [ ] **Step 2: Run** `make -C sim test_settings test_settings_controls && ./sim/test_settings && ./sim/test_settings_controls` → FAIL.
- [ ] **Step 3: Implement** the model, nav, settings_controls and prefs changes above. On the CYD (320 px) each tab
  is 64 px: the labels fit at scale 1 ("RECORDING" is 54 px).
- [ ] **Step 4: Run** Step 2's command → pass; `make -C sim test` → no new failures; `make -C sim regress` →
  report which images changed (only Settings screens may change; SCAN must say unchanged).
- [ ] **Step 5: Commit** `feat(settings): ALERTS tab with ALERT REPEAT (1 min / 10 min / 1 h / until reboot)`.

### Task 3: Follow alerts repeat every 10 minutes

**Files:**
- Modify: `ui/signatures.h` (`AlertRec`: add `uint16_t minutes = 0;` time together, for TH_FOLLOW),
  `device/blescan.cpp` (`tick()`, the `Extra` struct).

**Interfaces:**
- Consumes: `alertrepeat::followDue` (Task 1).
- Produces: a fresh `alert_` (new `atMs`) for a following tracker on its first detection and then whenever
  `followDue(e.followAlertMs, now)`; `alert_.minutes = (now - e.firstMs) / 60000` (clamped to 999); the name keeps
  today's address + distance text. `alert_.why` stays "TRAVELLED WITH YOU" (Task 4 swaps in the minutes text).

- [ ] **Step 1:** Replace `bool following` in `Extra` with `uint32_t followAlertMs` plus `bool following`; raise
  when `!following || followDue(followAlertMs, nowMs)`; set both; keep the serial line (it now prints on each
  re-alert, which is wanted).
- [ ] **Step 2: Build** `~/.platformio/penv/bin/pio run -e nm-cyd-c5 -e ws-c5-35-engine` → SUCCESS. (blescan is
  device-only; its cadence rule is `followDue`, tested in Task 1.)
- [ ] **Step 3: Commit** `feat(blescan): a tracker that keeps travelling with you re-alerts every 10 minutes`.

### Task 4: Wire the table into the alert pick

**Files:**
- Modify: `device/main.cpp` (the alert pick ~line 1910 and the episode/tone block ~1936), `device/engine_adapter.cpp`
  (the `AlertDismiss` case ~591), `docs/design-notes.md` (a short "Alert repeats" section), the user-facing doc that
  describes alerts (`grep -rln "TRAVELLED WITH YOU\|ALERT LEVEL" docs/ README.md`).

**Interfaces:**
- Consumes: `alertrepeat::Table`, `Verdict`, `reason` (Task 1); `Settings::alertRepeat` (Task 2);
  `AlertRec::minutes` (Task 3).
- Produces: `void alertRepeatDismiss()` in `device/main.cpp` (declared where engine_adapter can call it, next to
  the existing cross-file declarations), calling `table.dismissCurrent(millis())`.

- [ ] **Step 1: Pick among both candidates.** Collect the Wi-Fi and BLE candidates that pass `alertMin` and the
  6 s window (as today), newest first. `historyAlert` stays the newest (the history is unchanged). Call
  `table.see()` for each; the first with `show` becomes the takeover. TEST still bypasses (no `see()` call).
- [ ] **Step 2: Reason text.** If the shown verdict has `repeat` and the threat is TH_DRONE, `reason(s.alertWhy,
  sizeof s.alertWhy, TH_DRONE, (now - v.firstMs) / 60000)`; TH_FOLLOW with `minutes > 0` uses `reason(...,
  TH_FOLLOW, best.minutes)` on every follow alert. Otherwise copy `best.why` as today.
- [ ] **Step 3: Episode drives tone/wake.** In the block at ~1936, "a new alert" becomes "the verdict's `episode`
  differs from the last one shown" (keep a `static uint32_t lastEpisode`; the TEST path keeps today's
  threat/name comparison). `startedAt` resets on a new episode, so the card animates again.
- [ ] **Step 4: Dismiss.** In `engine_adapter.cpp`'s `AlertDismiss` case, after `alertGate_.dismiss(...)`, call
  `alertRepeatDismiss()`. The gate hides a dismissed alert until it ends, but a drone that never leaves never ends:
  add `void alertGateNewEpisode()` in `engine_adapter.cpp` (sets `alertGate_.dismissed = false`) and call it from
  Step 3 whenever a new episode starts, so the 20-min re-alert shows after an earlier dismiss.
- [ ] **Step 5: Build** all four envs: `~/.platformio/penv/bin/pio run -e nm-cyd-c5 -e nm-c5-node -e
  nm-c5-coordinator -e ws-c5-35-engine` → SUCCESS; report `ws-c5-35-engine` `.dram0.bss` (cap 94720: the table must
  not push it over; if it does, allocate the table in PSRAM at boot like `flogState()`).
- [ ] **Step 6: Hardware gate** (owner present, boards connected): flash `nm-cyd-c5` and run
  `~/.platformio/penv/bin/python tools/bootprobe.py` (12/12, heap > 55 KB, draw ~12 ms); Waveshare with
  `esptool --no-stub` and the same probe. Console `TEST` still shows its sequence. Never claim a check not run.
- [ ] **Step 7: Docs**, then **Commit** `feat(alerts): fixed things alert once per ALERT REPEAT; trackers and
  drones on their own clocks`.
