import test from 'node:test';
import assert from 'node:assert/strict';
import { imageToArt, artToBin, readArt, artBytes, resizeCover, ART_MIN_DELAY_MS, MAX_ART, MAX_ART_FRAMES } from '../js/artfmt.js';

function rgba(w, h, fn) {
  const d = new Uint8ClampedArray(w * h * 4);
  for (let y = 0; y < h; y++) for (let x = 0; x < w; x++) d.set(fn(x, y), 4 * (y * w + x));
  return d;
}
const stripes = (w, h) => rgba(w, h, (x) => (x % 2 ? [255, 0, 0, 255] : [0, 0, 255, 255]));

test('imageToArt makes exactly the placed size, and artToBin/readArt round-trip it', () => {
  for (const [w, h] of [[37, 61], [320, 240], [12, 12]]) {
    const a = imageToArt([stripes(80, 50)], 80, 50, { w, h });
    assert.equal(a.w, w); assert.equal(a.h, h); assert.equal(a.frames, 1);
    const bin = artToBin(a);
    assert.equal(bin.length, artBytes(w, h, 1, a.ncol));
    assert.deepEqual([...bin.slice(0, 4)], [0x55, 0x56, 0x41, 0x52]);
    const back = readArt(bin);
    assert.equal(back.w, w); assert.equal(back.h, h);
    assert.deepEqual(back.px, a.px);
    assert.deepEqual(back.pal, a.pal);
  }
});

test('transparent pixels become index 0 and the palette keeps index 0 for them', () => {
  const src = rgba(10, 10, (x, y) => (x < 5 ? [0, 0, 0, 0] : [0, 255, 0, 255]));
  const a = imageToArt([src], 10, 10, { w: 10, h: 10, transparent: true });
  assert.equal(a.transparent, true);
  const px = (x, y) => { const i = y * 10 + x; const b = a.px[0][i >> 1]; return i & 1 ? b & 15 : b >> 4; };
  assert.equal(px(0, 0), 0); assert.equal(px(4, 9), 0);
  assert.notEqual(px(5, 0), 0); assert.notEqual(px(9, 9), 0);
  assert.equal(a.pal[px(9, 9)], 0x07e0);   // pure green survives
  const opaque = imageToArt([src], 10, 10, { w: 10, h: 10 });
  assert.equal(opaque.transparent, false);
});

test('frame delays are kept per frame, floored to the board\'s minimum', () => {
  const f = [0, 1, 2, 3, 4].map((k) => rgba(8, 8, () => [k * 50, 0, 0, 255]));
  const a = imageToArt(f, 8, 8, { w: 8, h: 8, delays: [10, 120, 0, 300, 70] });
  assert.deepEqual(a.delays, [50, 120, 50, 300, 70]);
  assert.equal(ART_MIN_DELAY_MS.waveshare, 50); assert.equal(ART_MIN_DELAY_MS.cyd, 100);
  const c = imageToArt(f, 8, 8, { w: 8, h: 8, delays: [10, 120, 0, 300, 70], minDelay: ART_MIN_DELAY_MS.cyd });
  assert.deepEqual(c.delays, [100, 120, 100, 300, 100]);
  assert.deepEqual(readArt(artToBin(c)).delays, c.delays);
});

test('limits: too many frames or too many bytes is an error naming the limit', () => {
  const f = rgba(4, 4, () => [1, 2, 3, 255]);
  assert.throws(() => imageToArt(new Array(MAX_ART_FRAMES + 1).fill(f), 4, 4, { w: 4, h: 4 }), /64 frames/);
  assert.throws(() => imageToArt([f, f, f], 4, 4, { w: 480, h: 320 }), new RegExp(String(MAX_ART)));
  assert.throws(() => imageToArt([f], 4, 4, { w: 481, h: 10 }), /480x320/);
});

test('resizeCover crops to the target aspect and averages', () => {
  // 4x2 source, left half black right half white, into 1x1: the centre crop is a 2x2 straddling both halves -> grey
  const src = rgba(4, 2, (x) => (x < 2 ? [0, 0, 0, 255] : [255, 255, 255, 255]));
  const out = resizeCover(src, 4, 2, 1, 1);
  assert.ok(Math.abs(out[0] - 128) <= 1, `grey, got ${out[0]}`);
  const up = resizeCover(rgba(1, 1, () => [9, 8, 7, 255]), 1, 1, 3, 3);
  assert.deepEqual([...up.slice(0, 4)], [9, 8, 7, 255]);
});
