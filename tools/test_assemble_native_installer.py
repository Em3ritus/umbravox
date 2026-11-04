import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
import zipfile

spec = importlib.util.spec_from_file_location('assembler', Path(__file__).with_name('assemble-native-installer.py'))
assembler = importlib.util.module_from_spec(spec)
spec.loader.exec_module(assembler)

class Packages(unittest.TestCase):
    def make_packages(self, root, bad_capacity=False, corrupt=False):
        packages = []
        for key in ['nm-cyd-c5-engine', 'ws-c5-35-engine', 'ws-c5-28-engine', 'xiao-c5-node']:
            node = key == 'xiao-c5-node'
            prefix = '' if node else key + '/'
            files = []
            content = {}
            for name, offset, _ in assembler.EXPECTED:
                blob = b'\x01\x02\x03\x04'
                content[name] = blob
                files.append(dict(path=name, offset=offset if node else hex(offset), sha256=hashlib.sha256(blob).hexdigest()))
            manifest = dict(environment=key, commit='a'*40, files=files)
            if node:
                manifest.update(chip='ESP32-C5', flash_size=0x1000000 if bad_capacity else 0x800000)
            else:
                manifest.update(minimum_flash_bytes=0x1000000, app_slot_bytes=0x600000)
            content['manifest.json'] = json.dumps(manifest).encode()
            content['SHA256SUMS.txt'] = ''.join(hashlib.sha256(data).hexdigest()+'  '+name+'\n' for name,data in content.items()).encode()
            if node and corrupt:
                content['firmware.bin'] = b'bad!'
            path = root/(key+'.zip')
            with zipfile.ZipFile(path, 'w') as z:
                for name,data in content.items(): z.writestr(prefix+name,data)
            packages.append(path)
        return packages

    def test_four_boards_keep_distinct_capacities_and_images(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp); out=root/'site'
            assembler.assemble(out,self.make_packages(root))
            for key,capacity in [('xiao-c5-node',0x800000),('ws-c5-35-engine',0x1000000)]:
                manifest=json.loads((out/'firmware'/key/'manifest.json').read_text())
                self.assertEqual(manifest['flashBytes'],capacity)
                self.assertEqual([p['offset'] for p in manifest['parts']],[0x2000,0x8000,0xe000,0x10000])
            for line in (out/'SITE-SHA256SUMS.txt').read_text().splitlines():
                digest,name=line.split('  ',1)
                self.assertEqual(hashlib.sha256((out/name).read_bytes()).hexdigest(),digest)

    def test_reject_missing_node_bad_capacity_and_corrupt_image(self):
        for case in ['missing','capacity','checksum']:
            with self.subTest(case=case), tempfile.TemporaryDirectory() as tmp:
                root=Path(tmp)
                packages=self.make_packages(root,case=='capacity',case=='checksum')
                if case=='missing':packages.pop()
                with self.assertRaises(ValueError):assembler.assemble(root/'site',packages)

if __name__ == '__main__':unittest.main()
