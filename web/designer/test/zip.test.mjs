import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { spawnSync } from 'node:child_process';
import { makeZip } from '../js/zip.js';

const bytes = (n, seed) => Uint8Array.from({ length: n }, (_, i) => (i * 31 + seed) & 255);

test('zip round-trips through unzip -t and unzip -p', () => {
  const files = [
    { path: 'pack/theme.json', data: new TextEncoder().encode('{"format": 1}\n') },
    { path: 'pack/empty.bin', data: new Uint8Array(0) },
    { path: 'pack/one.bin', data: Uint8Array.of(0x7f) },
    { path: 'pack/sub/dir/big.bin', data: bytes(5000, 3) },
    { path: 'pack/ünï.txt', data: bytes(10, 9) },
  ];
  const zip = makeZip(files);
  assert.ok(zip instanceof Uint8Array);
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'uvx-zip-'));
  const zp = path.join(dir, 't.zip');
  fs.writeFileSync(zp, zip);
  const t = spawnSync('unzip', ['-t', zp], { encoding: 'utf8' });
  assert.equal(t.status, 0, t.stdout + t.stderr);
  for (const f of files.slice(0, 4)) {
    const p = spawnSync('unzip', ['-p', zp, f.path], { maxBuffer: 1 << 20 });
    assert.equal(p.status, 0, f.path);
    assert.deepEqual(new Uint8Array(p.stdout), f.data, f.path);
  }
  // Info-ZIP's unzip may not decode UTF-8 names in every build, so check the
  // bytes: general-purpose flag bit 11 set and the UTF-8 name in the headers.
  const dv = new DataView(zip.buffer, zip.byteOffset, zip.byteLength);
  const nameBytes = new TextEncoder().encode('pack/ünï.txt');
  const hay = Buffer.from(zip);
  assert.equal(hay.indexOf(nameBytes) > 0, true);
  const lastLocal = hay.indexOf(nameBytes) - 30;
  assert.equal(dv.getUint32(lastLocal, true), 0x04034b50);
  assert.equal(dv.getUint16(lastLocal + 6, true) & 0x0800, 0x0800);
});

test('zip is deterministic and an empty zip is valid', () => {
  const f = [{ path: 'a/b', data: bytes(100, 1) }];
  assert.deepEqual(makeZip(f), makeZip(f));
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'uvx-zip-'));
  const zp = path.join(dir, 'e.zip');
  fs.writeFileSync(zp, makeZip([]));
  assert.equal(makeZip([]).length, 22);
});
