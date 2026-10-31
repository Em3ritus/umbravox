# Observed Wi-Fi clients

Working hardware baseline: `64661b0` (GPX recorded on the spare and the actual
SD file validated). This increment has not been flashed or hardware-accepted.

## Use

CODEX → select a Wi-Fi AP → MORE → MORE opens page 3/3, **OBSERVED CLIENTS**.
The arrow buttons page through two clients at 480×320, one at 320×240, with
44-pixel touch targets. The AP's SSID/BSSID stay visible. BLE details retain two
pages; Wi-Fi clients are not BLE identities.

Each row shows the full station MAC, station RSSI when available, observed
evidence, last-heard age, frame-observation count, channel and vendor/OUI.
Locally administered MACs read `LOCAL MAC / VENDOR UNKNOWN`; a local address is
not proof of randomization or a reliable manufacturer identifier.

Rows are newest-first, with deterministic MAC ordering for tied timestamps.
They expire from the view five minutes after their last observation. This is a
recent-observation window, **not the AP's current association table**. An empty
list means no qualifying frames observed locally, not that the AP has no clients.

## Evidence and parsing

- DATA: an infrastructure data/null/QoS frame with exactly one of ToDS/FromDS
  set binds its unicast station address to the BSSID. Protected frames work
  without decrypting their payload. Only the MAC header is inspected.
- ASSOC OK: an unprotected, unfragmented association/reassociation response from
  that BSSID with a successful status code. DATA+ASSOC means both were observed
  within the retained relationship. It is historical evidence, not proof the
  client remains connected now; disconnect frames do not erase history.
- Probe requests and association requests alone do not establish membership.
  Beacons, control frames, IBSS/TDLS, four-address WDS and malformed/truncated
  headers are excluded. Group, zero and AP-equals-station addresses are rejected.
- Station RSSI comes only from station-transmitted ToDS frames. A downlink or
  association response measures the AP transmitter instead and never overwrites
  station RSSI. After 60 seconds without a station transmission, RSSI reads `?`.
- FR counts processed frame observations, including retries; it is not unique
  packets, payload throughput, or a complete packet count. MAC spoofing and
  address rotation remain limitations of unauthenticated observation.

The callback uses actual receive-channel metadata, excludes receive errors and
removes the documented FCS from the parser length. See Espressif's
[ESP32-C5 Wi-Fi API](https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32c5/api-reference/network/esp_wifi.html).

## Resource and integration boundaries

Native Waveshare engine builds allocate **10,512 bytes in PSRAM** for a 64-event
queue and 256 AP/station-pair records. There is no internal-heap fallback;
allocation/filter failure shows capture unavailable. The callback parses a fixed
header and copies one event under the existing short mutex. It does no SD I/O,
allocation, sorting or table scan. The loop drains at most eight events per pass;
records are updated on the loop task only. The detail snapshot sorts at most
256 indices, using a 512-byte stack array and no allocation.

The oldest pair is replaced when the table is full. Replacing a still-recent
pair increments EVICT. QDROP counts events rejected by the full input queue.
Both counters are global since boot/RESET and shown below the list; they do not
measure losses for the selected AP alone. Expired records are not counted in the
visible list. WiGLE table rollover retains this recent history; RESET clears it.

DATA reception remains enabled with RAW PCAP off; turning PCAP off removes its
extra control-frame capture without disabling clients. This raises callback load
compared with management-only reception, so bounded processing is not a promise
of unchanged RF throughput or touch latency. The radio scan schedule, dwell,
active/passive setting, bridge protocol and AP counters are unchanged. No new
probe, association or other transmit operation is added.

This first increment observes the local Waveshare radio only. Client telemetry
is not relayed by headless mesh nodes/coordinators and is not added to WiGLE or
bridge output. An AP learned only over the mesh can have no locally observed
clients. The existing NM legacy UI and radio filter remain unchanged; the shared
native UI is rendered/tested at 320×240 for the future NM engine adapter.

## Validation

- Sanitized parser tests cover frame directions, QoS/HT length checks, protected
  data, successful/failed association responses, truncation, addresses and every
  FC-byte/flag/length combination through the fixed-header range.
- Table/queue tests cover overflow, AP binding, RSSI provenance, evidence merging,
  ordering, pagination, five-minute expiry, saturation, eviction and clock wrap.
- UI tests cover full MAC/local-address projection, unknown RSSI, scene changes,
  hit bounds/non-overlap and partial repaint equivalence at both resolutions.
- Native renders cover populated, empty and unavailable states.

After explicit flash approval: generate normal traffic on your own AP with a
known client, open its client page, check the MAC and last-heard age, and test
paging/touch while the bridge streams. Compare responsiveness and queue-drop
counters with PCAP off/on, confirm PCAP off still permits new client sightings,
and check the GPX/recording path still works. Active scans solicit AP discovery;
they do not force otherwise quiet clients to transmit. Hardware acceptance and
performance measurements remain pending until that check.
