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

## Pending-observation coalescing check (2026-11-03)

With Discovery / Active / 100 ms applied, the original 5 GHz node accumulated
new queue drops during a stationary two-node observation. The candidate queue
keeps one pending record per BSSID, refreshing signal, channel and age while
preserving a learned name across hidden-name repeats. This is per-report queue
coalescing, not permanent deduplication or durable delivery.

The first candidate node completed a 70-second observation without queue drops
or send failures. After updating both nodes, a 65-second joint observation showed
zero node queue drops and send failures, increasing handheld accepted reports,
no handheld packet rejections, and an active WDGO stream without bridge receive
drops or notification retries. This is bounded stationary evidence, not a field
comparison or proof of lossless delivery. Seen and sent counts now differ by
coalescing as well as other causes; their difference must not be called loss.
Host tests exercise duplicate bursts, full queues, distinct BSSIDs, metadata
preservation and refreshing records in a full queue.

## Ten-node stationary check (2026-11-03)

All ten XIAO nodes were flashed and individually enrolled. Both groups of five
were observed with handheld report delivery and WDGO streaming. Bringing all ten
online produced ten assignments of two channels each in Discovery / Active /
100 ms mode. A joint node power interruption was followed by ten-node recovery;
expiry events occurred around the interruption and must not be omitted.

The subsequent stationary observation lasted at least 913 seconds. It recorded
no node-expiry messages or crashes, 6,466 additional accepted mesh reports, no
increase in packet rejection counters, and zero bridge receive drops or
notification retries. Free heap fluctuated between 33,680 and 40,964 bytes in
sampled bridge diagnostics. These are aggregate counters; per-node internal
counters were not collected simultaneously, and this is not proof of lossless
delivery, improved unique discovery, or moving performance with ten nodes.

Recurring I2C errors were present during the observation. A follow-up GPS status
query confirmed transfer failures; this is an unresolved peripheral issue and
prevents describing the complete hardware setup as error-free. The successful
mesh/bridge check does not establish current GPS reliability.

### GPS recovery follow-up

The intermittent GPS I2C errors persisted across software resets. A complete
30-second power removal from the handheld and GPS restored normal NMEA delivery
on the unchanged GPS driver. Timing-only experiments were rejected and reverted;
no GPS firmware root-cause fix is claimed. If GPS transfer errors persist after
flashing, remove both USB and battery power before restarting. A software reset
alone does not power-cycle the external receiver. Follow-up counters are recorded
separately from the ten-node mesh soak.

After the full power cycle, the unchanged driver delivered fresh NMEA with a 3D
fix. Two consecutive captures recorded counters increasing from 132 to 2,070
cycles and 14,256 to 261,760 bytes, while cycle failures, all write failures,
short reads, recovery attempts, wakes and budget overruns remained zero. The
captures span more than four minutes with ten nodes and WDGO active. This
supports the recovery procedure; the underlying warm-reset fault remains open.
