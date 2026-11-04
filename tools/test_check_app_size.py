import struct
import tempfile
import unittest
from pathlib import Path
from check_app_size import check


class AppSizeTest(unittest.TestCase):
    def test_size_boundaries(self):
        with tempfile.TemporaryDirectory() as tmp:
            image, table = Path(tmp)/'firmware.bin', Path(tmp)/'partitions.bin'
            table.write_bytes(struct.pack('<HBBII16sI', 0x50AA, 0, 0x10, 0x10000, 128, b'app0', 0))
            for size in (1, 127, 128):
                image.write_bytes(bytes(size))
                self.assertEqual(check(image, table), (size, 128))
            for size in (0, 129):
                image.write_bytes(bytes(size))
                with self.assertRaises(ValueError):
                    check(image, table)
            valid = table.read_bytes()
            wrong_offset = struct.pack('<HBBII16sI', 0x50AA, 0, 0x10, 0x20000, 128, b'app0', 0)
            for bad in (b'', b'bad table', valid*2, wrong_offset):
                table.write_bytes(bad)
                with self.assertRaises(ValueError):
                    check(image, table)


if __name__ == '__main__':
    unittest.main()
