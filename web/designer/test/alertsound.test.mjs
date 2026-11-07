// The alert sounds a pack can carry (alert.snd): the browser's conversion and file writer, held to the device's parser
// (themepack::parseSound, through sim/snd_check) byte for byte.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import { cutSound, SND_RATE, MAX_SND_SAMPLES, MAX_SOUND_FILE, prepareSound, soundFile, readSoundFile, validateSounds, emptySounds, hasSounds, soundSeconds } from '../js/alertsound.js';

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const sine = (hz, secs, amp = 0.5) => Float32Array.from({ length: Math.round(SND_RATE * secs) }, (_, i) => amp * Math.sin(2 * Math.PI * hz * i / SND_RATE));

test('constants match the device', () => {
  assert.equal(SND_RATE, 16000);
  assert.equal(MAX_SND_SAMPLES, 96000);
  assert.equal(MAX_SOUND_FILE, 20 + 3 * 96000 * 2);
});

test('prepareSound: trims silence, normalizes the peak, fades the edges, caps at 6 s', () => {
  const quiet = new Float32Array(SND_RATE * 2);
  quiet.set(sine(1000, 0.5, 0.1), 8000);                          // 0.5 s of tone, starting 0.5 s in, inside 2 s of silence
  const p = prepareSound(quiet);
  assert.ok(p instanceof Int16Array);
  assert.ok(p.length > 7000 && p.length < 8400, 'the silence around it is trimmed (got ' + p.length + ')');
  const peak = Math.max(...Array.from(p, Math.abs));
  assert.ok(peak > 0.68 * 32767 && peak <= 0.72 * 32767, 'normalized to about 70% of full scale (got ' + peak + ')');
  assert.ok(Math.abs(p[0]) < 600 && Math.abs(p[p.length - 1]) < 600, 'fades in and out');
  assert.equal(prepareSound(sine(500, 10)).length, MAX_SND_SAMPLES, 'ten seconds is cut at six');
  assert.equal(prepareSound(new Float32Array(5000)), null, 'pure silence is nothing');
  assert.equal(prepareSound(new Float32Array(0)), null);
  const loud = sine(300, 0.2, 4);                                  // way past full scale
  assert.ok(Math.max(...Array.from(prepareSound(loud), Math.abs)) <= 32767);
});

test('soundFile and readSoundFile round-trip, and match the layout', () => {
  const s = emptySounds(); s.low = Int16Array.from([1, -2, 3]); s.high = Int16Array.from([100, -200]);
  assert.equal(hasSounds(s), true); assert.equal(hasSounds(emptySounds()), false);
  const f = soundFile(s);
  assert.equal(f.length, 20 + 2 * 5);
  assert.deepEqual(Array.from(f.slice(0, 8)), [0x55, 0x56, 0x53, 0x4E, 1, 0, 16000 & 255, 16000 >> 8]);
  assert.deepEqual(Array.from(f.slice(8, 20)), [3, 0, 0, 0, 0, 0, 0, 0, 2, 0, 0, 0]);
  const back = readSoundFile(f);
  assert.deepEqual(Array.from(back.low), [1, -2, 3]); assert.equal(back.med, null); assert.deepEqual(Array.from(back.high), [100, -200]);
  assert.ok(Math.abs(soundSeconds(s.low) - 3 / 16000) < 1e-9);
  assert.throws(() => soundFile(emptySounds()), /no sounds/);
});

test('validateSounds refuses what the device would', () => {
  const s = emptySounds(); assert.match(validateSounds(s), /no sounds/);
  s.med = new Int16Array(96001); assert.match(validateSounds(s), /96000/);
  s.med = new Int16Array(96000); assert.equal(validateSounds(s), null);
  s.low = new Int16Array(96000); s.high = new Int16Array(96000);
  assert.equal(soundFile(s).length, MAX_SOUND_FILE);
});

