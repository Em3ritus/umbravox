# UI and discovery roadmap

Owner priorities, 2026-10-30. These are planned work, not claims of implemented support.

## Current UI pass

- DOMAIN: located observations as the primary content, with a secondary north-up overview. A recorded coordinate is where the device was heard, not the AP's physical location.
- Open a row for the full available AP record; browse every located record through the located CODEX list.
- Full printable 32-byte SSID preservation for locally captured Wi-Fi APs and native detail/list display. Non-printable bytes retain the existing dot substitution; mesh-reported names remain limited by the existing wire protocol.
- Security capabilities/cipher from the existing parser, full BSSID, band/channel, RSSI/history, first/last observation age, recorded coordinate and strongest located RSSI.
- MENU labels centered as one- or two-line groups.
- Render at 480x320 and 320x240 before hardware approval. Preserve baseline 5e9c103 and the currently flashed e0fa8d6. No flash in this increment.

## Next feature priority

BLE bridge first, using the owner's ESP32Marauder PR #1671 as a reference. Keep radio/session/identity semantics explicit rather than copying unrelated firmware assumptions.

## Network discovery and enumeration

- Full untruncated SSIDs in every relevant capture/export path.
- Complete security/auth and cipher reporting: Open, WEP, WPA, WPA2, WPA3, enterprise. Audit parser limitations separately from UI presentation.
- Vendor/OUI lookup; band/channel width and PHY reporting.
- First/last timestamps and observation count, RSSI history.
- Client/station associations, probe-request preferred-network lists, hidden-name resolution from observed frames.
- WPS status, beacon interval/capabilities/country, deauth/disassoc detection.

## Capture and exports

- Raw 802.11 PCAP to SD for Wireshark.
- GPX track export.
- PMKID/handshake capture artifacts; decide supported formats and required metadata explicitly, including the requested HCCAPX compatibility.
- Kismet-compatible logging: specify the actual format/import target before implementation.

## Platform and UX

- Verify and finish AXP2101 battery badge plumbing; distinguish no battery, unavailable data, and valid percentage. Do not infer an unimplemented driver from a screenshot showing N/A.
- Per-node mesh command/configuration.
- Screen recording through periodic BMP/PNG dumps to SD.
- GPS-disciplined NTP clock.
- Pressed-touch feedback, full vertical layout, theme-builder completion.
- Android companion for offline WiGLE CSV ingestion, later.
- Mark POI: reachable from SCAN/DOMAIN, save location and timestamp with optional label. Refuse to silently stamp a stale fix; show an explicit no-fresh-fix state. Export semantics to be defined with GPX/session metadata.

## Other protocols (later)

- Zigbee / IEEE 802.15.4 discovery, with CYM Zigbee Scout as a comparison target.
- Thread / Matter discovery. Confirm silicon, board and software-stack capabilities and distinguish radio discovery from higher-level protocol identification before scoping implementation.
