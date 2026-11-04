# Internal RAM headroom

Baseline: `4546958`, retained in history. No hardware flash accompanies this change.

## Changes

- The Waveshare native engine allocates its legacy animation/orientation canvas
  in PSRAM at startup. Allocation failure is explicit; it does not silently consume
  internal RAM. NM and the default renderer retain their internal canvas.
- The native build excludes the obsolete single-screen SCAN adapter. The active
  all-screen engine and its bounded strip buffer remain.
- Arduino NimBLE releases its temporary advertiser after the callback. Umbravox
  still copies observations into its own PSRAM table. The passive scan configuration
  and duplicate callbacks are unchanged. Routine cache-recycle restarts are skipped;
  stopped-scan recovery and intentional BLE-off behavior are retained.
- Native-engine serial diagnostics report internal free/minimum/largest-block,
  DMA-capable internal free, PSRAM free, and loop stack high-water mark, in bytes.

## BLE integration

The pinned Arduino BLE library already implements callback-only result storage,
but does not expose its private result limit. `tools/ble_callback_library.py`
stages a licensed copy beneath each environment's build directory and adds only
the public setter. PlatformIO resolves that copy before the framework library.
Installed BLE sources are not patched. API or callback-release drift fails the
build and requires review. The same integration applies to the NM and Waveshare
full firmware builds.

The callback must finish copying needed data before returning; no retained
pointer may refer to the temporary BLE advertiser. The current callback copies
the name, address and observation fields into application-owned storage.

## Evidence and limits

- WS native static internal RAM: **134,408 -> 94,752 bytes**, saving **39,656 bytes**.
  This is the build's data/BSS measurement, not a measured runtime heap increase.
- ELF inspection: WS canvas pointer is 4 bytes; NM canvas remains 38,404 bytes.
- `ws-c5-35-engine` and `nm-cyd-c5` compile successfully.
- `make -C sim -j4 test`: 79 host test programs and five Python build-script tests
  pass. The existing theme-parser invalid-enum sanitizer diagnostic remains at
  `ui/themepack.cpp:436`; a successful suite exit does not mean sanitizer-clean.
- The Python tests cover staging, preservation of installed sources/licenses,
  incremental behavior and rejection of incompatible library source. They do not
  simulate the radio or prove live BLE discovery behavior.

After an explicitly authorized flash, compare internal free/minimum/largest-block
against the baseline under WiFi+BLE discovery, recording and screen navigation.
Check that BLE unique/advertisement counts continue increasing, idle animation
still works, and heap reaches a stable range during a sustained run. NM runtime
performance and hardware compatibility remain for its own board validation.

## RSSI list ranking

The native Waveshare lists now rank globally by descending snapshot RSSI before
pagination (WiFi, 5 GHz, BLE and located WiFi). Equal values use stable table-slot
order. Refresh is at most once per second for the current filter; a filter change
or WiFi table generation change refreshes immediately. Displayed RSSI comes from
the same ranking snapshot; details continue resolving by address, not row index.
NM retains its legacy list/detail ordering contract.

Ranking buffers allocate lazily in PSRAM: 128 KiB for 32,768 WiFi slots and 16 KiB
for 4,096 BLE slots. They do not fall back to internal RAM. Allocation failure
produces an empty list rather than dereferencing a null buffer. Ranking uses
immutable copied scores, so live callback updates cannot invalidate the sort
comparator. Full-capacity ordering is host-tested; on-device sort latency and
scroll/tap behavior still need validation after flashing.
