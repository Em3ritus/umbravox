import importlib.util
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location("ble_local", Path(__file__).resolve().parents[1] / "ble_callback_library.py")
mod = importlib.util.module_from_spec(spec)
spec.loader.exec_module(mod)
HEADER = mod.ANCHOR + "\n#endif\nprivate:\nuint8_t m_maxResults;\n"
CALLBACK = "pScan->m_pAdvertisedDeviceCallbacks->onResult(*advertisedDevice);"
RELEASE = "if (pScan->m_maxResults == 0 && advertisedDevice->m_callbackSent) {\n        pScan->erase(advertisedAddress);"
SOURCE = "int BLEScan::handleGAPEvent(\n" + CALLBACK + "\n" + CALLBACK + "\n" + RELEASE + "\ncase BLE_GAP_EVENT_DISC_COMPLETE:"


class LocalLibraryTests(unittest.TestCase):
    def test_adds_only_public_setter(self):
        output = mod.patched_header(HEADER, SOURCE)
        self.assertEqual(output.replace(mod.SETTER, mod.ANCHOR), HEADER)
        self.assertIn("void setMaxResults(uint8_t count)", output)

    def test_api_drift_rejected(self):
        for header in (HEADER.replace("setDuplicateFilter", "changed"), HEADER + mod.ANCHOR, HEADER + "void setMaxResults("):
            with self.assertRaises(RuntimeError):
                mod.patched_header(header, SOURCE)

    def test_release_drift_rejected(self):
        for source in (SOURCE.replace("m_callbackSent", "changed"), SOURCE.replace(CALLBACK, ""),
                       SOURCE.replace(RELEASE, "") + RELEASE):
            with self.assertRaises(RuntimeError):
                mod.patched_header(HEADER, source)

    def test_release_before_callback_rejected(self):
        source = SOURCE.replace(RELEASE, "").replace(CALLBACK, RELEASE + "\n" + CALLBACK, 1)
        with self.assertRaises(RuntimeError):
            mod.patched_header(HEADER, source)

    def test_staging_never_changes_installed_library(self):
        with tempfile.TemporaryDirectory() as tmp:
            original, staged = Path(tmp)/"installed", Path(tmp)/"build"
            (original/"src").mkdir(parents=True)
            (original/"src/BLEScan.h").write_text(HEADER)
            (original/"src/BLEScan.cpp").write_text(SOURCE)
            (original/"LICENSE").write_text("preserve license")
            result = mod.stage_library(original, staged)
            self.assertEqual((original/"src/BLEScan.h").read_text(), HEADER)
            self.assertEqual((result/"BLEScan.cpp").read_text(), SOURCE)
            self.assertEqual((staged/"LICENSE").read_text(), "preserve license")
            before = (result/"BLEScan.h").stat().st_mtime_ns
            (result/"obsolete.cpp").write_text("obsolete")
            mod.stage_library(original, staged)
            self.assertFalse((result/"obsolete.cpp").exists())
            self.assertEqual((result/"BLEScan.h").stat().st_mtime_ns, before)


if __name__ == "__main__":
    unittest.main()
