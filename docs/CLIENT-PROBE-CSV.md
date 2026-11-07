# Client and probe CSV companions

Waveshare native UI: start REC normally. Alongside `/WD-<session>.csv` (unchanged WiGLE), the firmware writes `/WD-<session>-clients.csv` and `/WD-<session>-probes.csv`. No extra capture mode or GPS fix is required. Legacy NM and mesh coordinator behavior is unchanged. Files can be retrieved from the SD card or `SDLS` / `SDCAT <path>`; the existing WiGLE-only FILES list does not list these sidecars. `EVIDENCE CSV` reports state and committed row count.

## What rows mean

These are **rolling snapshots of bounded local observation tables**, not per-frame records or session-only counters. A sweep starts at REC, visits one of 768 slots per loop pass, flushes its batches, then waits 10 seconds before the next sweep. Sweep duration depends on loop load. Each row has its own snapshot time; a sweep is not an atomic table snapshot. Records can update or be evicted during a sweep. REC stop drains accepted rows but does not force a final full sweep; very short recordings may contain only headers or a partial sweep. Retained observations from before REC are included. Use PCAP for individual frames.

Repeated rows contain cumulative counts for that retained record: **do not sum them**. Key clients by AP BSSID + station MAC + first_seen_ms; key probes by station MAC + ssid_hex + first_seen_ms. Eviction/reset can restart counts. Files belong to one boot/session; millisecond values wrap after about 49.7 days and are not wall-clock timestamps. A MAC is an observed address, not a durable person/device identity.

Both schemas include `snapshot_ms`, `snapshot_utc` (UTC at export, blank without a valid clock), `first_seen_ms`, `last_seen_ms`, `queue_drops`, and `table_evictions`. Loss counters are cumulative table/queue diagnostics, not per-row losses. The probe queue also handles AP/name evidence, so its drops are not exclusively probe requests. There is no invented historical UTC or GPS location.

### Clients

Full `ap_bssid`, `station_mac`, `local_mac` (locally administered bit), channel, `station_rssi_dbm` and `rssi_age_ms`, `evidence` (`DATA`, `ASSOC_OK`, `DATA+ASSOC_OK`), and `observed_frames`. RSSI is from station-transmitted frames only; missing RSSI/age are empty fields. A retained older RSSI carries its age. Relations older than five minutes are omitted. Observed data or successful association responses are evidence, not a guarantee the station remains connected.

### Probes

Full station MAC, local-address bit, channel, received RSSI, `request_type` (`DIRECTED` or `WILDCARD`), `ssid_display`, `ssid_hex`, and `observed_requests`. Directed means a nonempty SSID element, not proof of a past connection. No requested network is assigned to an AP merely because the station was observed near it.

SSID display uses dots for nonprintable bytes, RFC4180 quote escaping, and a leading apostrophe to keep over-the-air names from becoming spreadsheet formulas. **`ssid_hex` is authoritative and lossless**, including embedded NULs, non-ASCII and the original leading character. Import it as text in spreadsheet software to preserve leading zeros. Wildcard raw SSID is empty. Headers/records use CRLF. Probe rows remain until the 512-record table evicts or resets them; last_seen_ms identifies their age.

## Storage and limits

Two fixed 1 KiB batches plus shared formatting scratch (no per-row allocation or large stack arrays). Main-loop only; each tick examines at most one slot and performs at most one append transaction. Shutdown may drain both batches synchronously. SD operations are synchronous and can still stall on slow media; this is a work bound, not a hard latency guarantee. Files are capped at 16 MiB each. On card, size, write or verified-close failure, both exports stop for that session and report a warning. Short writes attempt rollback to the previous size; power loss/rollback failure can still leave a partial last row. Existing sidecars are never overwritten. WiGLE and GPX continue independently. Header-only files do not prove reception or table availability.

## Validation

Host tests exercise the production formatter/writer with mocked SD: raw-name roundtrip, full MACs, unknown RSSI/time, quote/formula handling, wildcard/max-length names, one-slot/one-transaction limits, repeated snapshots, partial stop, rollover, existing-file protection, short writes, close failure, and absent card. Production builds cover Waveshare native, NM handheld and NM coordinator. Hardware recording/retrieval and latency remain to be checked after an authorized flash.
