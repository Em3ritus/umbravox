# Probe details and hidden-name evidence

Working hardware baseline: `7c0cc83`. This increment is not flashed or
hardware-accepted. It adds no transmit operations and changes no scan settings.

## PROBES

MENU → PROBES shows complete 32-byte printable SSIDs and full station MACs.
Each record contains RSSI, receive channel, age and request count. Empty SSID
requests are shown as `<ANY NETWORK>`, not discarded. Page arrows show two rows
at 480×320 and one at 320×240 with 44-pixel controls.

The key is **raw SSID bytes + length + station MAC**, so non-printable bytes that
both render as `.` do not merge distinct requests. ASCII printable bytes display
unchanged, including all 32 bytes; other bytes render as `.`. The view does not
claim lossless display of arbitrary binary/UTF-8 names. Equal timestamps retain
all rows and deterministic ordering. Request counts include retransmissions.

`LOCAL MAC` means locally administered, not proof of randomization. `UNIVERSAL
MAC` is likewise not authenticated identity. Neither probing a name nor the MAC
format proves prior membership, travel history, ownership or a particular person.
The title's MAC count counts retained distinct source addresses, not people.

`SEEN WITH AP <BSSID>` comes from the client's independently observed local
AP/station relationship within the preceding five minutes. It is the most recent
such relationship, **not a claim that the requested SSID belongs to that AP**.
A probe request alone never creates an associated-client entry or an AP link.

The view retains at most 512 source/name pairs, replacing the oldest at capacity.
EVICT is this replacement count; QDROP counts management-evidence queue overflow
(including non-probe evidence). Both are cumulative since boot/RESET. Counts and
lists describe retained observations, not exhaustive on-air history.

## Hidden SSID resolution

An existing AP with an empty name can gain a name from:

1. An AP-originated beacon/probe response with a valid SSID information element
   and matching transmitter/BSSID. SSID tags need not come first; malformed,
   duplicate or overlong SSID elements are rejected. Empty and all-zero beacon
   SSIDs are treated as hidden.
2. For an AP recently observed advertising a hidden SSID: a named association or
   reassociation request, followed in less than five seconds by a successful
   matching response. BSSID, station MAC, request/response subtype and receive
   channel must match. Failed, fragmented/protected, mismatched and timed-out
   responses do not resolve a name. A request alone never resolves it.

The hidden-AP observation must be less than five minutes old for association
correlation. Only one pending request per hidden AP is retained: a newer one
replaces it, which can miss concurrent exchanges but does not combine stations.
These are unauthenticated frame observations, not cryptographic proof. Spoofed
frames and changes to an AP's configuration remain possible.

AP detail → MORE labels the retained evidence `SSID RESOLVED: AP FRAME` or
`SSID RESOLVED: ASSOC EXCHANGE`. Conflicting names retain the existing AP name and
show `SSID EVIDENCE CONFLICT` while that evidence remains cached. Existing known
names are never overwritten. The cache holds 128 hidden BSSIDs, so evidence and
pending exchanges can be lost under churn; that is not an inference of a name.

Resolution fills only an already-known AP row. It does not invent an AP, signal
measurement, location, sighting or first/last-seen time. Name-based signature
matching is updated, but this path does not perform an unbounded evil-twin walk.
WiGLE's existing append-only export is unchanged: previously exported unnamed
rows are not rewritten retroactively. Probe records are not exported in this
increment; client/probe CSV export is a separate step.

## Runtime boundary

Native Waveshare builds replace the old probe table with a 53,272-byte PSRAM-only
store: 512 probe records, 128 hidden-AP evidence records and a 64-event queue.
There is no internal-heap fallback. Allocation failure is visible as unavailable.
The callback parses bounded frame/IE lengths and queues one fixed event; the loop
drains at most eight evidence events per pass. It does no callback SD I/O or
allocation. Management frames are parsed once for both name capture and evidence.
Sorting runs only for the probe screen, using a 1 KiB index array in the PSRAM
store, not on the loop stack and not a full-table copy. The separate existing client queue still has its own eight-event limit.

The native path fixes the old probe table's full-table alias update and equal-time
row omission, and bounds foreground work. It does not guarantee no latency or
queue drops in dense traffic. The NM legacy capture path and headless node wire
protocol are unchanged; remote mesh probe/name correlation is not implemented.
Shared native layouts are rendered and tested at both screen sizes.

## Verification

Sanitized host tests exercise truncated/malformed IE chains, full/binary SSIDs,
raw-key deduplication, wildcard requests, timestamp ties, failed/wrong-station/
wrong-channel/wrong-subtype responses, exact timeout boundary, timer wrap,
unknown BSSIDs, known-name protection, conflict handling and queue/table limits.
UI tests cover full names/MACs, observed-link wording, pager geometry, body repaint
and evidence labels. Firmware builds cover Waveshare, NM handheld and coordinator.

After explicit flash approval, use an owned hidden-SSID test AP and a normally
connecting client. Check the retained BSSID/name and evidence label, verify a
probe request for an unrelated SSID never renames that AP, and compare touch,
bridge and recording behavior with the hardware baseline. Without a captured
name-bearing frame or complete accepted exchange, staying hidden is correct.
