# Phone wardrive bridge

Reference: slickrick773's [ESP32Marauder PR #1671](https://github.com/justcallmekoko/ESP32Marauder/pull/1671),
reviewed at `301afc5ae841cd6ea1f7121f18aba98d72c7a81c`.
This implementation uses the protocol contract; it does not copy Marauder's
WiFi scan lifecycle or initialize a second BLE stack.

## Operation

On the Waveshare native UI, open MENU -> SETTINGS, then page 3/3.
BRIDGE cycles OFF / WARDRIVEGO / BISCUIT. The choice persists in NVS and
existing stores default to OFF. BLE's percentage setting still controls the
advertisement scanner; BRIDGE separately controls the phone peripheral.

- **WarDriveGo:** discovers `Umbravox-XXXX` via the GhostLink service UUID.
  TX subscription sends CAPS/MODE/BEGIN/PLAN and starts the observation stream.
  `wdstream start -wifi`, `wdstream start`, or `start` restarts the stream;
  `wdstream stop` or `stop` stops it.
- **Biscuit:** advertises the compatibility name `Biscuit`. Its Device Information
  model is `Biscuit DIY`, manufacturer is `Umbravox`. The app uses
  `CMD:wardriveall`, `CMD:scanap`, `CMD:wardrive`, `CMD:wardrive5g`, and
  `CMD:stopscan`. Band selection filters the exported observations; it does not
  override the board's capture-band settings. Time synchronization stops the
  app-time request; it does not replace the GNSS clock.
- **OFF:** stops advertising, disconnects the phone, and clears the stream.
  Allocated GATT objects and bounded PSRAM buffers remain for reuse; toggling
  does not deinitialize BLE or tear down the scanner.

Only the selected app surface accepts commands and sends notifications. This
version selects one advertising identity instead of rotating identities. A mode
change disconnects the old link. The wired `BRIDGE` command reports current
OFF / ADVERTISING / CONNECTED / STREAMING state.

**Version sentinel: `99.99.99`.** WD:CAPS contains `fw=99.99.99`, `ota=0`, and
`board_name=Umbravox_WS_C5_35`. Device Information firmware revision also reports
`99.99.99`. This is the interoperability sentinel, not an Umbravox release number.
There is no OTA characteristic or BLE path to the wired command console.
Tests enforce the exact sentinel, OTA flag, and board identity.

Phone start/stop controls export only. WiFi capture, BLE detection, mesh, local
GPS, and SD recording retain their existing controls. RECORD/STOP in the footer
continues to mean SD recording, independent of phone streaming.

## Data and resource bounds

The source is the existing deduplicated WiFi observation table, not a new active
scan. AP records contain full stored SSID, BSSID, latest RSSI, channel and auth.
GhostLink hex-encodes the SSID; Biscuit flattens commas/control characters because
its records are comma-delimited. Embedded NUL SSIDs are limited by the existing
collector's string representation. GPS/BT/raw-capture export capabilities are
reported as zero: this bridge exports WiFi APs and the phone supplies its location.

A cursor examines at most 32 table entries per call, copying each eligible row
under the capture lock. Records older than 60 seconds are skipped. Newly appended
APs remain eligible after the cursor reaches the current end; existing records
are revisited on completed passes no more frequently than once a minute. A large
pass finishes before restarting, and table rollover resets the cursor generation.
The exported plan is the collector's locally enabled, accepted channel set;
mesh-supplied observations can cover additional channels.

- Four RX writes, each at most 244 bytes; oversize/full input increments a counter.
- Command assembly at most 192 bytes, with a three-second fragment timeout.
- Twelve outgoing lines, each at most 383 bytes; state and queues live in PSRAM.
- At most one RX command and one notification per loop pass. Notifications are
  separated by at least 15 ms; AP enqueueing is paced to at most ten per second.
- Packetization honors negotiated ATT MTU, including the 12-byte GhostLink header.
  A failed host enqueue retains the same bytes for retry. Host acceptance is not
  a claim that the phone received or persisted the data.
- Stop/start drops unsent backlog but finishes a partially sent line to avoid
  corrupting the receiving line stream. RX control remains serviceable under
  notification backpressure.
- Disconnect/unsubscribe clears session state; epoch-tagged RX cannot execute a
  command from a prior connection. Writes immediately after subscription survive
  the loop's session reset.
- Connection parameters request 30–60 ms intervals and a six-second supervision
  timeout. AP transmission waits three seconds after subscription. The phone may
  choose other parameters; coexistence remains a hardware measurement.

The bridge is compiled only for `WS_C5_35 && UMBRAVOX_SCAN_ENGINE`. NM's device
bridge is a no-op; its legacy UI does not expose this setting. The shared native
layout also renders the control at 320x240 for future native-engine ports.

## Verification and next hardware check

Host tests cover frame validation, fragmented commands, limits, malicious/control
characters, exact command dispatch, full-length SSIDs, auth/band formatting,
MTUs 23/64/247/517, byte-identical notification retries, queue overflow, partial-line
preservation, large-pass completion, sentinel identity, settings clamps and hit
geometry at both sizes. The existing host suite also checks the settings comparator
and page projections. WS native and NM builds pass. All 81 host test programs and five build-script
tests pass; the pre-existing theme-parser invalid-enum sanitizer diagnostic
remains separate from this work.

These are software checks. Phone discovery, subscriptions, AP ingestion, reconnects,
radio coexistence and heap with the GATT server enabled are **not yet hardware
validated**. No flash accompanies this implementation. The current hardware-accepted
baseline remains `227b5e8`; the branch also contains the unflashed RSSI sorting change.

After an authorized flash, select each app in turn and verify discovery, APs,
start/stop, disconnect/reconnect, and OFF. Exercise touch, scrolling and SD recording
while linked. Record `[bridge]` queue/retry/drop counters and `[mem]` free/minimum/
largest-block/stack readings during a sustained run. Test at least one reconnect
and confirm SD recording continues when the phone stream stops.
