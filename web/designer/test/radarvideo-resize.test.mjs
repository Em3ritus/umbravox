// A radar clip at the placed radar size: resizeClip, and buildPack resizing to the layout's Waveshare radar.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { buildClip, clipFile, readClipFile, resizeClip, frameBytes } from '../js/radarvideo.js';
import { buildPack } from '../js/pack.js';
import { makeTemplate } from '../js/model.js';

const uniform = (level, frames = 2) => buildClip(Array.from({ length: frames }, () => new Uint8Array(102 * 102).fill(level)));

test('resizeClip keeps the header fields and sets the side', () => {
  const c = uniform(9, 3), r = resizeClip(c, 48);
  assert.equal(r.w, 48); assert.equal(r.h, 48);
  assert.equal(r.frames, 3); assert.equal(r.fps, c.fps);
  assert.equal(r.data.length, 3 * frameBytes(48));
  assert.equal(frameBytes(48), 1152); assert.equal(frameBytes(25), 313);
  assert.equal(resizeClip(c, 102), c, 'same size: unchanged');
});

test('a uniform grey clip stays uniform; a colour clip keeps its palette and indices', () => {
  const r = resizeClip(uniform(9), 48);
  assert.ok(r.data.every((b) => b === 0x99));
  const pal = new Uint8Array(32).fill(7);
  const c = buildClip([new Uint8Array(102 * 102).fill(5)], pal);
  const rc = resizeClip(c, 31);
  assert.deepEqual([...rc.palette], [...pal]);
  const lv = []; for (const b of rc.data) lv.push(b >> 4, b & 15);
  assert.ok(lv.slice(0, 31 * 31).every((v) => v === 5), 'indices are sampled, never averaged');
});

test('a resized clip round-trips through radar.vid (the device rules: any square 24 to 102)', () => {
  const r = resizeClip(uniform(3), 48);
  const back = readClipFile(clipFile(r));
  assert.equal(back.w, 48);
  assert.throws(() => resizeClip(uniform(3), 23), /24/);
});

test('the pack carries the clip at the Waveshare radar radius; the draft keeps the original', () => {
  const t = makeTemplate('umbravox').theme;
  const c = uniform(6);
  const layout = JSON.stringify({ format: 1, waveshare: { radar: [188, 56, 101, 101] }, cyd: {} });   // r = (101 - 5) / 2 = 48
  const p = buildPack(t, null, c, null, layout);
  const vid = p.files.find((f) => f.path.endsWith('/radar.vid')).data;
  assert.equal(vid[5], 48); assert.equal(vid[6], 48);
  assert.equal(c.w, 102, 'the clip passed in is not changed');
  const plain = buildPack(t, null, c, null, null).files.find((f) => f.path.endsWith('/radar.vid')).data;
  assert.equal(plain[5], 102);
});
