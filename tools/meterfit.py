#!/usr/bin/env python3
"""Fit the speed meter's frame time per board from tools/meterprobe.py CSVs (plan 5c):

    python3 tools/meterfit.py ws.csv cyd.csv --out web/designer/js/meter-fit.json [--cyd-scan-ms 14]

Per board, least squares over every frame of every pack: ms ~ a*drawn + b*pushed + c. The designer
(web/designer/js/meter.js) applies it to the counts its wasm replay (ui/meter.h) produces, and judges the worst
frame against the board's limits: alerts 250 ms on both boards; scan 14 ms on the CYD engine build (or
--cyd-scan-ms); scan on the Waveshare = the built-in theme's worst measured scan frame + 15%.

A board is refused (exit 2, nothing written) with fewer than 3 distinct packs or a negative slope: either means
the measurements cannot separate drawing from pushing, and a bad fit must never reach the designer.
"""
import argparse
import csv
import datetime
import json
import math
import statistics
import subprocess
import sys
from pathlib import Path

ALERT_MS = 250
CYD_SCAN_MS = 14
WS_SCAN_MARGIN = 1.15


def _solve3(m, v):
    """Gaussian elimination with partial pivoting for a 3x3 system."""
    a = [row[:] + [v[i]] for i, row in enumerate(m)]
    for col in range(3):
        piv = max(range(col, 3), key=lambda r: abs(a[r][col]))
        if abs(a[piv][col]) < 1e-12:
            raise ValueError("singular: the frames do not vary enough in pixels drawn and pushed")
        a[col], a[piv] = a[piv], a[col]
        for r in range(col + 1, 3):
            f = a[r][col] / a[col][col]
            for k in range(col, 4):
                a[r][k] -= f * a[col][k]
    x = [0.0, 0.0, 0.0]
    for r in (2, 1, 0):
        x[r] = (a[r][3] - sum(a[r][k] * x[k] for k in range(r + 1, 3))) / a[r][r]
    return x


def fit(samples):
    """samples: [(drawn, pushed, ms)] -> {a, b, c, rmsMs}. Normal equations, centred for conditioning."""
    n = len(samples)
    md = sum(s[0] for s in samples) / n
    mp = sum(s[1] for s in samples) / n
    rows = [(s[0] - md, s[1] - mp, 1.0, s[2]) for s in samples]
    m = [[sum(r[i] * r[j] for r in rows) for j in range(3)] for i in range(3)]
    v = [sum(r[i] * r[3] for r in rows) for i in range(3)]
    a, b, c0 = _solve3(m, v)
    c = c0 - a * md - b * mp
    rms = math.sqrt(sum((a * s[0] + b * s[1] + c - s[2]) ** 2 for s in samples) / n)
    return {"a": a, "b": b, "c": c, "rmsMs": rms}


def read_rows(paths):
    out = []
    for p in paths:
        with open(p, newline="") as fh:
            for r in csv.DictReader(fh):
                out.append({"board": r["board"], "pack": r["pack"], "mode": r["mode"], "drawn": int(r["drawn"]),
                            "pushed": int(r["pushed"]), "ms": int(r["us"]) / 1000.0, "gap": int(r["gap"])})
    return out


def board_fit(board, rows, cyd_scan_ms):
    packs = {r["pack"] for r in rows}
    if len(packs) < 3:
        raise ValueError("%s: %d distinct packs measured, the fit needs at least 3" % (board, len(packs)))
    f = fit([(r["drawn"], r["pushed"], r["ms"]) for r in rows])
    if f["a"] < 0 or f["b"] < 0:
        raise ValueError("%s: negative slope (a=%.3g, b=%.3g): the measurements do not separate drawing from pushing"
                         % (board, f["a"], f["b"]))
    gaps = [r["gap"] for r in rows if r["mode"] == "scan" and r["gap"] > 0]
    frame_ms = int(round(statistics.median(gaps))) if gaps else 20
    if board == "waveshare":
        builtin = [r["ms"] for r in rows if r["pack"] == "builtin" and r["mode"] == "scan"]
        if not builtin:
            raise ValueError("waveshare: no built-in theme scan frames (pack 'builtin'): the scan limit comes from them")
        scan_ms = round(max(builtin) * WS_SCAN_MARGIN, 1)
    else:
        scan_ms = cyd_scan_ms
    return {"a": f["a"], "b": f["b"], "c": f["c"], "rmsMs": round(f["rmsMs"], 3), "frameMs": frame_ms,
            "limits": {"scanMs": scan_ms, "alertMs": ALERT_MS}, "frames": len(rows), "packs": sorted(packs)}


def git_sha():
    try:
        return subprocess.run(["git", "rev-parse", "--short", "HEAD"], capture_output=True, text=True,
                              cwd=Path(__file__).resolve().parent).stdout.strip() or "unknown"
    except OSError:
        return "unknown"


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("csv", nargs="+")
    ap.add_argument("--out", required=True)
    ap.add_argument("--cyd-scan-ms", type=float, default=CYD_SCAN_MS)
    a = ap.parse_args()
    rows = read_rows(a.csv)
    cyd = int(a.cyd_scan_ms) if float(a.cyd_scan_ms).is_integer() else a.cyd_scan_ms
    boards = {}
    try:
        for b in sorted({r["board"] for r in rows}):
            boards[b] = board_fit(b, [r for r in rows if r["board"] == b], cyd)
    except ValueError as e:
        print("meterfit: %s" % e, file=sys.stderr)
        sys.exit(2)
    out = {"version": 1, "measured": datetime.date.today().isoformat(), "firmware": git_sha(), "boards": boards}
    Path(a.out).write_text(json.dumps(out, indent=2) + "\n")
    for b, f in boards.items():
        print("%s: ms = %.4g*drawn + %.4g*pushed + %.3g (rms %.2f ms, %d frames), frame %d ms, limits scan %s / alert %s ms"
              % (b, f["a"], f["b"], f["c"], f["rmsMs"], f["frames"], f["frameMs"], f["limits"]["scanMs"], f["limits"]["alertMs"]))


if __name__ == "__main__":
    main()
