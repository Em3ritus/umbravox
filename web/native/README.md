# Native UI browser installer

Desktop Chrome/Edge over HTTPS, using the pinned Espressif esptool-js 0.7.0
bundle served from the same origin. Source: https://github.com/espressif/esptool-js
The vendor directory retains its licenses. No remote executable CDN imports.

The installer connects to the ROM with detectChip(), not main()/runStub().
It checks ESP32-C5 and at least 16 MiB flash before enabling installation. Exact board
selection is the user's responsibility: chip detection cannot distinguish NM
from the two Waveshare models. Firmware downloads are checked against SHA-256
before any write, then all four device regions are verified using ROM MD5.
ROM ASCII MD5 responses are normalized from esptool-js's hex representation.
No full-chip erase; separate files avoid overwriting the NVS gap with padding.
The partition table is migrated; preserve internal data before installation.

Assemble with tools/assemble-native-installer.py using the three reviewed ZIPs.
The resulting directory is a self-contained GitHub Pages site including ZIP
fallback downloads, same-origin firmware, manifests and SITE-SHA256SUMS.txt.
Run `node --test web/native/test/flash.test.mjs` before deployment.
Browser UI and mocked protocol checks do not replace physical-board acceptance.

C5 ROM compatibility: `c5-loader.mjs` adds the missing zero encryption word
to 16-byte FLASH_DEFL_BEGIN packets only. Tests exercise the pinned vendor
packet builder and command status handling; stub and other-chip packets stay unchanged.
