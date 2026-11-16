import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location("bootprobe", Path(__file__).resolve().parents[1] / "bootprobe.py")
mod = importlib.util.module_from_spec(spec)
spec.loader.exec_module(mod)

# The engine prints this every 10 s (device/main.cpp). rpm is the share of wall time spent rendering, per mille.
LOG = """[eng] fr 410 sl 1640 compose 91000us xfer 120000us maxsl 3100us rpm 210 diff 800us entry 14ms gap 22ms wifi 40 ble 3
[touch] maxgap 52ms deadline-misses 19 dropped 0
[mem] internal 52872 min 48172 largest 34804 dma 36956 psram 3178912 loop-stack 3612 bytes
[eng] fr 420 sl 1700 compose 99000us xfer 130000us maxsl 5200us rpm 250 diff 900us entry 20ms gap 61ms wifi 55 ble 1
[eng] fr 400 sl 1600 compose 95000us xfer 125000us maxsl 4100us rpm 230 diff 850us entry 15ms gap 30ms wifi 52 ble 2
"""


class EngineStatsTests(unittest.TestCase):
    def test_summarises_the_engine_lines(self):
        s = mod.engine_stats(LOG)
        self.assertEqual(s["lines"], 3)
        self.assertEqual(s["frames_median"], 410)
        self.assertEqual(s["rpm_median"], 230)
        self.assertEqual(s["rpm_max"], 250)
        self.assertEqual(s["maxslice_us"], 5200)
        self.assertEqual(s["gap_ms_max"], 61)
        self.assertEqual(s["compose_us_median"], 95000)

    def test_none_when_the_screen_was_dark_or_the_build_is_not_the_engine(self):
        self.assertIsNone(mod.engine_stats("[scan ] draw 12 blit 3 ms\nnothing else\n"))

    def test_torn_line_is_ignored(self):
        s = mod.engine_stats(LOG + "[eng] fr 41")
        self.assertEqual(s["lines"], 3)


if __name__ == "__main__":
    unittest.main()