test('readSoundFile refuses like the device', () => {
  const good = soundFile({ low: Int16Array.from([1, 2, 3, 4]), med: null, high: Int16Array.from([5, 6]) });
  const bad = (f) => { const x = Uint8Array.from(good); f(x); return x; };
  assert.throws(() => readSoundFile(bad((x) => { x[0] = 0x58; })), /UVSN/);
  assert.throws(() => readSoundFile(bad((x) => { x[4] = 2; })), /version/);
  assert.throws(() => readSoundFile(bad((x) => { x[5] = 1; })), /reserved/);
  assert.throws(() => readSoundFile(bad((x) => { x[6] = 0x22; x[7] = 0x56; })), /16000/);
  assert.throws(() => readSoundFile(bad((x) => { x[8] = 0x01; x[9] = 0x77; x[10] = 1; })), /96000/);
  assert.throws(() => readSoundFile(good.slice(0, good.length - 1)), /length/);
  assert.throws(() => readSoundFile(good.slice(0, 10)), /short/);
  assert.throws(() => readSoundFile(Uint8Array.from(good.slice(0, 20)).fill(0, 8, 20)), /no sounds|length/);
});

// The writer against the device's parser (the same C++ the board runs).
const CHECK = path.join(ROOT, 'sim/snd_check');
test('the device parser accepts what the designer writes, and refuses what readSoundFile refuses', () => {
  const m = spawnSync('make', ['-C', path.join(ROOT, 'sim'), 'snd_check'], { encoding: 'utf8' });
  assert.equal(m.status, 0, m.stderr);
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'snd-'));
  const run = (bytes) => { const f = path.join(dir, 'a.snd'); fs.writeFileSync(f, bytes); return spawnSync(CHECK, [f], { encoding: 'utf8' }).stdout.trim(); };
  const cases = [
    { low: prepareSound(sine(1000, 0.3)), med: null, high: null },
    { low: null, med: prepareSound(sine(700, 1)), high: prepareSound(sine(1500, 3)) },
    { low: new Int16Array(96000).fill(7), med: new Int16Array(96000).fill(-7), high: new Int16Array(96000).fill(9) },
    { low: Int16Array.from([1]), med: null, high: null },
  ];
  for (const s of cases) {
    const f = soundFile(s);
    assert.equal(run(f), `OK ${s.low ? s.low.length : 0} ${s.med ? s.med.length : 0} ${s.high ? s.high.length : 0}`);
  }
  // mutate headers: the browser reader and the device parser must agree on accept/refuse
  const base = soundFile(cases[1]);
  let disagree = 0;
  for (let i = 0; i < 20; i++) for (const m of [1, 2, 0x80, 0xFF]) {
    const x = Uint8Array.from(base); x[i] ^= m;
    let js = true; try { readSoundFile(x); } catch { js = false; }
    if (js !== run(x).startsWith('OK')) disagree++;
  }
  assert.equal(disagree, 0, 'reader and device parser agree on every header mutation');
  fs.rmSync(dir, { recursive: true });
});

test('cutSound: the sound starts where you say, from a longer recording', () => {
  // 1 s of 500 Hz, then 1 s of 1500 Hz, then 8 s of 3000 Hz
  const a = sine(500, 1), b = sine(1500, 1), c = sine(3000, 8);
  const song = new Float32Array(a.length + b.length + c.length); song.set(a, 0); song.set(b, a.length); song.set(c, a.length + b.length);
  const hz = (p) => { let zc = 0; for (let i = 1; i < p.length; i++) zc += (p[i - 1] < 0) !== (p[i] < 0); return zc / 2 / (p.length / SND_RATE); };
  const near = (x, y) => Math.abs(x - y) < 0.04 * y;
  assert.ok(near(hz(cutSound(song, 0).subarray(0, 8000)), 500), 'from the start');
  assert.ok(near(hz(cutSound(song, 1).subarray(2000, 10000)), 1500), 'from 1 s in');
  assert.ok(near(hz(cutSound(song, 2.5)), 3000), 'from 2.5 s in');
  assert.equal(cutSound(song, 0).length, MAX_SND_SAMPLES, 'still capped at the limit');
  assert.ok(Math.abs(cutSound(song, 9).length - 16000) <= 3, 'near the end it is what is left (1 s, give or take the trimmed edge samples)');
  assert.ok(near(hz(cutSound(song, -5).subarray(0, 8000)), 500), 'a negative start means the start');
  assert.equal(cutSound(song, 100), null, 'past the end is nothing');
  assert.equal(cutSound(song, NaN) !== null, true, 'a junk start means the start');
});
