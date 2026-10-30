# DOMAIN and CODEX completion

Fallback remains `5e9c103`; the current hardware-confirmed recording build is
`725620d`. This increment is not flashed.

## DOMAIN

The device projects both observations and the track at the native map radius
(91px on 480x320), rather than reusing 42px legacy-canvas offsets. The map cache
also invalidates on a radius change. Legacy callers retain their existing scale.
A stale or missing GPS fix suppresses current-relative dots and track; the map
shows GPS STALE or GPS WAIT. Historical located-list entries remain accessible.

The right column now contains the newest located Wi-Fi observations and stable
record keys. Its header or compact OPEN LIST summary opens the real located
CODEX filter. That filter covers Wi-Fi observations, matching the map's source;
it does not claim to include geolocated BLE. Wide shows two preview rows; compact
retains its summary. The full list is paginated in CODEX.

Map labels distinguish IN RANGE from SHOWN (the map retains its 64-dot cap).
Located observations record where the receiver heard an AP, not a surveyed AP
position. Co-located observations can legitimately overlap at the center.

## CODEX

Three 44px rows remain at 480x320, two at 320x240. A separate pager column has
76x44 PREV/NEXT targets. The visible range and total are shown in those controls;
the range counts displayed rows, not the eight-entry source buffer. Page limits
handle an empty list and a partial last page. LOCATED actually filters the data.

Rows preserve the collector's newest-first order. Sorting each fetched window by
RSSI was not a global strongest-first list and made pagination misleading. Live
new arrivals can move page contents; taps are bound to displayed address keys.
Only completed presentation updates the visible key snapshot. Entry taps defer
against the incoming snapshot. Detail uses bounded hash-table identity lookup,
not a live row index. A vanished identity produces GONE, not another record.

The located count is maintained when a Wi-Fi record first gains a position and
cleared with the table, avoiding repeated whole-table counting in the UI loop.

## Verification and limits

Host coverage includes visible page counts at both sizes, traversal of eight
records including the last partial page, clamping, address parsing, located rows,
native-radius cardinal positions, stale-map clearing, render clipping, and hit
bounds/non-overlap. Firmware is compile-tested; on-device geographic behavior,
new-arrival selection and paging still need hardware acceptance.

## Next priority

BLE bridge is first after these fixes. Starting point: slickrick773's
ESP32Marauder PR #1671:
https://github.com/justcallmekoko/ESP32Marauder/pull/1671

Reuse the existing WarDriveGo/GhostLink and Biscuit Manager protocol work as
appropriate, with a separate review of umbravox's scan ownership, NimBLE lifetime,
coexistence and memory budget. The prior passive-only requirement has been lifted
by the owner; this screen increment does not change radio policy or add a bridge.
