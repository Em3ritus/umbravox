# Field Fixes Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development.

**Goal:** Fix the problems the user hit in the field (logs in `private/logs/`, analysed 2026-10-27): SD logging stalls and freezes as the AP table fills, no plan for 10,000 APs, footer wording, idle screen behaviour, and investigate BLE heap use, node dropouts and GPS lock.

**Architecture:** Firmware in `device/` plus shared drawing in `ui/` (host-tested via `make -C sim test`; UMBRAVOX renders must stay byte-identical: `make -C sim regress`). Firmware builds: `~/.platformio/penv/bin/pio run -e nm-cyd-c5 -e nm-c5-node -e nm-c5-coordinator`.

**Spec:** none; the user's list is the spec (memory `technomancer-bug-queue`). Decisions marked *my call* are the controller's and are reported to the user.

## Global Constraints

- UMBRAVOX is passive/receive-only: add nothing that transmits.
- Nothing can be flashed or run on hardware in this plan: never claim a hardware result. Host tests and builds only.
- `Settings` has one spare byte (`reserved`, ui/model.h); changing its size breaks the layout test in `sim/test_settings.cpp`.
- Evidence: `private/logs/` in the MAIN checkout `/home/mummydust/Documents/umbravox/technomancer/private/logs/` (read-only): `perf1.csv` (columns: t_s,dark,fps,draw_avg,draw_max,blit_avg,blit_max,gap_max,sd_max,heap_free,heap_min,heap_blk,psram_free,stack_free,wifi_fps,ble_aps,wifi_n,ble_n,tbl_pct,nodes,rep_s,loss_pct,camp,fix,theme,sweep; 10 s rows; a new boot restarts t_s), `diag.log`, two WiGLE CSVs `WD-*.CSV`. Findings so far: the logger rewrites the WHOLE CSV every 30 s (`device/sdlog.cpp writeAtomic`), so write stalls grow with the table (1.1 s at 2.5k APs, 5.4 s at 8k, up to 64 s later); at 8192 unique APs the dedup table (`device/capture.cpp TABLE`) is full and new APs are silently dropped; with BLE on, heap_min fell to ~11 KB (60 KB with BLE off); every boot in diag.log is reset=POWERON (the user pulled power on a frozen device; no panic captured).

## Review Focus

- A card removed/full/slow during an append must never freeze the UI for more than a few hundred ms and never corrupt the existing file.
- A reboot mid-session must never overwrite or truncate an earlier session file.
- The 10,000-AP rollover must not lose the rows already written, and must not stop capture or the dial.

---

### Task 1: Field failure analysis (read-only investigation)

**Files:** none changed. Write `docs/field-analysis-2026-10.md`-style findings to the report path only (the controller commits nothing from this task).

Investigate, from code and the logs, and report with file:line evidence and a confidence level for each; propose a concrete fix (diff-level description) for each cause you find, ranked by value:
1. **Hard crashes / freezes**: what blocks the loop for 5–64 s (confirm the SD rewrite path, and whether a task watchdog should have fired but is disabled/fed), what could *crash* (heap exhaustion with BLE on: largest free block fell to 15 KB; unchecked allocations in `blescan.cpp`, `capture.cpp`, `logging.cpp`, `sdlog.cpp`; stack use; `uint16_t order_` and other fixed-width indices if TABLE grows; the `ble_aps` column showing 429410830 in some rows: an unsigned underflow in a counter — find it). Does anything allow a crash near a total of ~9,120 records (8192 Wi-Fi + BLE rows)?
2. **BLE heap cost**: where do ~45 KB go when BLE scanning is on (NimBLE config in platformio.ini/sdkconfig, `blescan.cpp` tables, host/controller buffers); what is reducible without breaking the Wi-Fi/BLE coexistence.
3. **Nodes dropping out**: use the `nodes`, `rep_s`, `loss_pct`, `camp` columns and `device/meshcore.cpp`/`mesh.h`/`chanplan.h`/`rehome.h`: why would a node (Xiao ESP32-C5 over ESP-NOW) drop from the table (timeouts vs channel mismatch vs the Core camping on channel 1)? Dropouts did NOT correlate with SD stalls (2 of 151).
4. **GPS lock issues**: `fix` flips 153 times; read `ui/gnss*`/`device/gpstrack.cpp`/`gps_probe.cpp`/main's NMEA handling: how fix validity, hold-over and timeouts work, and why the flag might flap (e.g. a short staleness window while the loop is blocked, RMC status A/V, HDOP/satellite thresholds).
Rank causes by how well the logs support them. Do not change any source.

