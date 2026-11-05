// Alert sounds are the fourth file of a pack: alert.snd, written beside theme.json, emblem.bin and radar.vid.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { buildPack } from '../js/pack.js';
import { runChecks } from '../js/checks.js';
import { installGate } from '../js/gate.js';
import { makeTemplate, selectPackFiles, openPackFiles } from '../js/model.js';
import { buildClip, CLIP_SIZE } from '../js/radarvideo.js';
import { soundFile, emptySounds } from '../js/alertsound.js';
import limits from '../js/limits.json' with { type: 'json' };

const tone = (n, k = 1) => Int16Array.from({ length: n }, (_, i) => ((i * k) % 200) * 100 - 10000);
const sounds = (lo = 3200, me = 0, hi = 16000) => ({ low: lo ? tone(lo) : null, med: me ? tone(me, 3) : null, high: hi ? tone(hi, 5) : null });
const clip = () => buildClip([Uint8Array.from({ length: CLIP_SIZE * CLIP_SIZE }, (_, i) => i % 16)]);
const name = (f) => f.path.split('/')[1];

test('limits know the sounds: MAX_SOUND 288020, two sets at most', () => {
  assert.equal(limits.MAX_SOUND, 20 + 3 * 48000 * 2);
  assert.equal(limits.MAX_SOUNDS, 2);
});

test('a pack with sounds carries alert.snd last (after any video), byte for byte', () => {
  const t = makeTemplate('cosmic');
  const p = buildPack(t.theme, t.emblem, null, sounds());
  assert.deepEqual(p.files.map(name), ['theme.json', 'emblem.bin', 'alert.snd']);
  assert.deepEqual(Array.from(p.files[2].data), Array.from(soundFile(sounds())));
  assert.deepEqual(buildPack(t.theme, t.emblem, clip(), sounds()).files.map(name), ['theme.json', 'emblem.bin', 'radar.vid', 'alert.snd']);
  assert.equal(p.bytes, p.files.reduce((n, f) => n + f.data.length, 0));
});

test('no sounds, or an empty set, is exactly the pack it was; a bad set is refused', () => {
  const t = makeTemplate('umbravox');
  assert.deepEqual(buildPack(t.theme, null).files.map(name), ['theme.json']);
  assert.deepEqual(buildPack(t.theme, null, null, null).files.map(name), ['theme.json']);
  assert.deepEqual(buildPack(t.theme, null, null, emptySounds()).files.map(name), ['theme.json']);
  assert.throws(() => buildPack(t.theme, null, null, { low: new Int16Array(48001), med: null, high: null }), /48000/);
});

test('checks report the sounds, and hold them to the limit', () => {
  const t = makeTemplate('umbravox');
  const text = (r) => r.map((c) => c.text).join('\n');
  assert.ok(!/alert sounds/i.test(text(runChecks(t.theme, null))));
  const r = runChecks(t.theme, null, null, sounds(3200, 0, 16000));
  assert.match(text(r), /Alert sounds: LOW 0\.2 s, MED built-in, HIGH 1\.0 s/);
  assert.match(text(r), /Waveshare/);
  assert.ok(r.every((c) => c.level !== 'error'));
  const bad = runChecks(t.theme, null, null, { low: new Int16Array(48001), med: null, high: null });
  assert.ok(bad.some((c) => c.level === 'error' && /48000/.test(c.text)));
});

test('the install gate sees a bad sound set', () => {
  const t = makeTemplate('umbravox');
  assert.equal(installGate(t.theme, null, null, sounds()).ok, true);
  assert.equal(installGate(t.theme, null, null, { low: new Int16Array(48001), med: null, high: null }).ok, false);
});

test('opening a pack folder brings its sounds back, and a bad alert.snd is dropped with a warning', () => {
  const t = makeTemplate('umbravox');
  const files = buildPack(t.theme, null, null, sounds()).files.map((f) => ({ name: f.path, data: f.data }));
  const r = openPackFiles(files);
  assert.equal(r.ok, true, r.error);
  assert.deepEqual(Array.from(r.sounds.high), Array.from(sounds().high));
  assert.equal(r.sounds.med, null);
  const broken = files.map((f) => (f.name.endsWith('alert.snd') ? { ...f, data: f.data.slice(0, 50) } : f));
  const b = openPackFiles(broken);
  assert.equal(b.ok, true); assert.equal(b.sounds, null);
  assert.ok(b.warnings.some((w) => /alert\.snd/.test(w) && /left out/.test(w)));
  assert.equal(openPackFiles(files.filter((f) => !f.name.endsWith('alert.snd'))).sounds, null);
});

test('selecting files takes alert.snd from the same folder and refuses an oversize one', () => {
  const f = (path, size) => ({ name: path.split('/').pop(), webkitRelativePath: path, size, data: new Uint8Array(0) });
  const ok = selectPackFiles([f('p/theme.json', 100), f('p/alert.snd', 5000)]);
  assert.equal(ok.ok, true); assert.ok(ok.picked.some((x) => x.name === 'alert.snd'));
  assert.equal(selectPackFiles([f('p/theme.json', 100), f('q/alert.snd', 5000)]).ok, false);
  const big = selectPackFiles([f('p/theme.json', 100), f('p/alert.snd', limits.MAX_SOUND + 1)]);
  assert.equal(big.ok, false); assert.match(big.error, /alert\.snd/);
});
