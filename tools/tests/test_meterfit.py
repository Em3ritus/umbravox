import csv
import importlib.util
import json
import math
import os
import random
import subprocess
import sys
import tempfile
from pathlib import Path
import unittest

TOOL = Path(__file__).resolve().parents[1] / "meterfit.py"
spec = importlib.util.spec_from_file_location("meterfit", TOOL)
mod = importlib.util.module_from_spec(spec)
spec.loader.exec_module(mod)


SCAN = (0.0002, 0.0001, 3.0)
ALERT = (0.0006, 0.0003, 20.0)


def rows(board, scan=SCAN, alert=ALERT, packs=("builtin", "meter-small", "stress-p4", "alert-fx"), noise=0.1, seed=1,
         spike=0.0):
    """Per pack: a block of scan frames, then a block of high frames (as the board's probe walks them). `spike`
    adds that many ms to the first frame of every block: the transition frame meterfit must leave out."""
    rnd = random.Random(seed)
    out = []
    for i, pk in enumerate(packs):
        for mode, (a, b, c) in (("scan", scan), ("high", alert)):
            for k in range(40):
                d = 2000 + rnd.randint(0, 150000) // (i + 1)
                p = rnd.randint(1000, d)
                ms = a * d + b * p + c + rnd.uniform(-noise, noise) + (spike if k == 0 else 0.0)
                out.append([board, pk, mode, d, p, int(ms * 1000), 40])
    return out


def write(path, rs):
    with open(path, "w", newline="") as fh:
        w = csv.writer(fh)
        w.writerow(["board", "pack", "mode", "drawn", "pushed", "us", "gap"])
        w.writerows(rs)


def run(rs, board="waveshare"):
    with tempfile.TemporaryDirectory() as d:
        src, out = os.path.join(d, "in.csv"), os.path.join(d, "fit.json")
        write(src, rs)
        r = subprocess.run([sys.executable, str(TOOL), src, "--out", out], capture_output=True, text=True)
        j = json.loads(Path(out).read_text()) if os.path.exists(out) else None
        return r, j


def p95(xs):
    xs = sorted(xs)
    return xs[math.ceil(0.95 * len(xs)) - 1]


class FitTests(unittest.TestCase):
    def test_recovers_the_coefficients(self):
        f = mod.fit([(r[3], r[4], r[5] / 1000.0) for r in rows("waveshare") if r[2] == "scan"])
        self.assertAlmostEqual(f["a"], 0.0002, delta=0.0002 * 0.05)
        self.assertAlmostEqual(f["b"], 0.0001, delta=0.0001 * 0.05)
        self.assertAlmostEqual(f["c"], 3.0, delta=3.0 * 0.05)
        self.assertLess(f["rmsMs"], 0.1)

    def test_p95_is_nearest_rank(self):
        self.assertEqual(mod.p95(list(range(1, 21))), 19)
        self.assertEqual(mod.p95(list(range(1, 31))), 29)
        self.assertEqual(mod.p95([5.0]), 5.0)

    def test_fits_scan_and_alert_separately(self):
        r, j = run(rows("waveshare"))
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertEqual(j["version"], 2)
        ws = j["boards"]["waveshare"]
        for kind, (a, b, c) in (("scan", SCAN), ("alert", ALERT)):
            self.assertAlmostEqual(ws[kind]["a"], a, delta=a * 0.05, msg=kind)
            self.assertAlmostEqual(ws[kind]["b"], b, delta=b * 0.05, msg=kind)
            self.assertAlmostEqual(ws[kind]["c"], c, delta=c * 0.05, msg=kind)
        self.assertEqual(ws["frameMs"], 40)

    def test_limits_are_builtin_scan_p95_plus_15_and_alert_400_on_both_boards(self):
        rs = rows("waveshare") + rows("cyd-engine", seed=2)
        r, j = run(rs)
        self.assertEqual(r.returncode, 0, r.stderr)
        for b in ("waveshare", "cyd-engine"):
            steady = [x[5] / 1000.0 for i, x in enumerate(rs)
                      if x[0] == b and x[1] == "builtin" and x[2] == "scan" and rs[i - 1][:3] == x[:3]]
            L = j["boards"][b]["limits"]
            self.assertEqual(L["alertMs"], 400)
            self.assertAlmostEqual(L["scanMs"], round(p95(steady) * 1.15, 1), places=1, msg=b)

    def test_leaves_out_the_transition_frame(self):
        r, j = run(rows("waveshare", spike=500.0))
        self.assertEqual(r.returncode, 0, r.stderr)
        ws = j["boards"]["waveshare"]
        self.assertAlmostEqual(ws["alert"]["c"], ALERT[2], delta=ALERT[2] * 0.05)
        self.assertLess(ws["scan"]["rmsMs"], 0.1)
        self.assertLess(ws["limits"]["scanMs"], 100)

    def test_clamps_a_negative_pushed_slope_to_zero(self):
        r, j = run(rows("waveshare", alert=(0.0008, -0.0002, 20.0)))
        self.assertEqual(r.returncode, 0, r.stderr)
        al = j["boards"]["waveshare"]["alert"]
        self.assertEqual(al["b"], 0)
        self.assertGreater(al["a"], 0)

    def test_refuses_too_few_packs(self):
        r, j = run(rows("waveshare", packs=("builtin", "meter-small")))
        self.assertEqual(r.returncode, 2)
        self.assertIn("3", r.stderr)
        self.assertIsNone(j)

    def test_refuses_a_board_without_alert_frames(self):
        r, j = run([x for x in rows("waveshare") if x[2] == "scan"])
        self.assertEqual(r.returncode, 2)
        self.assertIn("alert", r.stderr)
        self.assertIsNone(j)

    def test_refuses_an_empty_input_and_writes_nothing(self):
        r, j = run([])
        self.assertEqual(r.returncode, 2)
        self.assertIn("no frames", r.stderr)
        self.assertIsNone(j)

    def test_refuses_when_both_slopes_are_negative(self):
        r, j = run(rows("waveshare", alert=(-0.0002, -0.0001, 400.0)))
        self.assertEqual(r.returncode, 2)
        self.assertIn("negative", r.stderr)
        self.assertIsNone(j)

    def test_a_one_board_run_does_not_drop_the_other_boards_fit(self):
        # 5c minor 5: re-measuring one board must not silently wipe the fit of the other.
        with tempfile.TemporaryDirectory() as d:
            ws, cyd, out = (os.path.join(d, n) for n in ("ws.csv", "cyd.csv", "fit.json"))
            write(ws, rows("waveshare"))
            write(cyd, rows("cyd-engine", seed=2))
            go = lambda *args: subprocess.run([sys.executable, str(TOOL), *args, "--out", out], capture_output=True,
                                              text=True)
            self.assertEqual(go(ws, cyd).returncode, 0)
            before = Path(out).read_text()
            r = go(ws)
            self.assertEqual(r.returncode, 2)
            self.assertIn("cyd-engine", r.stderr)
            self.assertIn("--drop-missing", r.stderr)
            self.assertEqual(Path(out).read_text(), before)
            r = go(ws, "--drop-missing")
            self.assertEqual(r.returncode, 0, r.stderr)
            self.assertEqual(sorted(json.loads(Path(out).read_text())["boards"]), ["waveshare"])


if __name__ == "__main__":
    unittest.main()
