import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location("meterprobe", Path(__file__).resolve().parents[1] / "meterprobe.py")
mod = importlib.util.module_from_spec(spec)
spec.loader.exec_module(mod)

# What the board prints with FRAMELOG on (device/main.cpp, ui/scan_frame.h formatFrm), with other log lines between.
LOG = """[frm] d 153600 p 153600 us 98000 gap 0 a -1
[eng] fr 410 sl 1640 compose 91000us xfer 120000us maxsl 3100us rpm 210 diff 800us entry 14ms gap 22ms wifi 40 ble 3
[frm] d 19888 p 13130 us 21000 gap 41 a -1
noise [frm] d 1 p 1 us 1 gap 1 a 1
[frm] d 76800 p 76800 us 190000 gap 230 a 2
[frm] d 12 p 1"""


class ParseTests(unittest.TestCase):
    def test_complete_frame_lines_only(self):
        rows = mod.parse_frm(LOG)
        self.assertEqual(rows, [
            {"drawn": 153600, "pushed": 153600, "us": 98000, "gap": 0, "alert": -1},
            {"drawn": 19888, "pushed": 13130, "us": 21000, "gap": 41, "alert": -1},
            {"drawn": 76800, "pushed": 76800, "us": 190000, "gap": 230, "alert": 2},
        ])

    def test_trailing_complete_line_counts(self):
        self.assertEqual(len(mod.parse_frm("[frm] d 1 p 2 us 3 gap 4 a 0\n")), 1)

    def test_mode_of(self):
        self.assertEqual([mod.mode_of(a) for a in (-1, 0, 1, 2)], ["scan", "low", "med", "high"])

    def test_csv_rows(self):
        rows = mod.parse_frm(LOG)
        self.assertEqual(mod.csv_rows("waveshare", "alert-fx", rows)[2],
                         ["waveshare", "alert-fx", "high", 76800, 76800, 190000, 230])

    def test_theme_reply(self):
        self.assertEqual(mod.theme_reply("theme 14 requested (METER SMALL)"), (14, "METER SMALL"))
        self.assertIsNone(mod.theme_reply("[eng] fr 1"))
        self.assertEqual(mod.theme_range("THEME 0..15"), 15)
        self.assertIsNone(mod.theme_range("theme 3 requested (X)"))


if __name__ == "__main__":
    unittest.main()
