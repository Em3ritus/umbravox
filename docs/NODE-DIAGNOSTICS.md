# Node diagnostics

The native engine's Nodes screen has one local page and two pages per live node.
NEXT cycles Local → node Radio → node Link. TUNE opens Settings → Nodes; its
profile and dwell controls are **fleet-wide**, not a per-node override. Active
versus passive discovery remains the shared Scanning setting.

## Radio

- ASSIGNED lists channels the coordinator assigned. This is the coordinator's
  plan, not proof that the node received or applied that assignment.
- APPLIED mode, dwell and camp/sweep come from the node's telemetry packet.
- TARGET is the current requested fleet mode, dwell and profile.
- MATCH means fresh telemetry values match the target; it is not a protocol ACK.
- PENDING means fresh reported values differ from the target.
- STALE means telemetry is at least 10 seconds old. It is not relabeled pending
  or matched even when its old values happen to equal the target.
- UNKNOWN means no supported telemetry was received. Old nodes remain visible;
  unavailable fields are not fabricated as zero or success.

## Link

- REPORT is age of the last received MSG_REPORT, including empty keepalive
  reports; HELLO and telemetry do not refresh this timestamp.
- HEARD is age of the latest accepted node activity.
- Q / PEAK / DROP describe node capture queue occupancy, high-water mark and
  dropped records, as reported by the node.
- DELIVERY FAIL counts failed asynchronous ESP-NOW sends; API REJECT counts
  immediate report-submission errors. These are distinct measurements.
- SCANFAIL is the node's scan-failure count. LOSS is the coordinator's existing
  report-sequence gap estimate, not a direct RF packet-loss measurement.
- Stale telemetry counters are marked STALE. Node counters can reset on reboot;
  they are totals, not per-second rates.

No packet layout/version or radio scheduling is changed by this increment.
Nodes that already emit the supported telemetry need no new firmware for these
views. Current scope does not add persistent per-node tuning or runtime changes
to the reporting interval. NM's legacy renderer is not redesigned by this work.

## Existing local counters

LOCAL is useful on the existing full-UI standalone WarDriveGo/Biscuit boards:

- ACTIVE/PASSIVE and dwell are the local applied software scan policy.
- SCANS / DONE / SCANFAIL / OMIT are the active Wi-Fi scan adapter counters.
  Passive operation does not increment these driver-managed scan counters.
- BRIDGE names the selected app, STREAM or LINK state, and outgoing queue depth.
- RXDROP counts rejected/full incoming phone-command queue entries.
- TXRETRY counts notification submission failures. A successful submission does
  not establish that the phone consumed the data. These counters are not RF
  capture loss and must not be compared with PCAP drops.

Bridge retry/drop counters accumulate since bridge-state initialization; scan
counters accumulate since boot. No BLE initialization, transmission cadence,
scanner scheduling, or radio dwell policy is changed by displaying them.


## Wire path and compatibility

An additive mesh-v4 kind 9 packet carries format version 1, sender MAC, random
boot token, uptime, applied policy and counters. Existing assignment and sighting
report layouts are unchanged. Nodes send one status in their existing 2.5-second
report window, even with no sightings; there is no extra channel visit or added
wait. It does add one radio packet per reporting window.

The receiver validates exact size, network/protocol/format, source-MAC match,
policy ranges, and queue invariants before storing a snapshot. Duplicate/older
uptime from the same boot token is ignored; a new token resets ordering. MAC
matching is identity consistency, not cryptographic authentication; existing
unencrypted mesh trust is unchanged.

Both core and coordinator builds accept status from their directly attached
headless nodes. The protocol does NOT relay individual downstream status through
the coordinator's ClusterBatch to the handheld. That collector UI still has only
its existing aggregate cluster information. Old node firmware remains usable but
shows NO TELEMETRY. Nodes lacking that telemetry message need compatible firmware for remote status.

The two standalone full-UI boards are not headless mesh nodes: LOCAL diagnostics
work on them without changing roles. Do not replace either with a headless build
just to test this screen. Node firmware deployment is a separate explicit step.
