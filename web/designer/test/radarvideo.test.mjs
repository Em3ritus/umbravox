// The radar-clip converter the designer runs in the browser: gray -> 16 levels -> two pixels per byte. Pure functions,
// so Node can test them. The drawing itself is held to the device by wasm-parity.test.mjs.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { CLIP_SIZE, CLIP_FPS, MAX_FRAMES, MAX_BYTES, lumaFromRgba, levelsFromGray, packLevels, buildClip, validateClip, clipFile, readClipFile, MAX_CLIP_FILE } from '../js/radarvideo.js';

test('the clip is 102 px at 12 fps, and the frame cap fits the wasm buffer', () => {
  assert.equal(CLIP_SIZE, 102);
  assert.equal(CLIP_FPS, 12);
  assert.ok(MAX_FRAMES * Math.ceil(CLIP_SIZE * CLIP_SIZE / 2) <= MAX_BYTES);
  assert.ok(MAX_CLIP_FILE <= MAX_BYTES + 12, 'the clip file fits the device limit');
  assert.ok(MAX_FRAMES >= 12 * 12, 'at least twelve seconds');
});

test('luma of RGBA pixels (BT.601, the same weights ffmpeg uses for gray)', () => {
  const g = lumaFromRgba(new Uint8ClampedArray([255, 255, 255, 255, 0, 0, 0, 255, 255, 0, 0, 255, 0, 255, 0, 255]));
  assert.deepEqual(Array.from(g), [255, 0, 76, 150]);
});

test('levels: black and white points, clamped, 16 steps', () => {
  const l = levelsFromGray(Uint8Array.from([0, 24, 100, 150, 235, 255]), 24, 235);
  assert.deepEqual(Array.from(l), [0, 0, 5, 9, 15, 15]);
  assert.deepEqual(Array.from(levelsFromGray(Uint8Array.from([10, 90, 150]), 0, 150)), [1, 9, 15]);
});

test('levels refuse a white point at or below the black point', () => {
  assert.throws(() => levelsFromGray(Uint8Array.from([1]), 100, 100), /white/);
});

test('packing: high nibble first, an odd pixel count is padded with 0', () => {
  assert.deepEqual(Array.from(packLevels(Uint8Array.from([1, 2, 3, 4]))), [0x12, 0x34]);
  assert.deepEqual(Array.from(packLevels(Uint8Array.from([15, 0, 7]))), [0xF0, 0x70]);
});

test('buildClip joins frames and reports the shape the wasm takes', () => {
  const frames = [0, 1, 2].map(k => new Uint8Array(CLIP_SIZE * CLIP_SIZE).fill(k));
  const c = buildClip(frames);
  assert.deepEqual([c.w, c.h, c.frames, c.fps], [102, 102, 3, 12]);
  assert.equal(c.data.length, 3 * 5202);
  assert.equal(c.data[5202], 0x11);
  assert.equal(c.data[2 * 5202], 0x22);
  assert.equal(validateClip(c), null);
});

test('buildClip refuses an empty set, a wrong frame size and too many frames', () => {
  assert.throws(() => buildClip([]), /no frames/);
  assert.throws(() => buildClip([new Uint8Array(10)]), /size/);
  assert.throws(() => buildClip(Array.from({ length: MAX_FRAMES + 1 }, () => new Uint8Array(CLIP_SIZE * CLIP_SIZE))), /frames/);
});

test('validateClip names what is wrong', () => {
  const ok = { w: 102, h: 102, frames: 2, fps: 12, data: new Uint8Array(2 * 5202) };
  assert.equal(validateClip(ok), null);
  assert.match(validateClip({ ...ok, w: 64 }), /102/);
  assert.match(validateClip({ ...ok, data: new Uint8Array(5) }), /bytes/);
  assert.match(validateClip({ ...ok, fps: 0 }), /fps/);
  assert.match(validateClip({ ...ok, frames: 0, data: new Uint8Array(0) }), /frames/);
});

test('radar.vid: 12-byte header then the frames, the layout ui/themepack.h parseClip reads', () => {
  const c = buildClip([0, 1].map(k => new Uint8Array(CLIP_SIZE * CLIP_SIZE).fill(k)));
  const f = clipFile(c);
  assert.equal(f.length, 12 + 2 * 5202);
  assert.deepEqual(Array.from(f.slice(0, 12)), [0x55, 0x56, 0x52, 0x56, 1, 102, 102, 12, 2, 0, 0, 0]);
  assert.equal(f[12 + 5202], 0x11);
  assert.equal(MAX_CLIP_FILE, 12 + 150 * 5202);
});

test('radar.vid round-trips, and a damaged file is refused with a reason', () => {
  const c = buildClip([3, 9, 12].map(k => new Uint8Array(CLIP_SIZE * CLIP_SIZE).fill(k)));
  const back = readClipFile(clipFile(c));
  assert.deepEqual([back.w, back.h, back.frames, back.fps], [102, 102, 3, 12]);
  assert.deepEqual(Array.from(back.data), Array.from(c.data));
  const f = clipFile(c);
  const bad = (mut) => { const x = Uint8Array.from(f); mut(x); return x; };
  assert.throws(() => readClipFile(bad(x => { x[0] = 0; })), /UVRV/);
  assert.throws(() => readClipFile(bad(x => { x[4] = 2; })), /version/);
  assert.throws(() => readClipFile(bad(x => { x[5] = 64; })), /102/);
  assert.throws(() => readClipFile(bad(x => { x[7] = 31; })), /fps/);
  assert.throws(() => readClipFile(bad(x => { x[8] = 0; x[9] = 0; })), /frames/);
  assert.throws(() => readClipFile(bad(x => { x[10] = 1; })), /reserved/);
  assert.throws(() => readClipFile(f.slice(0, f.length - 1)), /length/);
  assert.throws(() => readClipFile(f.slice(0, 5)), /short/);
});

import { gifPlan, isGifFile } from '../js/radarvideo.js';

test('gifPlan: which GIF frame is showing at each 12 fps tick (frame delays are uneven)', () => {
  // 10 frames of 100 ms = 1 s -> 12 ticks at 83.3 ms
  assert.deepEqual(gifPlan(Array(10).fill(100)), [0, 0, 1, 2, 3, 4, 5, 5, 6, 7, 8, 9]);
  // one long frame then short ones
  assert.deepEqual(gifPlan([500, 100, 100]), [0, 0, 0, 0, 0, 0, 1, 1, 2]);
  // a single still frame is a one-tick clip
  assert.deepEqual(gifPlan([1000]), Array(12).fill(0));
  // longer than 12 s is cut at 12 s (144 ticks)
  assert.equal(gifPlan(Array(200).fill(100)).length, 144);
  // zero or missing delays count as 100 ms (what browsers do)
  assert.deepEqual(gifPlan([0, 0, 0, 0, 0, 0, 0, 0, 0, 0]), gifPlan(Array(10).fill(100)));
  assert.throws(() => gifPlan([]), /no frames/);
});

test('isGifFile goes by type or by name', () => {
  assert.equal(isGifFile({ type: 'image/gif', name: 'x' }), true);
  assert.equal(isGifFile({ type: '', name: 'LOOP.GIF' }), true);
  assert.equal(isGifFile({ type: 'video/mp4', name: 'a.mp4' }), false);
});
