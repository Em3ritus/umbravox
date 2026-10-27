// The designer's preview module, through the real wasm, against the simulator.
import { test, before } from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import zlib from 'node:zlib';
import { spawnSync } from 'node:child_process';
import { pathToFileURL } from 'node:url';
import { ROOT, SIM } from './_util.mjs';
import { createPreview, W, H } from '../js/preview.js';
import { openPackFiles, makeTemplate, blankTheme } from '../js/model.js';

const WASM_JS = path.join(ROOT, 'web/designer/wasm/uv_preview.js');
let preview, M;

before(async () => {
  // Same incremental build the parity test runs; npm test runs files one at a time.
  for (const [cmd, args] of [['make', ['-s', '-C', path.join(ROOT, 'sim'), 'umbravox']], ['bash', [path.join(ROOT, 'tools/build-designer-wasm.sh')]]]) {
    const r = spawnSync(cmd, args, { encoding: 'utf8' });
    assert.equal(r.status, 0, `${cmd} failed:\n${r.stdout}${r.stderr}`);
  }
  M = await (await import(pathToFileURL(WASM_JS).href)).default();
  preview = createPreview(M);
});

function decodePng(buf) {
  let p = 8, w = 0, h = 0; const idat = [];
  while (p < buf.length) {
    const len = buf.readUInt32BE(p), type = buf.toString('latin1', p + 4, p + 8), d = buf.subarray(p + 8, p + 8 + len);
    if (type === 'IHDR') { w = d.readUInt32BE(0); h = d.readUInt32BE(4); } else if (type === 'IDAT') idat.push(d); else if (type === 'IEND') break;
    p += 12 + len;
  }
  const raw = zlib.inflateSync(Buffer.concat(idat)), stride = 1 + w * 3, out = new Uint8Array(w * h * 4);
  for (let y = 0; y < h; y++) {
    assert.equal(raw[y * stride], 0, 'PNG filter');
    for (let x = 0; x < w; x++) for (let k = 0; k < 3; k++) out[(y * w + x) * 4 + k] = raw[y * stride + 1 + x * 3 + k];
    for (let x = 0; x < w; x++) out[(y * w + x) * 4 + 3] = 255;
  }
  return { w, h, rgba: out };
}

function sim(args) {
  const out = path.join(fs.mkdtempSync(path.join(os.tmpdir(), 'uvx-prev-')), 'o.png');
  const r = spawnSync(SIM, [out, '--scale', '1', ...args], { encoding: 'utf8' });
  assert.equal(r.status, 0, r.stdout + r.stderr);
  const png = decodePng(fs.readFileSync(out));
  assert.deepEqual([png.w, png.h], [W, H]);
  return png.rgba;
}

function openFixture(d) {
  const dir = path.join(ROOT, 'sim/packs', d);
  return openPackFiles(fs.readdirSync(dir).map((n) => ({ name: n, data: new Uint8Array(fs.readFileSync(path.join(dir, n))) })));
}

test('a draft loaded through preview.js renders byte-identical to the simulator (cosmic-clone, scan and alert)', () => {
  const f = openFixture('cosmic-clone');
  const r = preview.loadDraft(f.theme, f.emblem);
  assert.equal(r.ok, true, r.error);
  assert.equal(r.id, 3);
  const mine = preview.renderDraft('scan', 24);
  assert.deepEqual(Buffer.from(mine), Buffer.from(sim(['--pack', path.join(ROOT, 'sim/packs/cosmic-clone'), '--frame', '24', '--screen', 'scan'])));
  const al = preview.renderDraft('alert', 20);
  assert.deepEqual(Buffer.from(al), Buffer.from(sim(['--pack', path.join(ROOT, 'sim/packs/cosmic-clone'), '--frame', '20', '--screen', 'scan', '--alert', '0'])));
  assert.notDeepEqual(Buffer.from(mine), Buffer.from(al), 'comparison is not vacuous');
});

test('built-in renders match the simulator too', () => {
  for (const id of [0, 1, 2]) {
    assert.deepEqual(Buffer.from(preview.renderBuiltin(id, 'scan', 24)), Buffer.from(sim(['--theme', String(id), '--frame', '24', '--screen', 'scan'])), 'theme ' + id);
  }
});

test('every template loads as a draft and draws', () => {
  for (const id of ['umbravox', 'baph0mau5', 'cosmic']) {
    const t = makeTemplate(id);
    const r = preview.loadDraft(t.theme, t.emblem);
    assert.equal(r.ok, true, id + ': ' + r.error);
    for (const s of ['scan', 'mesh', 'alert', 'menu']) assert.equal(preview.renderDraft(s, 3).length, W * H * 4);
  }
});

test('a rejected draft reports why and the last good one keeps drawing', () => {
  const good = makeTemplate('umbravox');
  assert.equal(preview.loadDraft(good.theme, good.emblem).ok, true);
  const before = Buffer.from(preview.renderDraft('scan', 5));
  const bad = { ...blankTheme(), name: 'UMBRAVOX' };          // reserved name: the device would skip it
  const r = preview.loadDraft(bad, null);
  assert.equal(r.ok, false);
  assert.ok(r.error.length > 3);
  assert.deepEqual(Buffer.from(preview.renderDraft('scan', 5)), before);
  const bad2 = { ...blankTheme(), name: 'A"B' };               // pack.js refuses before the device is asked
  assert.equal(preview.loadDraft(bad2, null).ok, false);
});

test('the preview never passes a sweep the pack does not offer (radar withdrawn -> its start sweep)', () => {
  const calls = [];
  const stub = {
    HEAPU8: new Uint8Array(1 << 17), _malloc: (n) => 1024, _free() {}, _uv_error: () => 0, UTF8ToString: () => '',
    _uv_load_pack: () => 3,
    _uv_render: (...a) => calls.push(a),
  };
  const p = createPreview(stub);
  const t = { ...blankTheme(), sweeps: ['pulse', 'comet'], sweep: 'comet' };
  assert.equal(p.loadDraft(t, null).ok, true);
  p.renderDraft('scan', 1);
  assert.equal(calls[0][3], 9, 'comet is id 9');
  const t2 = { ...blankTheme(), sweeps: ['star'], sweep: 'radar' };   // start sweep not offered
  p.loadDraft(t2, null); p.renderDraft('scan', 1);
  assert.equal(calls[1][3], 3, 'falls back to the first sweep offered (star = 3), never radar');
});
