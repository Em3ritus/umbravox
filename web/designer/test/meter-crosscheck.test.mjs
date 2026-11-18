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

// A fit where every scan frame costs 20 ms and every alert frame 90 ms, whatever its pixels.
const FIT = { version: 2, boards: { waveshare: { scan: { a: 0, b: 0, c: 20 }, alert: { a: 0, b: 0, c: 90 }, frameMs: 40, limits: { scanMs: 30, alertMs: 400 } } } };

// The board's rows for the replay's own counts. scanUs / alertUs: the board's time per frame. The first frame of every
// mode run is the transition (MED and LOW rise over the previous card on the board): `spike` multiplies its time.
function csvFor(scanUs, alertUs = 90000, spike = 1) {
  const rows = ['board,pack,mode,drawn,pushed,us,gap'];
  const add = (mode, list, us) => list.forEach((c, i) => rows.push(`waveshare,builtin,${mode},${c.drawn},${c.pushed},${i === 0 ? us * spike : us},40`));
  add('scan', counts(-1).slice(2), scanUs);
  ['low', 'med', 'high'].forEach((m, i) => add(m, counts(i), alertUs));
  return rows.join('\n') + '\n';
}

function xcheck(csv) {
  const d = fs.mkdtempSync(path.join(os.tmpdir(), 'uvx-xcheck-'));
  fs.mkdirSync(path.join(d, 'packs'));
  fs.writeFileSync(path.join(d, 'fit.json'), JSON.stringify(FIT));
  fs.writeFileSync(path.join(d, 'probe.csv'), csv);
  return spawnSync('node', [path.join(ROOT, 'tools/meter-crosscheck.mjs'), path.join(d, 'fit.json'), path.join(d, 'probe.csv'), path.join(d, 'packs')], { encoding: 'utf8' });
}

test('meter-crosscheck passes predicted scan times within 15% of the board and fails ones that are not', () => {
  const ok = xcheck(csvFor(22000));
  assert.equal(ok.status, 0, ok.stdout + ok.stderr);
  assert.match(ok.stdout, /builtin scan p95: board 22\.0 ms, predicted 20\.0 ms/);
  const bad = xcheck(csvFor(40000));
  assert.equal(bad.status, 1, bad.stdout + bad.stderr);
  assert.match(bad.stdout, /FAIL waveshare builtin scan/);
});

test('meter-crosscheck reports alert rows but never fails on them (the alert row is rough)', () => {
  const r = xcheck(csvFor(20000, 300000));
  assert.equal(r.status, 0, r.stdout + r.stderr);
  assert.match(r.stdout, /info waveshare builtin high p95: board 300\.0 ms, predicted 90\.0 ms/);
});

test('meter-crosscheck compares steady frames: a huge transition frame on the board does not fail it', () => {
  const r = xcheck(csvFor(20000, 90000, 10));
  assert.equal(r.status, 0, r.stdout + r.stderr);
});

test('meter-crosscheck reads a CSV with CRLF line endings', () => {
  const r = xcheck(csvFor(22000).replace(/\n/g, '\r\n'));
  assert.equal(r.status, 0, r.stdout + r.stderr);
  assert.match(r.stdout, /builtin scan p95: board 22\.0 ms/);
});
