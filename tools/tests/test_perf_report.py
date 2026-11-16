import importlib.util
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location("perf_report", Path(__file__).resolve().parents[1] / "perf_report.py")
mod = importlib.util.module_from_spec(spec)
spec.loader.exec_module(mod)

HEADER = ("t_s,dark,fps,draw_avg,draw_max,blit_avg,blit_max,gap_max,sd_max,heap_free,heap_min,heap_blk,psram_free,"
          "stack_free,wifi_fps,ble_aps,wifi_n,ble_n,tbl_pct,nodes,rep_s,loss_pct,camp,fix,theme,sweep,rec")


def row(t, dark=0, wifi=4, ble=1, gap=20, rec=0):
    return "%d,%d,30.0,20,40,0,0,%d,5,50000,45000,30000,3000000,3000,%d,%d,10,10,1,0,0,0,0,0,2,0,%d" % (t, dark, gap, wifi, ble, rec)


# What `SDCAT /perf.csv` over the serial console really delivers: a banner, the file, console chatter in the middle, an end marker.
PULLED = "\n".join([
    "[ 4565][E][esp32-hal-i2c-ng.c:275] i2cWrite(): slow",
    "=== SDCAT /perf.csv ===",
    HEADER,
    row(10), row(20, ble=0), row(30, dark=1, ble=3, rec=1),
    "0000025191 WARN loop: stalled 10788 ms, heap 51KB",
    row(40, ble=2, rec=1),
    row(10, wifi=8, ble=5, gap=900), row(20, wifi=8, ble=5),            # a second boot
    "=== SDCAT END ===",
]) + "\n"


class PerfReportTests(unittest.TestCase):
    def load(self, text):
        with tempfile.TemporaryDirectory() as d:
            p = Path(d) / "perf.csv"
            p.write_text(text)
            return mod.load([str(p)])

    def test_pulled_file_with_console_noise_loads(self):
        rows = self.load(PULLED)
        self.assertEqual([r["t_s"] for r in rows], [10, 20, 30, 40, 10, 20])

    def test_boots_are_split_where_t_s_restarts(self):
        boots = mod.split_boots(self.load(PULLED))
        self.assertEqual([len(b) for b in boots], [4, 2])

    def test_per_boot_summary(self):
        a, b = [mod.boot_summary(x) for x in mod.split_boots(self.load(PULLED))]
        self.assertEqual(a["rows"], 4)
        self.assertAlmostEqual(a["ble_mean"], 1.5)
        self.assertEqual(a["rec_rows"], 2)
        self.assertEqual(a["dark_pct"], 25)
        self.assertEqual(b["gap_max"], 900)
        self.assertAlmostEqual(b["wifi_mean"], 8.0)


if __name__ == "__main__":
    unittest.main()
