# Waveshare flash layout and migration

The WS 3.5 and WS 2.8 environments use `partitions/ws-c5-16mb.csv`.
It requires at least 16 MiB physical flash. A 32 MiB board can use this layout;
its upper 16 MiB remains unallocated. NM and XIAO retain `huge_app.csv`.

| Region | Offset | Size |
|---|---:|---:|
| NVS settings | 0x9000 | 20 KiB |
| OTA selection | 0xe000 | 8 KiB |
| App slot 0 | 0x10000 | 6 MiB |
| App slot 1 | 0x610000 | 6 MiB |
| Reserved internal SPIFFS | 0xc10000 | 3968 KiB |
| Core dump | 0xff0000 | 64 KiB |

The second app slot reserves space for a future OTA updater. This change does
not implement OTA, change bridge version reporting, or enable phone updates.
Current recordings and theme packs are on external SD; the firmware does not
mount the internal SPIFFS region. More flash space does not add RAM/heap.

## First migration (USB, not an app-only update)

1. Identify the intended board and read its flash ID. Confirm at least 16 MiB;
   do not infer capacity from the build's `16MB` setting. Keep the known-working
   firmware and its matching old partition table for rollback.
2. Stop recording. Save the current partition table and NVS before migrating.
   If preserving any other firmware's internal filesystem matters, back it up
   separately: the old internal SPIFFS/core-dump addresses become app space.
3. Build the correct board environment. Archive its bootloader.bin,
   partitions.bin, firmware.bin, framework boot_app0.bin and checksums together.
   The post-build check validates the actual firmware.bin against app0.
4. Write the matching artifacts at bootloader **0x2000**, partition table
   **0x8000**, boot_app0 **0xe000**, firmware **0x10000**. Resetting OTA selection
   with boot_app0 selects slot 0. Do not erase the whole chip. Separate writes
   at these offsets leave NVS at 0x9000 intact; external SD is untouched.
5. Verify writes, boot and check UI, settings, touch, scanning, bridge and SD.
   Hardware acceptance must be recorded per board. A build passing is not
   hardware acceptance.

An app-only update preserves the installed partition table: it cannot enlarge
an existing 3 MiB slot. Distribute the matching table with first-install images
and label the minimum flash capacity. The existing `make-web-flasher.sh` only
packages NM targets; this change does not add a Waveshare browser installer.
A merged image spanning address zero can overwrite NVS-filled gaps: do not use
such an image for the settings-preserving migration described above.

Rollback requires the old partition table, old firmware, and compatible boot
artifacts together, not just the old app. Never restore an old internal data
partition over the new running app. Retain backups until acceptance completes.

## Space check

`python3 tools/check_app_size.py firmware.bin partitions.bin` checks the actual
binary, including headers/padding. PlatformIO runs it after generating each
firmware.bin. It rejects empty/oversize binaries or an unexpected app0 layout.
`python3 tools/test_check_app_size.py` exercises fit/exact-fit/overflow and
missing/duplicate app slots.
