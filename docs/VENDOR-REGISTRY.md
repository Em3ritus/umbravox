# Offline vendor registry and alert identity

The UI resolves registered IEEE organizations, not retail product identity or ownership.
A CradlePoint or Flock Safety prefix is not, by itself, proof of a surveillance device.
Flock Audio Inc. is a separate registrant and is not Flock Safety. This change does not
add signatures, alter confidence thresholds, or trigger alerts for every vendor.

## Data

Snapshot retrieved 2026-10-31: 58,915 unique prefixes (40,423 MA-L /24;
6,664 MA-M /28; 11,828 combined MA-S and legacy IAB /36). Lookup searches
/36, then /28, then /24. The table and deduplicated ASCII name pool remain in flash;
there is no full-database heap allocation. Non-ASCII names are transliterated for
the display font; names that cannot be represented receive an explicit placeholder.
Conflicting registrations at one prefix show MULTIPLE IEEE REGISTRANTS.

Authoritative source: https://standards.ieee.org/products-programs/regauth/
The official CSV endpoints returned HTTP 418 during this retrieval, so the input is
from runZero's IEEE mirror, pinned to commit
`32ea8eb95d7538038033636de6059c44cf92f9b7`:
https://github.com/runZeroInc/mac-tracker/tree/32ea8eb95d7538038033636de6059c44cf92f9b7/data/ieee
The mirror's MA-L file is byte-for-byte identical to this project's preceding MA-L input.
Every official URL, actual retrieval URL, input SHA-256 and row count is recorded in
`ui/oui_data.inc.sources.json`. This is a pinned snapshot, not an automatic updater.

Download the four manifest URLs to one directory and verify their SHA-256 values.
Regenerate offline:

```sh
python3 tools/generate_oui.py SOURCE_DIR ui/oui_data.inc --date 2026-10-31 --mirror 32ea8eb95d7538038033636de6059c44cf92f9b7
make -C sim test_oui_registry
python3 tools/test_generate_oui.py SOURCE_DIR sim/test_oui_registry
```

The CSV audit checks both endpoints of every allocation against an independently
constructed longest-prefix oracle (117,346 distinct addresses). Routine host tests
also check the four registries, Flock/CradlePoint, group/local addresses and generator
failure paths. Hash verification is necessary: a syntactically valid subset of a CSV
is not detectable as a complete registry by schema validation alone.

## Alerts

History owns the MAC, address role and reason rather than reconstructing an alert
from its abbreviated on-screen label. Same-named devices with different addresses
stay distinct. Each history record has a stable ID; a tap resolves that ID and freezes
a detail snapshot, rather than following a changing row number. Evicted IDs do not
open another record. Details wrap/page to show the registered name and matched prefix.

BLE random addresses are explicitly unidentified even if their high bits happen to
resemble a registered public prefix. Group and locally administered addresses do not
receive a registered vendor. A deauthentication flood's address is labeled TARGET
(NOT SENDER). Synthetic alerts retain no fabricated MAC/vendor or RF evidence.

Host previews and builds do not establish hardware acceptance. No new detection
rules or RF transmissions are introduced by this change.

## Build capacity

The WS image is 3,114,944 bytes against the existing 3,145,728-byte app
partition: 30,784 bytes remain. PlatformIO reports linked flash sections as
96.6%, but the final image including mapped-segment alignment is the stricter
limit. No partition changes were made. Further sizeable features need a storage
strategy review (for example an indexed SD registry with a small built-in fallback).
