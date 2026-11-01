#!/usr/bin/env python3
"""Assemble the native browser installer from checksum-verified release ZIPs.
Usage: assemble-native-installer.py OUTPUT NM_ZIP WS35_ZIP WS28_ZIP
OUTPUT must be a fresh, empty directory. Never compiles or changes firmware.
All artifacts stay same-origin on Pages.
"""
import hashlib, json, pathlib, shutil, sys, zipfile

# path, flash offset, max bytes for its partition region (mirrors web/native/flash.mjs).
EXPECTED = [('bootloader.bin', 0x2000, 0x6000), ('partitions.bin', 0x8000, 0x1000),
            ('boot_app0.bin', 0xe000, 0x2000), ('firmware.bin', 0x10000, 0x600000)]

def assemble(out, packages):
    root = pathlib.Path(__file__).resolve().parents[1]
    out = pathlib.Path(out)
    if out.exists() and any(out.iterdir()):
        raise SystemExit(f'Output directory {out} is not empty; use a fresh directory')
    out.mkdir(parents=True, exist_ok=True)
    shutil.copytree(root/'web/native', out, dirs_exist_ok=True, ignore=shutil.ignore_patterns('test'))
    for key, path in zip(('nm-cyd-c5-engine', 'ws-c5-35-engine', 'ws-c5-28-engine'), packages):
        with zipfile.ZipFile(path) as archive:
            names = archive.namelist()
            manifest_name = next(n for n in names if n.endswith('/manifest.json'))
            prefix = manifest_name.rsplit('/', 1)[0] + '/'
            m = json.loads(archive.read(manifest_name))
            if m['environment'] != key or m['minimum_flash_bytes'] != 0x1000000 or m['app_slot_bytes'] != 0x600000:
                raise ValueError('Wrong board or partition layout in package')
            for line in archive.read(prefix + 'SHA256SUMS.txt').decode().splitlines():
                digest, name = line.split('  ', 1)
                if hashlib.sha256(archive.read(prefix + name)).hexdigest() != digest:
                    raise ValueError('Package checksum mismatch')
            files = m['files']
            if [f['path'] for f in files] != [name for name, _, _ in EXPECTED]:
                raise ValueError('Manifest must list exactly the four install files in order')
            dest = out/'firmware'/key
            dest.mkdir(parents=True, exist_ok=True)
            parts = []
            for (name, offset, limit), f in zip(EXPECTED, files):
                if f['path'] != name or int(f['offset'], 16) != offset:
                    raise ValueError(f'Unexpected install file path or offset: {f["path"]}')
                blob = archive.read(prefix + name)
                if hashlib.sha256(blob).hexdigest() != f['sha256']:
                    raise ValueError('Firmware checksum mismatch')
                if not blob or len(blob) % 4 or len(blob) > limit:
                    raise ValueError(f'Firmware part out of bounds: {name}')
                (dest/name).write_bytes(blob)
                parts.append({'path': name, 'offset': offset, 'bytes': len(blob),
                              'sha256': hashlib.sha256(blob).hexdigest(), 'md5': hashlib.md5(blob).hexdigest()})
            package_name = pathlib.Path(path).name
            shutil.copy2(path, out/package_name)
            manifest = {'boardKey': key, 'chip': 'ESP32-C5', 'flashBytes': 0x1000000,
                        'commit': m['commit'], 'parts': parts, 'packageUrl': package_name}
            (dest/'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    (out/'.nojekyll').touch()
    # Inventory only the files this run wrote into the fresh output tree.
    intended = sorted(p for p in out.rglob('*')
                      if p.is_file() and '.git' not in p.parts and p.name != 'SITE-SHA256SUMS.txt')
    (out/'SITE-SHA256SUMS.txt').write_text(
        ''.join(hashlib.sha256(p.read_bytes()).hexdigest() + '  ' + str(p.relative_to(out)) + '\n' for p in intended))

if __name__ == '__main__':
    if len(sys.argv) != 5:
        raise SystemExit(__doc__)
    assemble(sys.argv[1], sys.argv[2:])
