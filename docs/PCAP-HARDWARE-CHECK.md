# Couch check: AP details and PCAP

Preparation only. Flashing is a separate, explicit step. Keep the working
`c6b5e8a` baseline and the other board unchanged. Use one Waveshare 3.5-inch board,
a known-good SD card, and the existing USB connection. No GPS fix or outdoor trip
is needed. Keep the existing dwell and discovery settings unchanged for this check.

1. After the approved flash, open an AP in CODEX. Verify SSID/BSSID and security
   on page 1. Tap MORE: verify page 2 and return. Unknown metadata is acceptable;
   first-tap navigation and readable labels should remain intact.
2. Open SETTINGS, final page, RAW PCAP. It should initially read OFF. Enable it.
   Expect ON; NO CARD or an error means stop and report that message.
3. Let it capture for 60 seconds. During that minute, visit SCAN, DOMAIN, CODEX
   and MENU, scroll, and open an AP. Note missed taps, long pauses or a reset.
   If using the phone bridge, leave its connection in its usual state and note
   whether it stays connected. GPS coordinates are not required for PCAP.
4. Return to RAW PCAP and disable it. Wait for OFF (after any SAVING state).
   Record the displayed D/drop count. If serial is available, run `PCAP` to
   capture filename, written count and dropped count. Retain the serial capture
   with `[mem]` and stall lines; do not infer free heap from the build-size report.
5. Only after OFF, power down and read the SD card. Open the new
   `/captureNNNN.pcap` in Wireshark. Expect recognized IEEE 802.11 packets and
   relative timestamps advancing. Epoch-1970 dates are expected: these timestamps
   are board uptime, not wall-clock time. Some large frames may be truncated to
   the documented 1,536-byte snapshot size.
6. If the first run is clean, reconnect and repeat ON → capture → OFF once.
   Confirm a new filename and that the first file remains readable.

Pass for this bounded check: both files parse, some frames are saved, recording
stops cleanly, navigation stays usable, and no reset or sustained heap decline is
observed. Drops may be nonzero; report written and dropped counts rather than
calling a busy-channel capture lossless. An error, reset, unreadable file, or
noticeable input regression ends the check—retain the file and log before retrying.

Bring back: AP-page observations, capture duration, filenames/file sizes,
written/drop counts, UI/bridge behavior, and serial log if available. A screenshot
of Wireshark's packet list is sufficient for an initial format check. This does
not validate capture completeness, GPS timing, or 220-versus-150-ms performance.
