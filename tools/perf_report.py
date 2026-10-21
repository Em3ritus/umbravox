#!/usr/bin/env python3
"""Summarise UMBRAVOX perf logs (/perf.csv from the SD card).

Usage:  python3 -I tools/perf_report.py perf.0.csv perf.csv     (oldest first)

Prints frame cost, stalls, memory trend, capture/mesh rates and the SD cost,
then lists anything worth tuning. Standard library only.
"""
import csv
import statistics
import sys


def load(paths):
    rows = []
    for p in paths:
        with open(p, newline="") as fh:
            for r in csv.DictReader(fh):
                try:
                    rows.append({k: float(v) for k, v in r.items() if v not in (None, "")})
                except ValueError:
                    pass                      # a torn last line after a power cut
    return rows


def pct(vals, q):
    vals = sorted(vals)
    return vals[min(len(vals) - 1, int(len(vals) * q))] if vals else 0


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    rows = load(sys.argv[1:])
    if not rows:
        sys.exit("perf_report: no rows found")

    col = lambda k: [r[k] for r in rows if k in r]
    lit = [r for r in rows if not r["dark"]]                   # intervals with the screen on
    notes = []

    # A boot restarts t_s from a small number; count them.
    boots = 1 + sum(1 for a, b in zip(rows, rows[1:]) if b["t_s"] < a["t_s"])
    span_h = sum(max(0.0, b["t_s"] - a["t_s"]) if b["t_s"] >= a["t_s"] else b["t_s"]
                 for a, b in zip(rows, rows[1:])) / 3600.0
    print("%d rows, %d boot(s), ~%.1f h covered, screen on %d%% of intervals"
          % (len(rows), boots, span_h, 100 * len(lit) // len(rows)))

    if lit:
        fps, dav, dmx = [r["fps"] for r in lit], [r["draw_avg"] for r in lit], [r["draw_max"] for r in lit]
        bav = [r["blit_avg"] for r in lit]
        print("\nframes   fps median %.1f (worst %.1f) | draw avg %.0f ms, worst %.0f | blit avg %.0f ms"
              % (statistics.median(fps), min(fps), statistics.median(dav), max(dmx), statistics.median(bav)))
        if statistics.median(fps) < 20:
            notes.append("median frame rate is under 20 fps")
        if statistics.median(bav) > 20:
            notes.append("blit averages over 20 ms: the dirty regions are large")

    gaps = col("gap_max")
    slow = sum(1 for g in gaps if g > 100)
    print("stalls   worst gap between loops %.0f ms; %d of %d intervals had one over 100 ms"
          % (max(gaps), slow, len(gaps)))
    if max(gaps) > 250:
        notes.append("a stall of %.0f ms (UI froze that long)" % max(gaps))

    sd = col("sd_max")
    print("sd       worst write stall %.0f ms; %d intervals over 15 ms" % (max(sd), sum(1 for v in sd if v > 15)))
    if max(sd) > 60:
        notes.append("an SD write held the loop for %.0f ms" % max(sd))

    hf = col("heap_free")
    drift = (hf[-1] - hf[0]) / 1024.0
    perh = drift / span_h if span_h > 0.05 else 0.0
    print("memory   heap free %.0f -> %.0f KB (%+.1f KB, %+.1f KB/h) | lowest ever %.0f KB | "
          "largest block min %.0f KB | stack free min %.0f B | psram free min %.1f MB"
          % (hf[0] / 1024, hf[-1] / 1024, drift, perh, min(col("heap_min")) / 1024,
             min(col("heap_blk")) / 1024, min(col("stack_free")), min(col("psram_free")) / 1e6))
    if boots == 1 and perh < -20:
        notes.append("heap is falling %.0f KB/h: possible leak" % -perh)
    if min(col("heap_min")) < 40 * 1024:
        notes.append("heap dipped under 40 KB")
    if min(col("stack_free")) < 512:
        notes.append("loop stack nearly exhausted (%.0f B free)" % min(col("stack_free")))
    if min(col("heap_blk")) < 0.5 * min(col("heap_free")):
        notes.append("heap is fragmented: the largest free block is under half of free heap")

    print("capture  wifi %.0f fr/s median (peak %.0f) | ble %.0f adverts/s | dedup table peak %.0f%% full"
          % (statistics.median(col("wifi_fps")), max(col("wifi_fps")), statistics.median(col("ble_aps")),
             max(col("tbl_pct"))))
    if max(col("tbl_pct")) > 85:
        notes.append("the WiFi dedup table is over 85% full")

    meshy = [r for r in rows if r["nodes"] > 0]
    if meshy:
        loss = [r["loss_pct"] for r in meshy]
        print("mesh     up to %d node(s) | loss mean %.1f%%, worst %.0f%% | %.0f reports/s median"
              % (max(col("nodes")), statistics.mean(loss), max(loss), statistics.median([r["rep_s"] for r in meshy])))
        if max(loss) > 15:
            notes.append("mesh loss reached %.0f%%: consider another coordinator" % max(loss))

    print("\n" + ("worth a look:\n  - " + "\n  - ".join(notes) if notes else "nothing stands out."))


if __name__ == "__main__":
    main()
