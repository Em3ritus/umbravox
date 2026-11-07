// The radar-clip converter the designer runs in the browser: gray -> 16 levels -> two pixels per byte. Pure functions,
// so Node can test them. The drawing itself is held to the device by wasm-parity.test.mjs.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { quantizeColour, greyFrames, PAL_BYTES, MAX_COLOUR_FRAMES, CLIP_SIZE, CLIP_FPS, MAX_FRAMES, MAX_BYTES, lumaFromRgba, levelsFromGray, packLevels, buildClip, validateClip, clipFile, readClipFile, MAX_CLIP_FILE } from '../js/radarvideo.js';

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
  assert.throws(() => readClipFile(bad(x => { x[4] = 3; })), /version/);
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

// ---- colour clips: radar.vid version 2 (a 16-colour palette travels with the clip) -------------------------------
const PX = CLIP_SIZE * CLIP_SIZE;
const rgba = (fn) => { const a = new Uint8ClampedArray(PX * 4); for (let i = 0; i < PX; i++) { const [r, g, b] = fn(i % CLIP_SIZE, Math.floor(i / CLIP_SIZE)); a.set([r, g, b, 255], 4 * i); } return a; };
const pal565 = (pal, k) => { const v = pal[2 * k] | (pal[2 * k + 1] << 8); return [((v >> 11) & 31) * 255 / 31, ((v >> 5) & 63) * 255 / 63, (v & 31) * 255 / 31]; };

test('quantizeColour: two colours come back as two palette entries, every pixel on the right one', () => {
  const f = rgba((x) => (x < 51 ? [250, 20, 20] : [20, 20, 250]));
  const q = quantizeColour([f], { dither: false });
  assert.equal(q.palette.length, PAL_BYTES);
  assert.equal(q.frames.length, 1); assert.equal(q.frames[0].length, PX);
  assert.ok(q.frames[0].every((v) => v < 16));
  const left = pal565(q.palette, q.frames[0][0]), right = pal565(q.palette, q.frames[0][50 + 102 * 3 + 1]);
  assert.ok(left[0] > 230 && left[1] < 40 && left[2] < 40, 'red side is red (' + left + ')');
  assert.ok(right[2] > 230 && right[0] < 40, 'blue side is blue (' + right + ')');
  assert.equal(q.frames[0][0], q.frames[0][51 * 0 + 10]); assert.notEqual(q.frames[0][0], q.frames[0][60]);
});

test('quantizeColour: sixteen or fewer colours are kept almost exactly; more are approximated, never worse than the range', () => {
  const cols = Array.from({ length: 12 }, (_, i) => [(i * 67) % 256, (i * 131 + 40) % 256, (i * 29 + 100) % 256]);
  const f = rgba((x, y) => cols[(Math.floor(x / 9) + Math.floor(y / 9) * 3) % 12]);
  const q = quantizeColour([f], { dither: false });
  let worst = 0;
  for (let i = 0; i < PX; i++) { const c = pal565(q.palette, q.frames[0][i]); for (let k = 0; k < 3; k++) worst = Math.max(worst, Math.abs(c[k] - f[4 * i + k])); }
  assert.ok(worst < 9, 'RGB565 rounding is the only error (worst ' + worst + ')');
  const grad = rgba((x, y) => [x * 2.5, y * 2.5, 128]);                              // thousands of colours
  const g = quantizeColour([grad], { dither: false });
  let mean = 0; for (let i = 0; i < PX; i++) { const c = pal565(g.palette, g.frames[0][i]); mean += (Math.abs(c[0] - grad[4 * i]) + Math.abs(c[1] - grad[4 * i + 1]) + Math.abs(c[2] - grad[4 * i + 2])) / 3; }
  assert.ok(mean / PX < 14, 'a gradient is within a few percent on average (' + (mean / PX).toFixed(1) + ')');
});