---

### Task 2: Footer: GPS label and start/stop control

**Files:** `ui/screen_scan.cpp` (+ any golden/regress data it changes: regenerate nothing silently; see below), tests in `sim/`.

Requirements (user's words: "where the GPS fix gives an identifier to just say fixed or no gps"; "remove timer, keep just the Record button and rename to start/stop"; "Timer stopped at 99:59"):
- GPS: with a fix the footer text is `FIXED` (no satellite count); without it `NO GPS`. Keep the existing rune/colour logic.
- Record control: no elapsed timer at all (remove the mm:ss code and the 99:59 saturation). The button reads `START` when idle (hollow dot) and `STOP` when recording (filled dot); `NO CARD` stays. Keep the framed-button look and the touch area.
- UMBRAVOX regress baselines will change ONLY in the footer region: update the baselines through the repo's documented mechanism (see `sim/Makefile` regress/baseline targets and README of tests) and state in the report exactly which baseline files changed and that the diff is limited to the footer pixel rows. Add/adjust host tests so the labels are asserted (render the footer and check text or a pixel-level golden).
- `logSecs` may stay in `State` if other code uses it; remove it only if unused (grep).

---

### Task 3: Append-only logging and 10,000-AP rollover

**Files:** `device/logging.cpp/.h`, `device/sdlog.cpp/.h`, `device/capture.cpp/.h`, `device/blescan.cpp/.h`, `device/main.cpp` (writeWigle and callers), tests under `sim/` for the pure parts.

Requirements:
- **Append, not rewrite.** Each flush (keep `FLUSH_MS` 30000 or make it shorter if append is cheap) writes only rows not yet written: keep a cursor per source (the `order_` first-seen sequence for Wi-Fi; the equivalent for BLE) and append those rows to the session file; write the WiGLE header once when the file is created. Rows the exporters skip (e.g. no location) must be handled consistently (the existing `exportRows` returns `skipped`): a row not yet exportable (waiting for a fix) must be retried on a later flush, not lost or duplicated — read how `exportRows` decides and preserve the same CSV content for the same data. Keep the atomic/pulled-power safety story honest in the report: an append can leave a partial last line after power loss; say how the next boot handles it (e.g. ignore, or trim a trailing partial line when reopening) and test that logic.
- **Bounded stall.** The per-flush stall must stay roughly constant (proportional to new rows), not grow with file size. Use the existing stall metrics (`sdlog::noteStall`, perf log `sd_max`). Cap rows per flush if needed so one flush never blocks long (e.g. ≤ 200 rows, remainder next tick).
- **Never overwrite an earlier session**: the dateless counter logic (`sdlog::maxIndex`) and naming stay.
- **10,000-AP rollover (user: "we also need a solution for when it hits 10,000").** *My call:* raise the Wi-Fi dedup table (`TABLE`) to 16384 slots (PSRAM; check the `order_` index width and every place that assumes 8192) and, when unique Wi-Fi APs reach 10,000, roll over: flush everything, close the session file, start a NEW session file (new name via the existing naming), clear the dedup table and counters so capture continues and rows are logged again in the new file, keep the dial/contacts/alert state usable (do not blank the display mid-frame; the recent-blips ring may keep its entries). The rollover threshold is a named constant with a host-tested decision function. Log it via `elog` ("log: rollover at N APs, new file X"). BLE table: apply the same rule to its own cap if it has one (read `blescan.cpp`); state what happens at its cap.
- Remove or repurpose `writeAtomic` only if nothing else uses it (the NOTE: other files may use it: grep); keep a safe path for the other callers.
- Host tests for all pure logic (cursor/flush batching, partial-line trim, rollover decision, name collision). Fake the file with an in-memory sink. Show RED first.
- Builds: all three pio envs must build. Report flash/RAM before and after (`pio run` prints it), and the PSRAM delta from the larger table.
- State plainly in the report what could NOT be verified without hardware (SD timing, FAT behaviour), and give the user a short hardware test procedure.

---

### Task 4: Idle screen: off or low-power animation

**Files:** `ui/model.h` (Settings: use the single `reserved` byte as `idleMode`), `ui/nav.cpp`/settings screen, `device/main.cpp` (idle logic around the `dimmed`/`pwmNow` code), `device/prefs.cpp`, tests.

Requirements (user: "Screen time out to either low power animation or turn off"): the existing idle timeout (`IDLE_S` options, currently dims to bright/8 and suspends drawing) gains a MODE setting with three values: `DIM` (today's behaviour, default so nothing changes for existing users), `OFF` (backlight fully off, display/blit suspended, touch wakes and the wake touch is swallowed as today), `ANIM` (a tiny low-power animation: *my call:* a slow single-pixel-row scanning line or breathing dot drawn at a low frame rate (≤ 2 fps) at the dimmed brightness; it must cost far less than the normal 13 ms/frame, and must not delay capture). Add the setting row to the settings screen (the `SET_ROWS` count and the reset row move accordingly, update tests), persist it in NVS next to the other settings, and keep Settings 12 bytes (consume `reserved`, rename it, and satisfy the layout static_asserts). Host tests for the setting round trip and clamping; render tests for the settings screen. Do not change anything for the default mode.

---

### Task 5: Apply the Task 1 fixes (receive-only, low risk)

Source: `.superpowers/sdd/2026-10-27-field-fixes/task-1-report.md` (read its sections for each item: file:line evidence, expected effects, host tests). Implement, each with a failing host test first where the report says a pure helper is testable:
1. `cap::insert` table-full backstop: no overwrite of another AP's slot, no evil-twin pass, bounded probe, count + `elog` (rate-limited). Pure helper + host test. (Task 3 may already have part of this: read the code first; do not duplicate.)
2. BLE scan recycling in `blescan::tick`: periodically (or when the library's result count exceeds 64) stop/restart the scan to clear the Arduino BLEScan result map; restart when it has stopped; pure `shouldRecycle(...)` decision + host test; elog on restart.
3. perflog counter deltas reset-safe (the 429410830 underflow after RESET COUNTERS). Host test in `sim/test_perflog.cpp`.
4. Mesh node expiry: `NODE_TIMEOUT_MS` to 60-90 s (choose 75 s, constant), wrap-safe compare, and a camp hold-over (`CAMP_HOLD_MS` 120 s) after the last node so the Core does not drop off channel 1 as soon as the last node expires. Pure helpers + host tests (expiry across the millis wrap; hold window). No new transmissions.
5. GPS: `gpsPort.setRxBufferSize(2048)` before `begin`; debounce a single quality-0 GGA (keep the fix valid until `FRESH_MS` expires, or 3 consecutive quality-0s); host tests in `sim/test_gnss.cpp` exactly as the report describes; add `gps_bad`/`gps_q0` perflog columns only if `sim/test_perflog.cpp` and `tools/perf_report.py` are updated consistently.
6. elog diagnostics and heap-warning re-arm hysteresis (as the report describes).
7. Exporter rows via `snprintf` + `write` only if Task 3 has not already removed the per-row malloc; byte-identical output, host test.
NOT in scope (report to the user, do not implement): node keepalive transmissions, the loop watchdog, the evil-twin index rework, NimBLE sdkconfig trimming, replacing BLEScan with raw NimBLE.
All three firmware envs must build; `make -C sim test` and `make -C sim regress` must pass.
