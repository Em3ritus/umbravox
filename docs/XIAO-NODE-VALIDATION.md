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
After reset, the spare booted as UMBRAVOX, enrolled on the handheld's network,
received 20 channels and applied Passive/220 ms. In the follow-up observation,
handheld accepted reports increased from 24 to 120 while the node's sent-record
counter increased from 252 to 805 with zero node queue drops/send failures.
The handheld's two pre-enrollment magic/network rejects did not increase;
version, length and no-slot rejects remained zero. These are bounded bench
results, not unique-network counts or a multi-node capacity claim.

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

## Two-node power-loss and recovery check: passed

With two XIAO nodes joined to the Waveshare 3.5-inch handheld, one node was
powered off. The handheld expired it after 75 seconds of silence and assigned
all 20 scanning channels to the remaining node. Reports continued arriving.

After power was restored, the second node rejoined automatically. The handheld
assigned 12 channels (2–13) to one node and eight channels (starting at 36) to
the other, and reported two live nodes. The USB-observed node applied the
12-channel assignment and retained Passive scanning with a 220 ms dwell.

Across the 95-second observation, handheld accepted reports increased from
1,999 to 2,504. The observed node's dropped-record counter stayed at zero and
its send-failure counter stayed at 45; those failures predated this observation.
Handheld rejection counters did not increase. Only one node's serial counters
were available; aggregate handheld counters do not establish per-node delivery
or zero end-to-end loss.

## Validation boundaries

Two nodes and one Waveshare 3.5-inch handheld were used for the recovery check. This does not establish
capacity beyond two nodes or physical acceptance with the NM and Waveshare 2.8-inch
handhelds. The saved-file check above used a real GPS fix. The firmware ZIP contains the earlier validation snapshot; this
page and the release notes record the follow-up checks without replacing binaries.
