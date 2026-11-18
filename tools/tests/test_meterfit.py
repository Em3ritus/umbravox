import csv
import importlib.util
import json
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


def rows(board, a=0.0002, b=0.0001, c=3.0, packs=("builtin", "meter-small", "stress-p4", "alert-fx"), noise=0.1, seed=1):
    rnd = random.Random(seed)
    out = []
    for i, pk in enumerate(packs):
        for k in range(40):
            d = rnd.randint(2000, 150000) // (i + 1)
            p = rnd.randint(1000, d)
            ms = a * d + b * p + c + rnd.uniform(-noise, noise)
            out.append([board, pk, "scan" if k % 2 else "high", d, p, int(ms * 1000), 40])
    return out


def write(path, rs):
    with open(path, "w", newline="") as fh:
        w = csv.writer(fh)
        w.writerow(["board", "pack", "mode", "drawn", "pushed", "us", "gap"])
        w.writerows(rs)


class FitTests(unittest.TestCase):
    def test_recovers_the_coefficients(self):
        f = mod.fit([(r[3], r[4], r[5] / 1000.0) for r in rows("waveshare")])
        self.assertAlmostEqual(f["a"], 0.0002, delta=0.0002 * 0.05)
        self.assertAlmostEqual(f["b"], 0.0001, delta=0.0001 * 0.05)
        self.assertAlmostEqual(f["c"], 3.0, delta=3.0 * 0.05)
        self.assertLess(f["rmsMs"], 0.1)

    def test_writes_the_fit_with_limits(self):
        with tempfile.TemporaryDirectory() as d:
            src, out = os.path.join(d, "ws.csv"), os.path.join(d, "fit.json")
            rs = rows("waveshare")
            write(src, rs)
            r = subprocess.run([sys.executable, str(TOOL), src, "--out", out], capture_output=True, text=True)
            self.assertEqual(r.returncode, 0, r.stderr)
            j = json.loads(Path(out).read_text())
            self.assertEqual(j["version"], 1)
            ws = j["boards"]["waveshare"]
            self.assertEqual(ws["frameMs"], 40)
            self.assertEqual(ws["limits"]["alertMs"], 250)
            worst = max(r[5] for r in rs if r[1] == "builtin" and r[2] == "scan") / 1000.0
            self.assertAlmostEqual(ws["limits"]["scanMs"], round(worst * 1.15, 1), places=1)
            self.assertNotIn("cyd-engine", j["boards"])

    def test_cyd_scan_limit_is_14_unless_given(self):
        with tempfile.TemporaryDirectory() as d:
            src, out = os.path.join(d, "cyd.csv"), os.path.join(d, "fit.json")
            write(src, rows("cyd-engine"))
            subprocess.run([sys.executable, str(TOOL), src, "--out", out], check=True, capture_output=True)
            self.assertEqual(json.loads(Path(out).read_text())["boards"]["cyd-engine"]["limits"]["scanMs"], 14)
            subprocess.run([sys.executable, str(TOOL), src, "--out", out, "--cyd-scan-ms", "30"], check=True, capture_output=True)
            self.assertEqual(json.loads(Path(out).read_text())["boards"]["cyd-engine"]["limits"]["scanMs"], 30)

    def test_refuses_too_few_packs(self):
        with tempfile.TemporaryDirectory() as d:
            src, out = os.path.join(d, "ws.csv"), os.path.join(d, "fit.json")
            write(src, rows("waveshare", packs=("builtin", "meter-small")))
            r = subprocess.run([sys.executable, str(TOOL), src, "--out", out], capture_output=True, text=True)
            self.assertEqual(r.returncode, 2)
            self.assertIn("3", r.stderr)
            self.assertFalse(os.path.exists(out))

    def test_refuses_a_negative_slope(self):
        with tempfile.TemporaryDirectory() as d:
            src, out = os.path.join(d, "ws.csv"), os.path.join(d, "fit.json")
            write(src, rows("waveshare", a=-0.0002, c=40.0))
            r = subprocess.run([sys.executable, str(TOOL), src, "--out", out], capture_output=True, text=True)
            self.assertEqual(r.returncode, 2)
            self.assertIn("negative", r.stderr)
            self.assertFalse(os.path.exists(out))


if __name__ == "__main__":
    unittest.main()
