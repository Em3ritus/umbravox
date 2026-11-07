# UMBRAVOX XIAO ESP32-C5 node

For the Seeed Studio XIAO ESP32-C5 with 8 MB flash only. This is a headless
scanning node, not handheld or Piglet firmware. No display or GPS is required.
Use it with the UMBRAVOX native handheld firmware from the same release. Mesh protocol: version 4 (builds that speak
version 4 interoperate; a coordinator now also reports how many nodes it carries in one optional trailing byte, which
older handhelds ignore); default network ID: 0 (open). Previously stored enrollment survives these writes.

Download [umbravox-xiao-c5-node.zip](https://github.com/Em3ritus/umbravox/releases/latest/download/umbravox-xiao-c5-node.zip).

## Install in your browser

Open the [browser installer](https://em3ritus.github.io/umbravox/) in desktop
Chrome or Edge. Select **Seeed Studio XIAO ESP32-C5 — scanning node (8 MB)**,
confirm the board selection, connect its USB serial port, then install.
Wait for **Verified: all four images hashed OK**, then press RESET without
holding BOOT. The installer checks ESP32-C5 and at least 8 MB flash for this
target; handheld targets still require at least 16 MB. Select the exact board:
chip and capacity checks cannot identify every board model.

## Command-line installation

Back up any existing firmware/data you need before installing. This replaces
the bootloader, partition table and application. No full-chip erase is used.

1. Install Python and esptool 5.6.0: `python -m pip install esptool==5.6.0`.
2. Extract this ZIP and open a terminal in its folder.
3. Connect a USB data cable. If the board will not connect, hold BOOT, press
   and release RESET, then release BOOT.
4. Replace PORT below with your board's serial port (for example COM5 on
   Windows, /dev/ttyACM0 on Linux, or /dev/cu.usbmodemXXXX on macOS).
5. Run this command:

```sh
python -m esptool --chip esp32c5 --port PORT --baud 460800 --no-stub write-flash --flash-mode dio --flash-freq 80m --flash-size 8MB 0x2000 bootloader.bin 0x8000 partitions.bin 0xe000 boot_app0.bin 0x10000 firmware.bin
```

Wait for successful verification, then press RESET without holding BOOT.
The browser and command-line methods install the same four node images.

## Connect to the handheld

Power the handheld and node. Open Nodes on the handheld and use ENROLL when
joining its network. An already enrolled node ignores invitations from another
network; reset its enrollment using the original handheld first. Check the
node is present and its reports/diagnostics refresh. Set scanning mode and dwell
from the handheld's Nodes settings. GPS is unnecessary for this connection test;
location-tagged local recordings require a GPS fix at the handheld.

## Source and integrity

Built with PlatformIO environment xiao-c5-node at the commit recorded in manifest.json, using the platform
pinned in platformio.ini. The included source archive is the exact tracked source
revision. SHA256SUMS.txt covers every package file except itself. manifest.json
records image offsets, sizes and hashes. This package does not update the
handheld firmware or browser installer.

Current bench results: [XIAO node validation](XIAO-NODE-VALIDATION.md).
