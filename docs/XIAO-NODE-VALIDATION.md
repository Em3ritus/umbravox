# XIAO node public-beta validation

Bench date: 2026-11-03. XIAO ESP32-C5 node firmware c2bf4e5, paired with
Waveshare ESP32-C5-Touch-LCD-3.5 handheld firmware bce092c.

## Passed

- Four-image installation verified using esptool 5.6.0 in ROM/no-stub mode.
- Node joined, received its channel assignment and delivered reports.
- Active/150 ms settings applied on the node.
- Changing the handheld's node controls to Passive/220 ms produced the matching
  node confirmation. This checks configuration propagation/application, not
  measured RF dwell duration or absence of probe traffic.
- Node restart recovered assignment and reporting.
- Enrollment assigned a nonzero network ID; the node retained it through restart.
- After the handheld was physically disconnected and reconnected, and the node
  restarted, they rejoined on the enrolled network with Passive/220 ms retained.
- In the initial 40-second reporting window, accepted reports increased from
  217 to 473 with zero protocol rejections. Node counters showed zero queue drops
  and send failures. These counts are records, not unique networks.
- GPS-disconnected testing produced I2C errors. After wiring the LC76G and
  power-cycling the handheld, valid NMEA arrived with zero GPS I2C failures or
  transfer errors in the observed diagnostics. This establishes the connection
  issue seen on this bench; it does not rule out other I2C faults.

## Validation boundaries

One node and one Waveshare 3.5-inch handheld were used. This does not establish
multi-node capacity or physical acceptance with the NM and Waveshare 2.8-inch
handhelds. Location-tagged recording requires a real GPS fix and separate saved-file
verification. The firmware ZIP contains the earlier validation snapshot; this
page and the release notes record the follow-up checks without replacing binaries.
