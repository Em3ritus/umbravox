import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location("sdpull", Path(__file__).resolve().parents[1] / "sdpull.py")
mod = importlib.util.module_from_spec(spec)
spec.loader.exec_module(mod)

# What SDLS prints: the old %8lu column (an 8-digit size has no leading space) and the wider %10lu one.
OLD = """[  4568][E][esp32-hal-i2c-ng.c:275] i2cWrite(): slow
=== SDLS /umbravox/sessions/WD-0048 ===
     238  wifi.csv
  363559  clients.csv
16776796  probes.csv
   <dir>  sub/
0000025191 WARN loop: stalled 10788 ms, heap 51KB
=== SDLS END ===
"""
NEW = """=== SDLS / ===
       238  WD-0002.csv
  16776796  probes.csv
     <dir>  umbravox/
=== SDLS END ===
"""


class ListingTests(unittest.TestCase):
    def test_old_format_keeps_the_8_digit_size(self):
        got = {e["name"]: e for e in mod.parse_listing(OLD)}
        self.assertEqual(got["probes.csv"]["size"], 16776796)
        self.assertEqual(got["wifi.csv"]["size"], 238)
        self.assertTrue(got["sub"]["is_dir"])
        self.assertEqual(len(got), 4)                       # the diag line and the I2C chatter are not files

    def test_new_format(self):
        got = {e["name"]: e for e in mod.parse_listing(NEW)}
        self.assertEqual(got["probes.csv"]["size"], 16776796)
        self.assertTrue(got["umbravox"]["is_dir"])

    def test_names_with_spaces(self):
        got = mod.parse_listing("=== SDLS / ===\n     12  my pack.zip\n=== SDLS END ===\n")
        self.assertEqual(got[0]["name"], "my pack.zip")


CAT = """=== SDCAT /umbravox/sessions/WD-0050/probes.csv ===\r
snapshot_ms,snapshot_utc,station_mac,local_mac,channel,rssi_dbm,request_type,ssid_display,ssid_hex,first_seen_ms,last_seen_ms,observed_requests,queue_drops,table_evictions\r
91000,,AA:BB:CC:00:00:01,1,6,-60,DIRECTED,"'a,b",612C62,90000,90500,3,0,0\r
screen: dark, UI suspended\r
[dark] wifi    7  hits 56  deauth 0  heap 41732\r
92000,,AA:BB:CC:00:00:02,1,11,-70,WILDCARD,"'",,91000,91000,1,0,0\r
93000,,AA:BB:CC:00:00:03,1,11\r
=== SDCAT END ===\r
"""


class CatTests(unittest.TestCase):
    def test_body_is_between_the_markers(self):
        body = mod.cat_body(CAT)
        self.assertTrue(body[0].startswith("snapshot_ms,"))
        self.assertNotIn("=== SDCAT END ===", body)

    def test_csv_drops_console_chatter_and_torn_rows_but_keeps_quoted_commas(self):
        header, rows, dropped = mod.csv_rows(mod.cat_body(CAT))
        self.assertEqual(len(rows), 2)
        self.assertEqual(rows[0][7], "'a,b")                # a comma inside quotes is one field
        self.assertEqual(dropped, 3)                        # two chatter lines and one torn row

    def test_no_marker_means_no_body(self):
        self.assertEqual(mod.cat_body("sdlog: no such file\n"), [])


if __name__ == "__main__":
    unittest.main()
