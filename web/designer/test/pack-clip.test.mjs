// A radar clip is the third file of a pack: radar.vid, written beside theme.json and emblem.bin.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { buildPack } from '../js/pack.js';
import { runChecks } from '../js/checks.js';
import { makeTemplate } from '../js/model.js';
import { buildClip, CLIP_SIZE, clipFile } from '../js/radarvideo.js';
import limits from '../js/limits.json' with { type: 'json' };

const clip = n => buildClip(Array.from({ length: n }, (_, f) => Uint8Array.from({ length: CLIP_SIZE * CLIP_SIZE }, (_, i) => (i + f) % 16)));

test('limits know the clip: MAX_CLIP 780312, two clips at most', () => {
  assert.equal(limits.MAX_CLIP, 12 + 150 * 5202);
  assert.equal(limits.MAX_CLIPS, 2);
});

test('a pack with a clip has radar.vid after theme.json and emblem.bin', () => {
  const t = makeTemplate('cosmic');
  const p = buildPack(t.theme, t.emblem, clip(3));
  assert.deepEqual(p.files.map(f => f.path.split('/')[1]), ['theme.json', 'emblem.bin', 'radar.vid']);
  assert.deepEqual(Array.from(p.files[2].data), Array.from(clipFile(clip(3))));
  assert.equal(p.bytes, p.files.reduce((n, f) => n + f.data.length, 0));
});

test('a pack without a clip is exactly what it was', () => {
  const t = makeTemplate('cosmic');
  assert.deepEqual(buildPack(t.theme, t.emblem).files.map(f => f.path.split('/')[1]), ['theme.json', 'emblem.bin']);
  assert.deepEqual(buildPack(t.theme, t.emblem, null).files.length, 2);
});

test('a clip works with a pack that has no emblem, and a bad clip is refused', () => {
  const t = makeTemplate('umbravox');
  assert.deepEqual(buildPack(t.theme, null, clip(2)).files.map(f => f.path.split('/')[1]), ['theme.json', 'radar.vid']);
  assert.throws(() => buildPack(t.theme, null, { ...clip(2), w: 64 }), /102/);
});

test('checks say whether the clip keeps its own colours', () => {
  const t = makeTemplate('umbravox');
  const text = (r) => r.map(c => c.text).join('\n');
  const frames = [Uint8Array.from({ length: CLIP_SIZE * CLIP_SIZE }, (_, i) => i % 16)];
  assert.match(text(runChecks(t.theme, null, buildClip(frames, new Uint8Array(32)))), /own colours/);
  assert.match(text(runChecks(t.theme, null, buildClip(frames))), /theme's colours/);
  assert.ok(runChecks(t.theme, null, buildClip(frames, new Uint8Array(32))).every(c => c.level !== 'error'));
  const p = buildPack(t.theme, null, buildClip(frames, new Uint8Array(32)));
  assert.equal(p.files.find(f => f.path.endsWith('radar.vid')).data[4], 2);
});

test('checks: the clip is reported, and its size is held to the limit', () => {
  const t = makeTemplate('umbravox');
  const none = runChecks(t.theme, null);
  const withClip = runChecks(t.theme, null, clip(125));
  const text = (r) => r.map(c => c.text).join('\n');
  assert.ok(!/radar video/i.test(text(none)));
  assert.match(text(withClip), /Radar video: 125 frames, 10\.4 s/);
  assert.ok(withClip.every(c => c.level !== 'error'), 'a good clip raises no error');
  assert.match(text(withClip), /Waveshare/);
  const bad = runChecks(t.theme, null, { ...clip(2), w: 64 });
  assert.ok(bad.some(c => c.level === 'error' && /102/.test(c.text)));
});