test('quantizeColour: one palette for the whole clip, deterministic, any frame count', () => {
  const a = rgba((x) => [x * 2, 30, 200 - x]), b = rgba((x, y) => [200, y * 2, x]);
  const q1 = quantizeColour([a, b, a]), q2 = quantizeColour([a, b, a]);
  assert.deepEqual(Array.from(q1.palette), Array.from(q2.palette));
  assert.deepEqual(q1.frames.map((x) => Array.from(x)), q2.frames.map((x) => Array.from(x)));
  assert.equal(q1.frames.length, 3);
  assert.deepEqual(Array.from(q1.frames[0]), Array.from(q1.frames[2]), 'the same picture quantizes the same way in every frame (no shimmer)');
  const flat = quantizeColour([rgba(() => [10, 200, 90])]);
  assert.ok(flat.frames[0].every((v) => v === flat.frames[0][0]), 'a flat picture is one index');
});

test('quantizeColour: dithering stays on the palette and is steady from frame to frame', () => {
  const grad = rgba((x, y) => [x * 2.5, 100, y * 2.5]);
  const q = quantizeColour([grad, grad]);
  assert.ok(q.frames[0].every((v) => v < 16));
  assert.deepEqual(Array.from(q.frames[0]), Array.from(q.frames[1]));
  const plain = quantizeColour([grad], { dither: false });
  assert.notDeepEqual(Array.from(q.frames[0]), Array.from(plain.frames[0]), 'dither changes some pixels');
});

test('greyFrames is the old grey conversion', () => {
  const f = rgba((x, y) => [x * 2, y * 2, 77]);
  const g = greyFrames([f], 24, 235);
  assert.deepEqual(Array.from(g[0]), Array.from(levelsFromGray(lumaFromRgba(f), 24, 235)));
});

test('a colour clip is radar.vid version 2: palette after the header, at most 149 frames', () => {
  const pal = Uint8Array.from({ length: 32 }, (_, i) => (i * 9) & 255);
  const frames = Array.from({ length: 3 }, (_, f) => Uint8Array.from({ length: PX }, (_, i) => (i + f) % 16));
  const c = buildClip(frames, pal);
  assert.deepEqual(Array.from(c.palette), Array.from(pal));
  assert.equal(validateClip(c), null);
  const file = clipFile(c);
  assert.equal(file[4], 2);
  assert.equal(file.length, 12 + 32 + 3 * 5202);
  assert.deepEqual(Array.from(file.slice(12, 44)), Array.from(pal));
  const back = readClipFile(file);
  assert.deepEqual(Array.from(back.palette), Array.from(pal));
  assert.deepEqual(Array.from(back.data), Array.from(c.data));
  assert.equal(back.frames, 3);
  assert.equal(clipFile(buildClip(frames)).length, 12 + 3 * 5202, 'without a palette it is version 1, as before');
  assert.equal(clipFile(buildClip(frames))[4], 1);
  assert.equal(readClipFile(clipFile(buildClip(frames))).palette, undefined);
  assert.equal(MAX_COLOUR_FRAMES, 149);
  const many = Array.from({ length: 150 }, () => frames[0]);
  assert.throws(() => clipFile(buildClip(many, pal)), /149/);
  assert.equal(clipFile(buildClip(many.slice(0, 149), pal)).length, 12 + 32 + 149 * 5202);
  assert.ok(clipFile(buildClip(many.slice(0, 149), pal)).length <= MAX_CLIP_FILE);
  assert.throws(() => clipFile({ ...c, palette: new Uint8Array(31) }), /palette/);
  const bad = (f) => { const x = Uint8Array.from(file); f(x); return x; };
  assert.throws(() => readClipFile(bad((x) => { x[4] = 3; })), /version/);
  assert.throws(() => readClipFile(file.slice(0, 30)), /short/);
  assert.throws(() => readClipFile(file.slice(0, file.length - 1)), /length/);
  assert.throws(() => readClipFile(bad((x) => { x[8] = 150; x[9] = 0; })), /149|length/);
});
