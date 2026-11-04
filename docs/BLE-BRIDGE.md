# Phone wardrive bridge

Reference: slickrick773's [ESP32Marauder PR #1671](https://github.com/justcallmekoko/ESP32Marauder/pull/1671),
reviewed at `301afc5ae841cd6ea1f7121f18aba98d72c7a81c`.
This implementation uses the protocol contract; it does not copy Marauder's
WiFi scan lifecycle or initialize a second BLE stack.

## Operation

On the Waveshare native UI, tap BRIDGE in the shared header on any screen to toggle ON/OFF.
Choose the app separately under MENU -> SETTINGS -> page 3/3 -> BRIDGE APP
(WARDRIVEGO / BISCUIT). Turning OFF remembers that app. These choices persist
in NVS; existing stores default to OFF and WarDriveGo. BLE's percentage setting still controls the
advertisement scanner; BRIDGE separately controls the phone peripheral.

- **WarDriveGo:** discovers `Umbravox-XXXX` via the GhostLink service UUID.
  TX subscription sends CAPS/MODE/BEGIN/PLAN and starts the observation stream.
  `wdstream start -wifi`, `wdstream start`, or `start` restarts the stream;
  `wdstream start -wifi -ch 1,6,11,36` restarts with a channel filter for
  exported observations. The list accepts decimal channels 1–196; malformed
  lists are rejected. This does not retune the handheld or change node channel
  assignments. A start without `-ch` clears the filter.
  `iam a <phone name>` is accepted as a startup identity announcement; it does
  not change scanning or retain the phone name.
  `wdstream stop` or `stop` stops it.
- **Biscuit:** advertises the compatibility name `Biscuit`. Its Device Information
  model is `Biscuit DIY`, manufacturer is `Umbravox`. The app uses
  `CMD:wardriveall`, `CMD:scanap`, `CMD:wardrive`, `CMD:wardrive5g`, and
  `CMD:stopscan`. Band selection filters the exported observations; it does not
  override the board's capture-band settings. Time synchronization stops the
  app-time request; it does not replace the GNSS clock.
- **Startup:** briefly pauses an active BLE discovery scan before GATT reset/
  registration, then resumes it. A deliberately stopped scan stays stopped.
  Stop/init/resume failures are surfaced; failed startup returns the UI to OFF.
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
pass finishes before restarting, and table rollover resets the cursor generation. Learning a hidden AP name does
not restart the bridge cursor.
The exported plan is the collector's locally enabled, accepted channel set;
mesh-supplied observations can cover additional channels.

- Four RX writes, each at most 244 bytes; oversize/full input increments a counter.
- Command assembly at most 192 bytes, with a three-second fragment timeout.
- Twelve outgoing lines, each at most 383 bytes; state and queues live in PSRAM.
- At most one RX command and one notification per loop pass. Notifications are
  separated by at least 15 ms. AP enqueueing follows outbox capacity, with six
  slots reserved for control traffic; there is no additional ten-AP/s throttle.
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
validated**. The original bridge build was flashed and activation crashed. Inspection of
its linked NimBLE binary confirmed GATT mutation is forbidden during discovery;
the original implementation did not pause scanning before server creation.
No panic was captured in the subsequent idle-only serial observation. The pause/
restore fix is host-tested but awaits a new hardware validation.
The current hardware-accepted
baseline remains `227b5e8`; the branch also contains the unflashed RSSI sorting change.

After an authorized flash, select each app in turn and verify discovery, APs,
start/stop, disconnect/reconnect, and OFF. Exercise touch, scrolling and SD recording
while linked. Record `[bridge]` queue/retry/drop counters and `[mem]` free/minimum/
largest-block/stack readings during a sustained run. Test at least one reconnect
and confirm SD recording continues when the phone stream stops.

## Follow-up validation (2026-11-03)

The channel-qualified start command and phone identity announcement were exercised
with WardriveGo Android 3.1.339. The latest startup terminal exchange acknowledged
the starts, continued streaming, and contained no unsupported-command response.
Older errors remain in the app terminal history. This hardware check used the
Waveshare 3.5-inch handheld; other board builds are compile checks only.

A moving session was saved on the handheld and Android. The handheld CSV contained
9,177 distinct Wi-Fi identifiers and 337 BLE identifiers; the Android CSV contained
7,442 distinct Wi-Fi identifiers, with 6,436 Wi-Fi identifiers shared. Android also
scanned independently and applied its own geofence, so these sets do not measure
bridge loss. The app reported successful submissions to WiGLE, WDGWars and Wardrift.
The GPX parsed with 681 points over 1,023 seconds and one one-second backward time
step. Client and probe CSV structures were consistent. Raw captures stay private.

## Dense-route bridge pacing regression (November 4)

A fifteen-minute iPhone comparison contained 9,474 distinct handheld Wi-Fi IDs
and 5,511 phone Wi-Fi IDs, all of the latter present in the handheld file.
There were 3,963 handheld-only IDs; 165 had first-seen times before the phone's
observed window and 3,798 fell within it. First-seen times are not recording
button times, and these aggregates alone do not establish transport loss.

The old 100 ms AP producer delay capped output at 600 records/minute before
fragmentation, repeated observations, or other traffic. The handheld file had
nine minute bins above 600 newly seen Wi-Fi IDs, peaking at 1,095. A bounded
1,000-row burst regression reproduces unsent rows aging beyond the 60-second
freshness cutoff despite zero outbox drops. Capacity-driven admission delivers
all 1,000 in the tested 247- and 80-byte ATT MTU simulations, preserving the
15 ms notification pacing and reserved control capacity. This demonstrates a
failure mechanism, not an exact reconstruction of the field run.

Diagnostics now include negotiated MTU, completed host-enqueued lines, and
stale table visits. Stale visits can count the same row on later passes; they
are not unique losses. Completed lines include control traffic and do not prove
phone persistence. Freshness filtering remains intentional: replaying old
observations at the phone's current location would misrepresent their position.
Very small MTUs, a slow loop, congestion, or a slow receiver can still exceed
capacity. Physical phone validation and a matched field comparison are required
before claiming the drive's gap is resolved.

Candidate validation: the full host test target and all three native handheld
builds passed. The connected WS3.5 application write verified, rebooted, and
resumed phone streaming at negotiated MTU 247. With ten nodes powered, one
sampled ten-second interval queued 145 AP records (above the former 100-record
ceiling), with zero RX command drops or notification retries. The sampled
performance rows showed ten nodes and seven to eight reports/second; mesh packet
rejection counters remained zero. Stale visits did occur (47 cumulative in the
observed bridge session); they are not unique losses or proof of the new run's
delivery completeness. WDGO was paused during this bench check, so there is no phone recording to
export or compare. These results establish transport activity only; application
recording and field-gap closure remain unverified. This candidate has not updated
public downloads.
