# Local and mesh-node diagnostics

Working hardware baseline: `3ab7864`. Its PCAP couch check saved 1,837 frames with
9 drops (0.49%); the user confirmed responsive touch and a readable capture. The
new diagnostics are a separate increment and have not been flashed.

## Native MESH screen

MENU → MESH opens a paged diagnostics view. The middle `1/N >` button advances
through LOCAL, then each directly heard mesh node. ENROLL and RESET retain their
existing network actions. The same layout supports 480×320 and 320×240.

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

Remote node pages show applied ACTIVE/PASSIVE and dwell, coordinator-supplied
profile, actual CAMP/SWEEP assignment state, report-queue depth/capacity and peak,
queue drops, LINKFAIL, API rejection count, active scan failures, and the existing
receiver-estimated report-loss percentage. Queue depth is sampled after the report
queue drains, so zero is expected; PEAK is the lifetime high-water mark. All remote
counters are cumulative since that node boot.

LINKFAIL counts failed ESP-NOW send callbacks (including status/hello traffic),
not application-level acknowledgement. API counts immediate report submission
rejections, in frames. DROP counts sightings rejected because the report queue
was full. These are deliberately separate measures. The local bridge and remote
ESP-NOW paths use different counters and are not interchangeable.

NO TELEMETRY means no valid status message has arrived; requested coordinator
settings are never substituted. After ten seconds without a new status, the last
values are amber and explicitly STALE. Node silence can eventually expire the row
under the existing node-liveness policy.

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
headless nodes. This increment does NOT relay individual downstream status through
the coordinator's ClusterBatch to the handheld. That collector UI still has only
its existing aggregate cluster information. Old node firmware remains usable but
shows NO TELEMETRY. New node firmware is needed for remote status.

The two standalone full-UI boards are not headless mesh nodes: LOCAL diagnostics
work on them without changing roles. Do not replace either with a headless build
just to test this screen. Node firmware deployment is a separate explicit step.

## Verification and next hardware check

Host tests cover packet validation, sender mismatch, stale/unknown state
projection, duplicate/out-of-order rejection, reboot and uptime wrap, local bridge
projection, scene invalidation, and geometry/hit bounds at both sizes. Firmware
builds cover NM handheld, native Waveshare, both headless-node USB configurations,
and coordinator. Previews cover LOCAL, fresh remote, stale remote, and unknown
remote at both sizes. These checks do not establish over-air telemetry delivery.

After an explicitly approved flash, open MESH on a standalone board and verify
LOCAL matches the chosen dwell/mode and the bridge connection. Start/stop the
phone bridge stream and check that Q/STREAM changes without affecting touch.
Remote validation needs a separately approved headless node: verify its applied
settings, then stop its status transmissions and confirm STALE appears rather
than inferred fresh settings. Keep the working PCAP build available for rollback.
