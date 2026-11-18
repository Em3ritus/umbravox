// Plan 5c: the speed meter. Counts from the device code in wasm (uv_meter), turned into ms with the boards' fits
// (js/meter-fit.json, tools/meterfit.py: one model for scan frames, one for alert frames), judged on the p95 frame.
import { test, before } from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { spawnSync } from 'node:child_process';
import { pathToFileURL } from 'node:url';
import { ROOT } from './_util.mjs';
import { loadFit, meterRows, measureAll, p95 } from '../js/meter.js';
import { createPreview } from '../js/preview.js';
import { makeTemplate } from '../js/model.js';

// Scan: 1 us per pixel drawn (ms = drawn / 1000). Alert: 2 us per pixel drawn (ms = drawn / 500).
// Limits: scan 10 ms, alert 400 ms.
const BOARD = (frameMs) => ({ scan: { a: 0.001, b: 0, c: 0 }, alert: { a: 0.002, b: 0, c: 0 }, frameMs, limits: { scanMs: 10, alertMs: 400 } });
const FIT = { version: 2, boards: { waveshare: BOARD(40), 'cyd-engine': BOARD(50) } };
const fr = (...drawn) => drawn.map((d) => ({ drawn: d, pushed: 0 }));
const many = (n, d) => fr(...Array(n).fill(d));
const one = (scanDrawn, alertDrawn = 1000) => ({ waveshare: { scan: fr(99999, 99999, scanDrawn), low: fr(alertDrawn), med: fr(1), high: fr(1) } });

test('loadFit takes a good version 2 fit and refuses a broken one', () => {
  assert.equal(loadFit(FIT).ok, true);
  assert.equal(loadFit({ ...FIT, version: 1 }).ok, false);
  const nan = structuredClone(FIT); nan.boards.waveshare.alert.c = NaN;
  assert.equal(loadFit(nan).ok, false);
  const neg = structuredClone(FIT); neg.boards.waveshare.scan.a = -0.001;
  const r = loadFit(neg);
  assert.equal(r.ok, false); assert.match(r.why, /waveshare/);
  const noAlert = structuredClone(FIT); delete noAlert.boards['cyd-engine'].alert;
  assert.match(loadFit(noAlert).why, /cyd-engine/);
  assert.equal(loadFit(null).ok, false);
});

test('p95 is the nearest-rank 95th percentile', () => {
  assert.equal(p95([5]), 5);
  assert.equal(p95(Array.from({ length: 20 }, (_, i) => i + 1)), 19);
  assert.equal(p95(Array.from({ length: 30 }, (_, i) => 30 - i)), 29);
  assert.equal(p95([]), 0);
});

test('amber from 80%, over past 100%', () => {
  const st = (d) => meterRows(FIT, one(d)).rows.find((r) => r.board === 'waveshare' && r.kind === 'scan').state;
  assert.equal(st(7900), 'ok');
  assert.equal(st(8000), 'amber');
  assert.equal(st(10000), 'amber');
  assert.equal(st(10100), 'over');
  const m = meterRows(FIT, one(10100));
  assert.equal(m.over, true);
  assert.match(m.reason, /^Speed: Waveshare scan is 10\.1 ms, over its 10 ms limit \(p95 frame\)\.$/);
  assert.equal(meterRows(FIT, one(10000)).over, false);
});

test('judged on the p95 frame: one slow frame in 20 is not the verdict, two are', () => {
  const scan = (...slow) => meterRows(FIT, { waveshare: { scan: [...fr(99999, 99999), ...many(20 - slow.length, 1000), ...fr(...slow)], low: fr(1), med: fr(1), high: fr(1) } })
    .rows.find((r) => r.kind === 'scan');
  assert.equal(scan(50000).ms, 1);
  assert.equal(scan(50000).state, 'ok');
  assert.equal(scan(50000, 50000).ms, 50);
  assert.equal(scan(50000, 50000).state, 'over');
});

test('scan frames use the scan model and skip the first two; alert frames use the alert model from their first', () => {
  const m = meterRows(FIT, { waveshare: { scan: fr(99999, 99999, 5000), low: fr(1), med: fr(210000, 1), high: fr(1) } });
  const scan = m.rows.find((r) => r.kind === 'scan'), alert = m.rows.find((r) => r.kind === 'alert');
  assert.equal(scan.ms, 5);
  assert.equal(alert.level, 'med'); assert.equal(alert.ms, 420); assert.equal(alert.state, 'over');
  assert.match(m.reason, /Waveshare alert \(MED\) is 420 ms, over its 400 ms limit/);
});

test('a board the fit does not have is uncalibrated and never blocks', () => {
  const fit = { version: 2, boards: { waveshare: FIT.boards.waveshare } };
  const m = meterRows(fit, { 'cyd-engine': { scan: fr(1, 1, 999999), low: fr(999999), med: fr(1), high: fr(1) } });
  assert.deepEqual(m.rows.map((r) => r.state), ['uncalibrated', 'uncalibrated']);
  assert.equal(m.over, false);
  assert.equal(meterRows(null, one(999999)).over, false);
});

let preview;
before(async () => {
  const r = spawnSync('bash', [path.join(ROOT, 'tools/build-designer-wasm.sh')], { encoding: 'utf8' });
  assert.equal(r.status, 0, r.stdout + r.stderr);
  preview = createPreview(await (await import(pathToFileURL(path.join(ROOT, 'web/designer/wasm/uv_preview.js')).href)).default());
});

test('measure replays the draft on both boards in under 2 s', () => {
  const t = makeTemplate('umbravox');
  assert.equal(preview.loadDraft(t.theme, t.emblem).ok, true);
  const t0 = performance.now();
  const m = measureAll((b, mode, n, ms) => preview.measure(b, mode, n, ms), FIT);
  const took = performance.now() - t0;
  assert.ok(took < 2000, `measure took ${took.toFixed(0)} ms`);
  for (const b of ['waveshare', 'cyd-engine']) for (const k of ['scan', 'low', 'med', 'high']) assert.equal(m[b][k].length, 30, `${b} ${k}`);
  assert.equal(m.waveshare.scan[0].drawn, 480 * 320);
  assert.equal(m['cyd-engine'].scan[0].drawn, 320 * 240);
  assert.throws(() => preview.measure('cyd', 'scan'), /board/);
});

const SHIPPED = path.join(ROOT, 'web/designer/js/meter-fit.json');
test('the built-in theme with the Standard layout is under its limits with the shipped fit', { skip: !fs.existsSync(SHIPPED) && 'js/meter-fit.json is written by the calibration session (plan 5c Task 7)' }, () => {
  const f = loadFit(JSON.parse(fs.readFileSync(SHIPPED, 'utf8')));
  assert.equal(f.ok, true, f.why);
  const t = makeTemplate('umbravox');
  assert.equal(preview.loadDraft(t.theme, t.emblem).ok, true);
  const m = meterRows(f.fit, measureAll((b, mode, n, ms) => preview.measure(b, mode, n, ms), f.fit));
  for (const r of m.rows) assert.ok(r.state === 'ok' || r.state === 'amber', `${r.board} ${r.kind}: ${r.ms} ms of ${r.limit} ms`);
});
