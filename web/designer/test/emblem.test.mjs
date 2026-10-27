import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { imageToEmblem, emblemToBin } from '../js/emblem.js';
import { ROOT, read, gradient } from './_util.mjs';

function solid(w, h, r, g, b, a = 255) {
  const d = new Uint8ClampedArray(w * h * 4);
  for (let i = 0; i < w * h; i++) { d[4 * i] = r; d[4 * i + 1] = g; d[4 * i + 2] = b; d[4 * i + 3] = a; }
  return d;
}
test('emblemToBin of the muffin data equals the committed cosmic-clone emblem.bin', () => {
  const text = read('ui/muffin.h');
  const num = (n) => Number(new RegExp('constexpr int ' + n + ' = (\\d+);').exec(text)[1]);
  const body = (n) => new RegExp(n + '\\s*\\[[^\\]]*\\]\\s*=\\s*\\{([^}]*)\\}').exec(text)[1]
    .replace(/\/\/[^\n]*/g, '').split(/[\s,]+/).filter(Boolean);
  const e = { w: num('MUFFIN_W'), h: num('MUFFIN_H'), ncol: num('MUFFIN_NCOL'),
    pal565: body('MUFFIN_PAL').map((t) => parseInt(t, 16)), px: body('MUFFIN_PX').map(Number) };
  assert.equal(e.px.length, e.w * e.h);
  const want = fs.readFileSync(path.join(ROOT, 'sim/packs/cosmic-clone/emblem.bin'));
  assert.deepEqual(Buffer.from(emblemToBin(e)), want);
});

test('emblemToBin layout', () => {
  const b = emblemToBin({ w: 2, h: 1, ncol: 2, pal565: [0xF800, 0x001F], px: [0, 1] });
  assert.deepEqual([...b], [0x55, 0x56, 0x45, 1, 2, 1, 2, 0, 0x00, 0xF8, 0x1F, 0x00, 0, 1]);
});

test('1x1 input yields a 1x1 emblem with ncol 2', () => {
  const e = imageToEmblem(solid(1, 1, 200, 10, 10), 1, 1);
  assert.equal(e.w, 1); assert.equal(e.h, 1); assert.equal(e.ncol, 2);
  assert.deepEqual(e.px, [0]);
  assert.equal(e.pal565[0], 0xC841); // (200,10,10): r5=25, g6=2, b5=1
});

test('small images are never upscaled, large ones go to 96', () => {
  assert.equal(imageToEmblem(solid(40, 30, 1, 2, 3), 40, 30).w, 30);
  const e = imageToEmblem(gradient(500, 300), 500, 300);
  assert.equal(e.w, 96); assert.equal(e.h, 96);
  const f = imageToEmblem(gradient(96, 96), 96, 96);
  assert.equal(f.w, 96);
});

test('photo-like input quantises to <= 16 colours with valid indices', () => {
  const e = imageToEmblem(gradient(500, 300), 500, 300);
  assert.ok(e.ncol >= 2 && e.ncol <= 16);
  assert.equal(e.pal565.length, e.ncol);
  assert.equal(e.px.length, e.w * e.h);
  assert.ok(e.px.every((i) => Number.isInteger(i) && i >= 0 && i < e.ncol));
  assert.ok(e.ncol > 8, 'a gradient should use most of the palette, got ' + e.ncol);
});

test('one-colour image yields ncol 2 and only index 0', () => {
  const e = imageToEmblem(solid(64, 64, 10, 200, 30), 64, 64);
  assert.equal(e.ncol, 2);
  assert.ok(e.px.every((i) => i === 0));
  assert.notEqual(e.pal565[0], e.pal565[1]);
});

test('cover-crop: a wide image keeps its centre square', () => {
  const w = 30, h = 10, d = new Uint8ClampedArray(w * h * 4);
  for (let y = 0; y < h; y++) for (let x = 0; x < w; x++) {
    const i = 4 * (y * w + x);
    const red = x >= 10 && x < 20;
    d[i] = red ? 255 : 0; d[i + 1] = 0; d[i + 2] = red ? 0 : 255; d[i + 3] = 255;
  }
  const e = imageToEmblem(d, w, h);
  assert.equal(e.w, 10);
  assert.ok(e.px.every((i) => i === e.px[0]), 'only the red centre should remain');
  assert.equal(e.pal565[e.px[0]], 0xF800);
});

test('transparent pixels composite over black; opaque ones are kept', () => {
  const d = solid(2, 2, 255, 255, 255, 255);
  d[7] = 0; d[15] = 0; // right column fully transparent white
  const e = imageToEmblem(d, 2, 2);
  assert.deepEqual(e.px.map((i) => e.pal565[i]), [0xFFFF, 0x0000, 0xFFFF, 0x0000]);
});

test('deterministic', () => {
  const g = gradient(200, 150);
  assert.deepEqual(imageToEmblem(g, 200, 150), imageToEmblem(g, 200, 150));
});
