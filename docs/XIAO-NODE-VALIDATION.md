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

## Browser installation on a spare XIAO: passed

A second XIAO ESP32-C5 was installed using desktop Chrome and the native browser
installer. The page detected ESP32-C5 with 8 MB flash, downloaded the existing
c2bf4e5 node images, and reported **Verified: all four images hashed OK**.
The tested browser flow wrote bootloader, partitions, boot_app0 and application;
no command-line flashing substituted for this check. Handheld firmware remains
unchanged, with its separate 16 MB minimum and 6 MiB application limit.

## Saved recording check: passed

A real-GPS recording was stopped using the handheld control, then all four files
were read back from SD without publishing their contents:

| File | Result |
| --- | --- |
| wifi.csv | 427 rows, all with location fields; consistent 14-column structure |
| clients.csv | 367 rows; consistent 14-column structure |
| probes.csv | 1,301 rows; consistent 14-column structure |
| track.gpx | Valid XML, 332 points over 411 seconds; coordinates in range and timestamps ordered |

GPS held a 3D fix during the recording observation. GPS I2C and transfer-failure
counters remained zero, as did handheld node-packet rejection counters.
The XIAO USB serial port was unavailable during this longer run, so node-side
send/drop counters were not observed for that entire interval; continuing report
delivery was verified at the handheld. No claim of zero end-to-end packet loss
is made. The test was stationary, not a moving field run or capacity benchmark.

## Validation boundaries

One node and one Waveshare 3.5-inch handheld were used. This does not establish
multi-node capacity or physical acceptance with the NM and Waveshare 2.8-inch
handhelds. The saved-file check above used a real GPS fix. The firmware ZIP contains the earlier validation snapshot; this
page and the release notes record the follow-up checks without replacing binaries.
