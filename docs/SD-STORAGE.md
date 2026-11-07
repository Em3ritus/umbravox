# SD recordings and Files browser

New recordings use this layout:

```
/umbravox/sessions/WD-YYYYMMDD-HHMMSS/
    wifi.csv
    clients.csv
    probes.csv
    track.gpx
/umbravox/captures/captureNNNN.pcap
```

Session IDs use UTC when available, otherwise `WD-NNNN`. A timestamp collision
also falls back to a numbered session. Companion files are created only when
the corresponding feature has data to write. Raw PCAP capture has its own
start/stop lifecycle and folder.

Existing root recordings remain in place and accessible: `WD-*.csv`,
`WD-*-clients.csv`, `WD-*-probes.csv`, `WD-*.gpx`, and `capture*.pcap`.
No automatic migration, formatting, or cleanup takes place. Existing session
directories are never reused, including empty ones. Name collisions and directory
creation failures must not cause an older recording to be overwritten.

Themes remain under `/themes`; `/poi.csv`, `/diag.log`, and `/perf.csv` retain
their existing locations. This change organizes recordings, not those services.

## Browsing and deletion

Open Files from Recording settings. The native UI provides Previous, Next, and
Refresh controls, file type, session identity, size, and location (session folder,
captures, or legacy root). Delete asks for confirmation with the full path.

WiGLE, GPX, client CSV, probe CSV, and raw PCAP files are marked `IN USE` while
owned by a writer, including pending stop/rollover flushes. Both touchscreen
Delete and serial `SDDEL` check current ownership immediately before removal.
The UI freezes the requested file identity before a rescan can reorder rows.
Directories and unrelated files cannot be removed through the Files browser.

The browser keeps at most 64 files, sorted newest first by recording identity.
A single scan examines at most 512 directory entries across the supported
locations and checks a 1000 ms budget between operations. Slow SD operations
can exceed that time: this is not a hard real-time deadline. `PARTIAL` means the
scan or display capacity was limited; it does not mean files were deleted.
For a large archive, use the card on a computer or directory-specific serial
commands to access records outside the displayed subset. Card capacity is shown;
free-space calculation is deliberately avoided because it can block on FAT scans.

## Validation and rollback

Rollback baseline: `9fa2807`. Native previews cover 480x320 and 320x240.
Host tests cover path policy, legacy/new listings, bounded walks, naming collisions,
directory failures, active/draining deletion protection, paging and confirmation.
Compatibility builds cover Waveshare native/legacy and NM display/coordinator.
No hardware or SD-card migration is implied by these host checks. The storage
change must be flashed only on explicit user instruction.
