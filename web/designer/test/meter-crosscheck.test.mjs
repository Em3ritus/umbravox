// Plan 5c: tools/meter-crosscheck.mjs compares the board's pixel counts (meterprobe CSV) with the wasm replay's.
import { test, before } from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { spawnSync } from 'node:child_process';
import { pathToFileURL } from 'node:url';
import { ROOT } from './_util.mjs';

let M;
before(async () => {
  const r = spawnSync('bash', [path.join(ROOT, 'tools/build-designer-wasm.sh')], { encoding: 'utf8' });
  assert.equal(r.status, 0, r.stdout + r.stderr);
  M = await (await import(pathToFileURL(path.join(ROOT, 'web/designer/wasm/uv_preview.js')).href)).default();
});

function counts(mode, frames = 30, frameMs = 40) {
  const p = M._malloc(frames * 8);
  const n = M._uv_meter(0, 480, 320, mode, frames, frameMs, p);
  const v = new Uint32Array(M.HEAPU8.buffer, p, frames * 2).slice();
  M._free(p);
  return Array.from({ length: n }, (_, i) => ({ drawn: v[2 * i], pushed: v[2 * i + 1] }));
}

// The board's rows for the replay's own counts, pushed scaled by `scale`. The first frame of every mode run is the
// transition (MED and LOW rise over the previous card on the board): `spike` multiplies it.
function csvFor(scale, spike = 1) {
  const rows = ['board,pack,mode,drawn,pushed,us,gap'];
  const add = (mode, list, us) => list.forEach((c, i) => {
    const k = i === 0 ? spike : 1;
    rows.push(`waveshare,builtin,${mode},${c.drawn * k},${Math.round(c.pushed * scale * k)},${us},40`);
  });
  add('scan', counts(-1).slice(2), 20000);
  ['low', 'med', 'high'].forEach((m, i) => add(m, counts(i), 90000));
  return rows.join('\n') + '\n';
}

test('meter-crosscheck passes counts that match the wasm replay and fails ones that do not', () => {
  const d = fs.mkdtempSync(path.join(os.tmpdir(), 'uvx-xcheck-'));
  fs.mkdirSync(path.join(d, 'packs'));
  const tool = path.join(ROOT, 'tools/meter-crosscheck.mjs');
  fs.writeFileSync(path.join(d, 'same.csv'), csvFor(1));
  const ok = spawnSync('node', [tool, path.join(d, 'same.csv'), path.join(d, 'packs')], { encoding: 'utf8' });
  assert.equal(ok.status, 0, ok.stdout + ok.stderr);
  assert.match(ok.stdout, /builtin/);
  fs.writeFileSync(path.join(d, 'off.csv'), csvFor(2));
  const bad = spawnSync('node', [tool, path.join(d, 'off.csv'), path.join(d, 'packs')], { encoding: 'utf8' });
  assert.equal(bad.status, 1, bad.stdout + bad.stderr);
  assert.match(bad.stdout, /pushed/);
});

test('meter-crosscheck compares steady frames: a huge transition frame on the board does not fail it', () => {
  const d = fs.mkdtempSync(path.join(os.tmpdir(), 'uvx-xcheck-'));
  fs.mkdirSync(path.join(d, 'packs'));
  fs.writeFileSync(path.join(d, 'spike.csv'), csvFor(1, 10));
  const r = spawnSync('node', [path.join(ROOT, 'tools/meter-crosscheck.mjs'), path.join(d, 'spike.csv'), path.join(d, 'packs')], { encoding: 'utf8' });
  assert.equal(r.status, 0, r.stdout + r.stderr);
  assert.match(r.stdout, /p95/);
});
