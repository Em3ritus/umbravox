# GPX track export

GPX follows the existing REC control. Recording creates a companion `.gpx`
next to the WiGLE CSV (for example `/WD-0012.csv` and `/WD-0012.gpx`). There
is no separate track toggle. RAW PCAP remains independent.

A file is created only after a fresh onboard GNSS position arrives. The phone
GPS used by WarDriveGo/Biscuit is not fed back into this firmware's GNSS parser;
phone positioning alone does not create an onboard track. Without onboard GPS,
REC still records WiGLE normally and the GPX status reads `WAIT GPS`.

## Contents

- GPX 1.1 track, WGS84 latitude/longitude to seven decimal places.
- At most one point per second, only on a newly received valid fix. Re-reading
  the same fix does not manufacture another point. Freshness matches
  `ui::Gnss::FRESH_MS` (strictly less than five seconds).
- A stale/invalid fix or a gap of five seconds between accepted fixes starts a
  new track segment on reacquisition. No line is invented across that gap.
- UTC `<time>` is included only when a known GNSS date is available (2020 or
  later). It uses the parser clock adjusted for fix age, at whole-second
  precision. Otherwise time is omitted; no 1970 timestamp is fabricated.
- No invented altitude, speed, accuracy or satellite-fix dimension. This first
  increment does not include POI waypoints; existing POIs remain in `poi.csv`.

## Storage and failure handling

A fixed 2 KiB buffer batches points every five seconds; STOP, console reboot or
CSV rollover flushes the remainder. Each service call performs at most one GPX
batch transaction. Existing SD flushes are synchronous and can still exceed the
usual loop time on a slow card; this is not a hard real-time write guarantee.
There is no callback I/O, heap allocation, scan-policy change or new task.

Every successful batch writes a closing XML footer. The next batch replaces only
that footer, appending new points and another footer. Thus the document is
complete between batches, even while recording. A sudden power loss can lose the
buffered points and, if it happens during footer replacement, leave truncated XML.
This is not an atomic filesystem operation or a power-loss recovery mechanism.
Always stop recording and wait for saving to finish before removing power/card.

On a detected write error, the writer attempts to restore the previous footer
and stops writing that track. Restoration can itself fail if the card is absent
or full. WiGLE and PCAP continue under their own existing error handling.
`diag.log` receives an error; the serial `GPX` command reports status, path and
successfully committed point count. A new CSV session clears the GPX error.
Existing GPX files are never overwritten or resumed. Files are limited to 16 MiB;
reaching that limit reports an error and stops the track rather than filling
storage indefinitely. Buffered points are not counted as committed.

The FILES screen currently lists WiGLE CSV sessions only. Retrieve GPX directly
from the SD card; `SDLS` can confirm its presence. No file-browser or deletion
policy change is included here.

## Verification

`make -C sim test_gpx && ./sim/test_gpx` exercises the actual writer with a fake
SD sink under AddressSanitizer and UndefinedBehaviorSanitizer: delayed creation,
1 Hz sampling, batching, final flush, segment gaps, invalid coordinates, unknown
UTC, rollover, existing-file protection, partial writes, close verification,
card removal, truncate failure and millisecond wrap.

The test exports `/tmp/umbravox-gpx-time.gpx` and
`/tmp/umbravox-gpx-gap.gpx`. Validate both with `xmllint --schema` against the
[official GPX 1.1 schema](https://www.topografix.com/GPX/1/1/gpx.xsd).

Firmware compilation covers the Waveshare native UI and NM handheld. Hardware
validation remains pending: with onboard GPS connected and fresh, record a short
track, stop REC, confirm `GPX` reports SAVED and import the file into a GPX viewer.
Then check a GPS reception gap creates separate segments and that touch/bridge
remain responsive while recording. Do not flash merely to test phone-only GPS.
