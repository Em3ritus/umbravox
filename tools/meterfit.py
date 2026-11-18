#!/usr/bin/env python3
"""Fit the speed meter's frame time per board from tools/meterprobe.py CSVs (plan 5c):

    python3 tools/meterfit.py ws.csv cyd.csv --out web/designer/js/meter-fit.json

Per board, two least-squares fits ms ~ a*drawn + b*pushed + c: one over the scan frames, one over the alert frames
(LOW, MED, HIGH). An alert pixel costs 2-2.5x a scan pixel (the rite, cards, text), so one model over both cannot
work. The first frame after every change of pack or mode is left out: on the board MED and LOW appear over the
previous card, which the designer's replay cannot reproduce. A negative slope is clamped to 0 and the other term
refitted alone (pixels cannot cost negative time); both negative refuses the board.

The designer (web/designer/js/meter.js) applies the fits to the counts its wasm replay (ui/meter.h) produces and
judges the p95 frame (nearest rank) against the board's limits: alerts 400 ms on both boards (the alert row only
warns); scan is written here as the built-in theme's MEASURED scan p95 + 15%, which is provisional:
tools/meter-limits.mjs replaces it with the built-in theme's PREDICTED p95 + 15%, which cancels the fit's bias.

A board is refused (exit 2, nothing written) with fewer than 3 distinct packs, no alert or no built-in scan frames,
or both slopes of a fit negative: a bad fit must never reach the designer.
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

ALERT_MS = 400
SCAN_MARGIN = 1.15


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


def fit_nonneg(samples):
    """fit() with a and b kept >= 0: a negative one is clamped to 0 and the other refitted alone."""
    f = fit(samples)
    if f["a"] >= 0 and f["b"] >= 0:
        return f
    i = 0 if f["b"] < 0 else 1  # the term kept
    xs = [s[i] for s in samples]
    ys = [s[2] for s in samples]
    n = len(samples)
    mx, my = sum(xs) / n, sum(ys) / n
    sxx = sum((x - mx) ** 2 for x in xs)
    k = sum((x - mx) * (y - my) for x, y in zip(xs, ys)) / sxx if sxx else -1.0
    if k < 0:
        raise ValueError("negative slope (a=%.3g, b=%.3g, and %.3g alone): the frames do not separate drawing from "
                         "pushing" % (f["a"], f["b"], k))
    c = my - k * mx
    rms = math.sqrt(sum((k * s[i] + c - s[2]) ** 2 for s in samples) / n)
    return {"a": k if i == 0 else 0, "b": k if i == 1 else 0, "c": c, "rmsMs": rms}


def p95(xs):
    """Nearest-rank 95th percentile (the same rule as web/designer/js/meter.js)."""
    xs = sorted(xs)
    return xs[max(0, math.ceil(0.95 * len(xs)) - 1)]


def steady(rows):
    """Drop the first frame after every change of board, pack or mode (in file order)."""
    out, prev = [], None
    for r in rows:
        key = (r["board"], r["pack"], r["mode"])
        if key == prev:
            out.append(r)
        prev = key
    return out


def read_rows(paths):
    out = []
    for p in paths:
        with open(p, newline="") as fh:
            for r in csv.DictReader(fh):
                out.append({"board": r["board"], "pack": r["pack"], "mode": r["mode"], "drawn": int(r["drawn"]),
                            "pushed": int(r["pushed"]), "ms": int(r["us"]) / 1000.0, "gap": int(r["gap"])})
    return out


def board_fit(board, rows):
    packs = {r["pack"] for r in rows}
    if len(packs) < 3:
        raise ValueError("%s: %d distinct packs measured, the fit needs at least 3" % (board, len(packs)))
    out = {}
    for kind, sel in (("scan", lambda r: r["mode"] == "scan"), ("alert", lambda r: r["mode"] != "scan")):
        rs = [r for r in rows if sel(r)]
        if len(rs) < 3:
            raise ValueError("%s: no %s frames measured" % (board, kind))
        try:
            f = fit_nonneg([(r["drawn"], r["pushed"], r["ms"]) for r in rs])
        except ValueError as e:
            raise ValueError("%s %s: %s" % (board, kind, e))
        out[kind] = {"a": f["a"], "b": f["b"], "c": f["c"], "rmsMs": round(f["rmsMs"], 3), "frames": len(rs)}
    gaps = [r["gap"] for r in rows if r["mode"] == "scan" and r["gap"] > 0]
    builtin = [r["ms"] for r in rows if r["pack"] == "builtin" and r["mode"] == "scan"]
    if not builtin:
        raise ValueError("%s: no built-in theme scan frames (pack 'builtin'): the scan limit comes from them" % board)
    out["frameMs"] = int(round(statistics.median(gaps))) if gaps else 20
    out["limits"] = {"scanMs": round(p95(builtin) * SCAN_MARGIN, 1), "alertMs": ALERT_MS}
    out["frames"] = len(rows)
    out["packs"] = sorted(packs)
    return out


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
    a = ap.parse_args()
    rows = steady(read_rows(a.csv))
    boards = {}
    try:
        for b in sorted({r["board"] for r in rows}):
            boards[b] = board_fit(b, [r for r in rows if r["board"] == b])
    except ValueError as e:
        print("meterfit: %s" % e, file=sys.stderr)
        sys.exit(2)
    out = {"version": 2, "measured": datetime.date.today().isoformat(), "firmware": git_sha(), "boards": boards}
    Path(a.out).write_text(json.dumps(out, indent=2) + "\n")
    for b, f in boards.items():
        for k in ("scan", "alert"):
            m = f[k]
            print("%s %s: ms = %.4g*drawn + %.4g*pushed + %.3g (rms %.2f ms, %d frames)"
                  % (b, k, m["a"], m["b"], m["c"], m["rmsMs"], m["frames"]))
        print("%s: frame %d ms, limits (p95) scan %s / alert %s ms" % (b, f["frameMs"], f["limits"]["scanMs"],
                                                                      f["limits"]["alertMs"]))


if __name__ == "__main__":
    main()
