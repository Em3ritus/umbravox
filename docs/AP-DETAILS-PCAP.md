# AP details and raw Wi-Fi capture

This increment follows the working `c6b5e8a` baseline. No board is flashed by
building it. The native UI exposes these controls; the legacy NM canvas remains
unchanged. Native previews cover both 480×320 and 320×240.

## AP detail

Open an AP from CODEX or DOMAIN. MORE switches between signal/location/security
and advertised AP information. Back retains the list selection. SSID and BSSID
remain visible on both pages.

The second page shows IEEE MA-L registrant, observation count, HT/VHT/HE
capabilities, advertised operating width, country, WPS advertisement/lock flag,
beacon interval (TU), raw capability bits, and first/last ages. These are parsed
from locally received beacons/probe responses. Active-scan records and mesh
reports do not carry these extra information elements; missing values remain
unknown. Previously collected valid metadata is retained until superseded by a
valid local advertisement. It is not a guarantee of the AP's current settings.

Observation count includes local receptions, driver scan results, and relayed
reports, saturating at UINT32_MAX. It is not a count of distinct frames or clients.
First/last are session-relative ages. Signal history is not a direction estimate.
HT/VHT/HE are advertised capabilities, not the modulation of each received frame.
No WPS IE means NOT ADVERTISED, not necessarily disabled. BLE addresses are not
assigned a Wi-Fi vendor or observation count by this page.

### Vendor database

`ui/oui_data.inc` is generated from the official IEEE MA-L CSV:
https://standards-oui.ieee.org/oui/oui.csv

It contains 40,423 distinct 24-bit prefixes. The generated file records the input
SHA-256; regenerate explicitly with:

```
python3 tools/generate_oui.py /path/to/oui.csv ui/oui_data.inc
```

Names are normalized to ASCII for the firmware font. Historical duplicate
assignments are labeled MULTIPLE IEEE REGISTRANTS. Group and locally administered
addresses are labeled rather than attributed. MA-M and MA-S databases are not
included. A registrant is not proof of the finished product's manufacturer.
Long registrant names end with an explicit ellipsis on the display.

## Raw PCAP (native Waveshare engine)

SETTINGS → final page → RAW PCAP. Either step button toggles it. The row shows
state and `D<count>` dropped frames. Serial commands:

- `PCAP ON`: start a new `/captureNNNN.pcap` on the SD card.
- `PCAP OFF`: stop accepting frames and drain the queued records.
- `PCAP`: print state, filename, successfully written and dropped counts, successful data batches, and peak queue occupancy.

This is independent of the footer's REC/STOP (WiGLE CSV) and defaults OFF on every
boot. Stop PCAP and wait for OFF before removing power/card. Read the resulting
file from the card in Wireshark. The existing FILES browser still lists WiGLE
sessions, not PCAPs.

Format: little-endian PCAP v2.4, LINKTYPE_IEEE802_11 (105), 1,536-byte snapshot
length, FCS removed. Longer frames have their original length recorded and their
captured bytes truncated. Only local, successful management/data/control RX
frames supplied by the ESP driver are recorded. No BLE, mesh-relayed frames,
radiotap RSSI/channel information, decryption, or promise of complete coverage.
The existing channel hopper/active-scan policy is unchanged. PCAP does not itself
send probes or attack traffic.

**Timestamps are monotonic board uptime in microseconds**, represented using the
PCAP seconds/microseconds fields. They are not GPS/phone/UTC timestamps; an epoch
1970 date in Wireshark is expected. Use relative-time display. Do not align files
from different boots/boards by those dates.

A 64-record queue plus an 8 KiB contiguous staging buffer occupies 107,648 bytes
of PSRAM, allocated only on first start and retained for reuse. This is 82,784
bytes more PSRAM than the original recorder, while removing its 3,108-byte
internal-RAM staging array. The callback never writes SD, allocates, or waits on
a lock; contention/full queue increments drops.

Main-loop service copies at most sixteen queued records per pass, releasing the
queue lock between records. It makes at most one contiguous append of up to 8 KiB,
followed by the existing verified close. A batch flushes when full (or when the
next record would not fit), after 250 ms since its first staged record, or when
stopping. The deadline is serviced by the main loop, not a guaranteed wall-clock
latency. Stop drains all staged/queued frames before OFF. There is no SD file held
open across loop passes, so WiGLE and POI keep using the same append API.

The working `a9def7e` recorder was hardware checked: the file opened in Wireshark
and touch stayed responsive, with 1,868 written and 1,453 dropped in the reported
run. This batching revision has not been flashed. Its drop rate and touch
latency still need measurement: SD open/write/flush latency cannot be bounded by
a byte budget. An unexpected power loss may lose the staged and queued tail;
stop and wait for OFF before removing power.

Files stop at 32 MiB. Writes/close failures stop recording; partial batches are
rolled back. Failed rollback is shown as DAMAGED FILE. A new start uses a new
filename, never appends to the damaged file. Startup checks at most 64 candidate
names per request; NAME LIMIT may require another request if the card already
contains many captures. Drops include queue contention/overflow and records lost
to write failure or the file-size cap. They do not count frames never delivered
by the radio or time spent on other channels.

## Resource and validation notes

AP metadata adds 20 bytes per dedup slot: 655,360 bytes of PSRAM for 32,768 slots
(table now 4,063,232 bytes on the 32-bit target). The vendor table is flash-resident.
New UI snapshots and PCAP staging also consume a few KB of internal RAM. This is
not a measured post-flash heap result.

Host tests exercise metadata parsing, unknown/invalid cases, OUI handling,
settings/page projection, binary PCAP parsing, bounded queue/service, concurrent
producer/consumer accounting, stop/drain, unique files, write/close/open failures,
rollback failure, and size limiting. `tcpdump` independently decodes the generated
complete-beacon fixture. All four production build targets are checked; native
renders are inspected at both sizes. Hardware validation remains separate.

Reference format registry: https://www.iana.org/assignments/pcap
ESP-IDF C5 RX metadata: https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32c5/api-reference/network/esp_wifi.html
