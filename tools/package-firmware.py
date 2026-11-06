#!/usr/bin/env python3
"""Package one PlatformIO build as a release ZIP that tools/assemble-native-installer.py accepts.

    package-firmware.py ENV BUILD_DIR OUT_DIR [--commit SHA] [--boot-app0 PATH]

ENV is one of the four installable boards. BUILD_DIR is `.pio/build/ENV` (bootloader.bin, partitions.bin,
firmware.bin). The ZIP holds the four images the installer writes, manifest.json (board, commit, offsets,
hashes), SHA256SUMS.txt for every other file, and a source snapshot of the commit (git archive), so a
release can always be traced to the code that built it. The file is named umbravox-ENV.zip: a stable
name, so release links such as .../releases/latest/download/umbravox-xiao-c5-node.zip never change.
Nothing here compiles firmware, signs it or talks to the network.
"""
import argparse
import hashlib
import json
import re
import subprocess
import sys
import zipfile
from pathlib import Path

# board key -> (is a headless node, flash capacity, app slot) -- the same limits assemble-native-installer.py enforces.
BOARDS = {
    'nm-cyd-c5-engine': (False, 0x1000000, 0x600000),
    'ws-c5-35-engine':  (False, 0x1000000, 0x600000),
    'ws-c5-28-engine':  (False, 0x1000000, 0x600000),
    'xiao-c5-node':     (True,  0x800000,  0x300000),
}
IMAGES = [('bootloader.bin', 0x2000, 0x6000), ('partitions.bin', 0x8000, 0x1000),
          ('boot_app0.bin', 0xe000, 0x2000), ('firmware.bin', 0x10000, None)]
DEFAULT_BOOT_APP0 = Path.home() / '.platformio/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin'


def package(env, build_dir, boot_app0, out_dir, commit, source=None):
    """Write OUT_DIR/umbravox-ENV.zip and return its path. `source` is the source snapshot's bytes (None: none)."""
    if env not in BOARDS:
        raise ValueError('not a releasable board: %s (one of %s)' % (env, ', '.join(BOARDS)))
    if not re.fullmatch(r'[0-9a-f]{40}', commit or ''):
        raise ValueError('the commit must be a full 40-character hex SHA, not %r' % commit)
    node, capacity, app_slot = BOARDS[env]
    build_dir = Path(build_dir)
    blobs = {}
    for name, _, limit in IMAGES:
        path = Path(boot_app0) if name == 'boot_app0.bin' else build_dir / name
        if not path.is_file():
            raise ValueError('missing build output: %s' % path)
        blob = path.read_bytes()
        limit = app_slot if name == 'firmware.bin' else limit
        if not blob or len(blob) % 4 or len(blob) > limit:
            raise ValueError('%s is %d bytes: it must be non-empty, a multiple of 4 and at most %d' % (name, len(blob), limit))
        blobs[name] = blob
    files = [dict(path=name, offset=offset if node else hex(offset), sha256=hashlib.sha256(blobs[name]).hexdigest())
             for name, offset, _ in IMAGES]
    manifest = dict(environment=env, commit=commit, files=files)
    if node:
        manifest.update(chip='ESP32-C5', flash_size=capacity)
    else:
        manifest.update(chip='ESP32-C5', minimum_flash_bytes=capacity, app_slot_bytes=app_slot)
    content = dict(blobs)
    content['manifest.json'] = (json.dumps(manifest, indent=2) + '\n').encode()
    if source is not None:
        content['source-%s.tar.gz' % commit[:7]] = source
    sums = ''.join('%s  %s\n' % (hashlib.sha256(data).hexdigest(), name) for name, data in sorted(content.items()))
    content['SHA256SUMS.txt'] = sums.encode()
    out = Path(out_dir) / ('umbravox-%s.zip' % env)
    prefix = 'umbravox-%s/' % env
    with zipfile.ZipFile(out, 'w', zipfile.ZIP_DEFLATED) as z:
        for name, data in content.items():
            z.writestr(prefix + name, data)
    return out


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('env'); ap.add_argument('build_dir'); ap.add_argument('out_dir')
    ap.add_argument('--commit', help='full SHA of the source commit (default: git rev-parse HEAD)')
    ap.add_argument('--boot-app0', default=str(DEFAULT_BOOT_APP0))
    a = ap.parse_args(argv)
    commit = a.commit or subprocess.check_output(['git', 'rev-parse', 'HEAD'], text=True).strip()
    source = subprocess.check_output(['git', 'archive', '--format=tar.gz', '--prefix=umbravox-%s/' % commit[:7], commit])
    Path(a.out_dir).mkdir(parents=True, exist_ok=True)
    out = package(a.env, a.build_dir, a.boot_app0, a.out_dir, commit, source)
    print('%s  %d bytes  commit %s' % (out, out.stat().st_size, commit[:7]))
    return 0


if __name__ == '__main__':
    try:
        sys.exit(main(sys.argv[1:]))
    except ValueError as e:
        sys.exit('package-firmware: %s' % e)
