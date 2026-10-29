# Engineering lessons

Short, evidence-backed rules learned on this project. Add one only with the evidence that taught it; delete any that stop
being true. This file is technical (code, hardware, tooling); how we work is kept elsewhere.

| # | Lesson | Evidence | Check that catches it |
|---|---|---|---|
| 1 | **Host tests do not see the C5.** Flash and measure after every structural change. | Draw time rose 11 -> 15 ms after the canvas refactor (runtime stride, per-pixel clip, a real `div` per pixel); only the unit showed it. | `tools/bootprobe.py` frame timing; `make -C sim bench` |
| 2 | **Free-space queries on FAT are a stall.** `SD.usedBytes/totalBytes` walk the FAT. | 8,432 ms at boot on a 29 GB card. | grep for `usedBytes|totalBytes|f_getfree` must be empty |
| 3 | **Internal RAM is the scarce resource; PSRAM is not.** | Boot heap fell 61 -> 41 KB over a day of features; moving about 36 KB of buffers to PSRAM restored 77 KB. | boot heap line in `bootprobe.py`; floor 55 KB |
| 4 | **The boot panic depends on the exact binary.** | `s_psram_chip_init` illegal-instruction panic on 66dc86c and 862b4e1, clean on 29cf577 and da32447 (14-byte difference). | 12-reset probe on the exact build you ship |
| 5 | **Unsigned time compares break at the wrap and on timestamps from the future.** | Row-hold release bug; twin-budget refill via a backwards timestamp; millis wrap. | use `(int32_t)(now - then)`; test wrap and future stamps |
| 6 | **Anything that caps or budgets an attacker-reachable resource needs an adversarial-drain test.** | Rollover deferral under a beacon flood; probe-limit evasion; shared twin-scan budget drain. | ask "what if the attacker spends this budget first?" |
| 7 | **A rewrite-the-whole-file logger stalls as the file grows.** Append in bounded batches. | 1 s at 2.5k rows to 64 s at 8k+; the unit froze and the power was pulled. | `sd_max` column in perf.csv stays flat |
| 8 | **Opening the CH340 port resets the board.** Wait for the banner before sending. | Test commands sent into a booting unit were lost. | `tools/bootprobe.py` / `tools/sdput.py` handle it |
| 9 | **Arduino's SD flush ignores fsync errors.** Verify the size after a write. | Review of the append logger. | size check after close in the logger |
| 10 | **Check what the checksum in a "known good" fixture really is.** | A brief's `$GPRMC...*53` was wrong; correct is `*73`. | compute the NMEA XOR in the test |
| 11 | **A shared gitignored baseline goes stale silently.** Keep real copies per worktree and refresh deliberately. | All 9 regress images differed on an untouched base. | `make -C sim regress` against a known-fresh copy |
| 12 | **Write the test first and watch it fail**: several fixes were found by tests that "never ran red". | Reviewer mutation runs on the FILES and GPS work caught tests that did not bite. | mutate the fix, expect failures |
