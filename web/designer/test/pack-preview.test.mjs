// tools/pack-preview.mjs: a pack folder -> cyd.png and waveshare.png, drawn by the device's own code (the wasm preview).
import { test, before } from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import zlib from 'node:zlib';
import { spawnSync } from 'node:child_process';
import { ROOT, writePack } from './_util.mjs';
import { encodePng } from '../../../tools/png.mjs';
import { buildPack } from '../js/pack.js';
import { makeTemplate } from '../js/model.js';

const SIG = [137, 80, 78, 71, 13, 10, 26, 10];
const size = (png) => { const dv = new DataView(png.buffer, png.byteOffset); return [dv.getUint32(16), dv.getUint32(20)]; };
// The pixels of a PNG written by encodePng (one IDAT, RGB, filter 0 on every row).
function pixels(png) {
  const dv = new DataView(png.buffer, png.byteOffset);
  const [w, h] = size(png);
  const len = dv.getUint32(33);
  assert.equal(String.fromCharCode(...png.subarray(37, 41)), 'IDAT');
  const raw = zlib.inflateSync(png.subarray(41, 41 + len));
  assert.equal(raw.length, h * (1 + 3 * w));
  return (x, y) => [...raw.subarray(y * (1 + 3 * w) + 1 + 3 * x, y * (1 + 3 * w) + 4 + 3 * x)];
}

test('encodePng writes a valid PNG of the scaled size', () => {
  const png = encodePng(new Uint8Array([255, 0, 0, 255, 0, 255, 0, 255]), 2, 1, 3);
  assert.deepEqual([...png.subarray(0, 8)], SIG);
  assert.deepEqual(size(png), [6, 3]);
  assert.equal(png[24], 8); assert.equal(png[25], 2);                 // 8-bit, colour type RGB
  const at = pixels(png);
  assert.deepEqual(at(0, 0), [255, 0, 0]); assert.deepEqual(at(2, 2), [255, 0, 0]);
  assert.deepEqual(at(3, 0), [0, 255, 0]); assert.deepEqual(at(5, 2), [0, 255, 0]);
  assert.equal(String.fromCharCode(...png.subarray(png.length - 8, png.length - 4)), 'IEND');
});

test('encodePng refuses a buffer that is not w*h*4 bytes, and a bad scale', () => {
  assert.throws(() => encodePng(new Uint8Array(7), 2, 1), /size/);
  assert.throws(() => encodePng(new Uint8Array(8), 2, 1, 0), /scale/);
});

const run = (folder, out) => spawnSync('node', ['tools/pack-preview.mjs', folder, '--out', out], { cwd: ROOT, encoding: 'utf8' });
const tmp = () => fs.mkdtempSync(path.join(os.tmpdir(), 'uvx-prev-'));
const packOf = (id) => { const t = makeTemplate(id); return writePack(buildPack(t.theme, t.emblem)); };

before(() => {
  const r = spawnSync('bash', [path.join(ROOT, 'tools/build-designer-wasm.sh')], { encoding: 'utf8' });
  assert.equal(r.status, 0, r.stdout + r.stderr);
});

test('pack-preview writes both pictures for a template pack', () => {
  const out = tmp();
  const r = run(packOf('cosmic'), out);
  assert.equal(r.status, 0, r.stderr);
  const cyd = fs.readFileSync(path.join(out, 'cyd.png')), ws = fs.readFileSync(path.join(out, 'waveshare.png'));
  assert.deepEqual([...cyd.subarray(0, 8)], SIG); assert.deepEqual([...ws.subarray(0, 8)], SIG);
  assert.deepEqual(size(cyd), [480, 360]);
  assert.deepEqual(size(ws), [480, 320]);
  assert.deepEqual(fs.readdirSync(out).sort(), ['cyd.png', 'waveshare.png']);
});

test('two different templates give different pictures', () => {
  const a = tmp(), b = tmp();
  assert.equal(run(packOf('umbravox'), a).status, 0);
  assert.equal(run(packOf('cosmic'), b).status, 0);
  for (const f of ['cyd.png', 'waveshare.png']) assert.notEqual(fs.readFileSync(path.join(a, f)).toString('hex'), fs.readFileSync(path.join(b, f)).toString('hex'), f);
});

test('pack-preview fails with one line for a folder with no theme.json', () => {
  const empty = tmp(), out = tmp();
  fs.writeFileSync(path.join(empty, 'readme.txt'), 'x');
  const r = run(empty, out);
  assert.equal(r.status, 1);
  assert.match(r.stderr, /theme\.json/);
  assert.equal(r.stderr.trim().split('\n').length, 1);
  assert.deepEqual(fs.readdirSync(out), []);
});

test('pack-preview fails for a pack the device would reject, and reads only the four pack files', () => {
  const folder = packOf('umbravox'), out = tmp();
  fs.writeFileSync(path.join(folder, 'theme.json'), '{"format":1,"name":"UMBRAVOX"');
  fs.writeFileSync(path.join(folder, 'extra.bin'), Buffer.alloc(10));
  const r = run(folder, out);
  assert.equal(r.status, 1);
  assert.equal(r.stderr.trim().split('\n').length, 1);
});

test('pack-preview with no arguments prints its usage', () => {
  const r = spawnSync('node', ['tools/pack-preview.mjs'], { cwd: ROOT, encoding: 'utf8' });
  assert.equal(r.status, 1);
  assert.match(r.stderr, /pack-preview\.mjs <pack-folder> --out <dir>/);
});
