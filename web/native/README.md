# Native UI browser installer

Desktop Chrome/Edge over HTTPS, using the pinned Espressif esptool-js 0.7.0
bundle served from the same origin. Source: https://github.com/espressif/esptool-js
The vendor directory retains its licenses. No remote executable CDN imports.

The installer connects to the ROM with detectChip(), not main()/runStub().
It checks ESP32-C5 and the selected target capacity (8 MiB for XIAO nodes,
16 MiB for handhelds) before enabling installation. Node applications are bounded
to their 3 MiB slot; handheld applications retain their 6 MiB bound. Exact board
selection is the user's responsibility: chip detection cannot distinguish NM
from the two Waveshare models. Firmware downloads are checked against SHA-256
before any write, then all four device regions are verified using ROM MD5.
ROM ASCII MD5 responses are normalized from esptool-js's hex representation.
No full-chip erase; separate files avoid overwriting the NVS gap with padding.
The partition table is migrated; preserve internal data before installation.

Assemble with tools/assemble-native-installer.py using the four reviewed ZIPs (NM, Waveshare 3.5, Waveshare 2.8, XIAO node).
The resulting directory is a self-contained GitHub Pages site including ZIP
fallback downloads, same-origin firmware, manifests and SITE-SHA256SUMS.txt.
Run `node --test web/native/test/*.test.mjs` before deployment.
Browser UI and mocked protocol checks do not replace physical-board acceptance.

C5 ROM compatibility: `c5-loader.mjs` uses 1024-byte download blocks, appends
the ROM encryption word to FLASH_DEFL_BEGIN and the reserved word to SPI_ATTACH,
and configures SPI flash geometry with SPI_SET_PARAMS using detected capacity.
Tests exercise the real loader constructor and pinned vendor packet builder.

Standalone CLI version and setup: [flashing tools](../../docs/FLASHING-TOOLS.md).
Python esptool versions do not apply to this JavaScript vendor bundle.

Public deployment and release maintenance: [release maintenance](../../docs/RELEASE-MAINTENANCE.md).
