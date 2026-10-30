# Located observations and AP details

Baseline retained: 5e9c103. Patch starts at e0fa8d6 in the isolated ui-input-presentation worktree. No merge, push or flash in this increment.

## Behavior

DOMAIN is the located-observation overview. The list occupies 312px at 480 and 192px at 320. Three 56px rows fit the large display; two 44px rows fit compact. Both show name, full BSSID, band/channel and RSSI. The 44px header opens the complete located CODEX list with its existing pager. A row opens details by the displayed record's stable address key. Recorded rows remain accessible with stale/absent current GPS; current-position map dots do not.

The smaller map is 144px/104px wide with its radius derived from its region. Its counter is plotted points / observations in range (the plot remains capped at 64). Coordinates represent where observations were recorded, not transmitter positions. Range controls remain 44px touch targets; range text sits beneath the overview.

Details show the full printable SSID (up to 32 bytes), BSSID, band/channel, signal, parsed security capability/cipher string, strongest recorded location and RSSI, first/last age, signature verdict/reason, and signal history. Larger metadata uses 2x type at 480; compact uses 1x type with a smaller history plot. Unknown security/location is labeled rather than invented. Freshness does not erase historic coordinates.

MENU labels use the actual scaled font height, with the LABELS/mode pair centered as one group. Hit regions are unchanged.

## Data boundaries

Local AP storage now holds 32 SSID bytes plus a terminator. Native CODEX and DOMAIN wrap names; detail fits the entire name. Existing non-printable-byte substitution and uppercase font appearance remain. Mesh-reported names and probe auxiliary records retain their existing wire/storage limits. No new RF metadata collection or TX behavior is introduced.

The C5 Node grows from 92 to 104 bytes: 384 KiB additional PSRAM across 32768 slots. Node size verified from the target object DWARF. Snapshot strings also grow modestly. Hardware memory/performance acceptance is not claimed before flashing.

## Verification

- Host suite: 79 test programs pass. Existing themepack invalid-enum sanitizer diagnostic remains at ui/themepack.cpp:436; outside this patch.
- Theme/designer suite: 206 tests pass.
- Target compile: ws-c5-35-engine succeeds.
- New checks: rendered MENU ink centered in both label modes/sizes; full 32-byte names retained through native projections; DOMAIN long names wrap entirely inside their hit row; enterprise/cipher strings and signed coordinates preserved.
- sim/native-hit-contract.json is the independent expected native hit set. SCAN remains unchanged; DOMAIN expectations intentionally supersede the old narrow-column mockup. Generate with `python3 sim/gen_oracle.py sim/native-hit-contract.json > sim/oracle_hits.inc`.
- sim/ui_review_png.cpp generates production-compositor previews for DOMAIN, AP detail, CODEX and MENU at both native sizes from explicit fixtures. Render data is illustrative, not a capture from the board.
