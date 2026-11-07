"""Tests for tools/package-firmware.py: its zips must be exactly what tools/assemble-native-installer.py accepts."""
import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
import zipfile

def load(name):
    spec = importlib.util.spec_from_file_location(name.replace('-', '_'), Path(__file__).with_name(name + '.py'))
    mod = importlib.util.module_from_spec(spec); spec.loader.exec_module(mod); return mod

packager = load('package-firmware')
assembler = load('assemble-native-installer')
KEYS = ['nm-cyd-c5', 'ws-c5-35-engine', 'ws-c5-28-engine', 'xiao-c5-node']
COMMIT = 'c' * 40

class Packaging(unittest.TestCase):
    def build_dir(self, root, key, firmware=b'\x01\x02\x03\x04' * 8):
        d = root / ('build-' + key); d.mkdir()
        (d / 'bootloader.bin').write_bytes(b'\xaa\xbb\xcc\xdd' * 4)
        (d / 'partitions.bin').write_bytes(b'\x11\x22\x33\x44' * 2)
        (d / 'firmware.bin').write_bytes(firmware)
        return d

    def boot_app0(self, root):
        p = root / 'boot_app0.bin'; p.write_bytes(b'\x00\x01\x02\x03' * 4); return p

    def all_packages(self, root):
        out = root / 'zips'; out.mkdir()
        boot = self.boot_app0(root)
        return [packager.package(k, self.build_dir(root, k), boot, out, COMMIT, source=b'src-' + k.encode()) for k in KEYS]

    def test_the_four_packages_assemble_into_the_installer_site(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp); zips = self.all_packages(root); site = root / 'site'
            assembler.assemble(site, zips)                      # the real consumer; it raises on anything off
            for k in KEYS:
                m = json.loads((site / 'firmware' / k / 'manifest.json').read_text())
                self.assertEqual(m['commit'], COMMIT)
                self.assertEqual([p['offset'] for p in m['parts']], [0x2000, 0x8000, 0xe000, 0x10000])
            self.assertTrue((site / 'SITE-SHA256SUMS.txt').exists())

    def test_names_are_stable_so_release_links_never_change(self):
        with tempfile.TemporaryDirectory() as tmp:
            names = sorted(p.name for p in self.all_packages(Path(tmp)))
            self.assertEqual(names, sorted('umbravox-%s.zip' % k for k in KEYS))

    def test_a_package_carries_its_source_and_checksums_for_every_file(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp); z = zipfile.ZipFile(self.all_packages(root)[3])    # the XIAO node
            names = set(n.split('/', 1)[-1] for n in z.namelist())
            self.assertTrue({'bootloader.bin', 'partitions.bin', 'boot_app0.bin', 'firmware.bin', 'manifest.json', 'SHA256SUMS.txt'} <= names)
            self.assertTrue(any(n.startswith('source-') for n in names))
            prefix = next(n for n in z.namelist() if n.endswith('manifest.json'))[:-len('manifest.json')]
            sums = dict(reversed(l.split('  ', 1)) for l in z.read(prefix + 'SHA256SUMS.txt').decode().splitlines())
            self.assertNotIn('SHA256SUMS.txt', sums)
            for n in names - {'SHA256SUMS.txt'}:
                self.assertEqual(sums[n], hashlib.sha256(z.read(prefix + n)).hexdigest())

    def test_refuses_what_the_installer_would_refuse(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp); out = root / 'o'; out.mkdir(); boot = self.boot_app0(root)
            with self.assertRaises(ValueError):                                   # not a releasable board
                packager.package('nm-cyd-c5-engine', self.build_dir(root, 'a'), boot, out, COMMIT)
            with self.assertRaises(ValueError):                                   # not a 40-hex commit
                packager.package('xiao-c5-node', self.build_dir(root, 'b'), boot, out, 'main')
            with self.assertRaises(ValueError):                                   # bigger than the node's 3 MiB app slot
                packager.package('xiao-c5-node', self.build_dir(root, 'c', b'\x00' * (0x300000 + 4)), boot, out, COMMIT)
            with self.assertRaises(ValueError):                                   # the CYD's original-UI build has a 3 MiB app slot
                packager.package('nm-cyd-c5', self.build_dir(root, 'f', b'\x00' * (0x300000 + 4)), boot, out, COMMIT)
            packager.package('ws-c5-35-engine', self.build_dir(root, 'g', b'\x00' * (0x300000 + 4)), boot, out, COMMIT)   # the engine boards have 6 MiB
            with self.assertRaises(ValueError):                                   # not a multiple of 4
                packager.package('ws-c5-35-engine', self.build_dir(root, 'd', b'\x00' * 6), boot, out, COMMIT)
            missing = self.build_dir(root, 'e'); (missing / 'partitions.bin').unlink()
            with self.assertRaises(ValueError):                                   # a build output is missing
                packager.package('ws-c5-35-engine', missing, boot, out, COMMIT)

if __name__ == '__main__':
    unittest.main()
