# Point of interest and navigation

Native footer order: DOMAIN, CODEX, MENU, POI, REC. Tap the theme name / HOME in the header to return to SCAN. MENU retains its SCAN tile. REC is muted without a box while idle, STOP is red and underlined while recording, and SAVING is amber. Existing no-card and unwritten-row feedback is retained.

POI works independently of recording. A tap snapshots the current position and timestamp into one fixed pending record; it does not stop capture or start a recording session. The next main-loop service, after the ordinary logger closes its append handle, appends to `/poi.csv`. No task, dynamic allocation, or extra GPS polling is introduced.

A valid position must be strictly younger than the existing GNSS 5000 ms freshness threshold. Missing/stale fixes show NO FIX and write nothing. A missing card shows NO SD. Successful verified close shows SAVED; write/open/close failures show ERROR. Feedback returns to POI after four seconds. Repeated taps within one second, or while a mark is pending, do not queue duplicates.

CSV columns: `gps_utc_epoch,uptime_ms,latitude,longitude,fix_age_ms,recording_file`. Coordinates retain seven decimal places. If GNSS UTC is not yet available, the epoch column is empty and uptime is retained; a wall-clock timestamp is never invented. Recording-file reference is empty while idle. Session names are CSV-escaped. This is a separate file, not a fabricated WiGLE AP row.

The append path uses the existing verified close. A failed write/close attempts truncation to the pre-append size. If truncation also fails, further POI writes are blocked until reboot to avoid appending behind known partial data. Power-loss recovery of a preexisting partial CSV and user-editable marker names are not implemented. One short append is serviced per mark; actual SD filesystem latency is not a hard real-time bound.

Host coverage exercises fresh/stale boundaries, a frozen position, absent/card-removal cases, CSV quoting, failed write/close and rollback, all native screens' Home/POI hit geometry, and footer-only feedback repaint/clearing at both sizes. Hardware acceptance of writing the POI file remains separate from these checks.
