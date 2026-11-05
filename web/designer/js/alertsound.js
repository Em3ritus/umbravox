// The alert sounds a pack can carry: alert.snd (device side: ui/themepack.h parseSound). Three optional sounds, one per
// alert level (LOW, MED, HIGH), as 16 kHz mono 16-bit samples, up to 6 seconds each. A level without a sound keeps the
// board's built-in beep. This file holds the pure parts (conversion, file writer and reader), which Node tests; the
// browser-only decoding and preview playback are at the bottom.
export const SND_RATE = 16000;
export const MAX_SND_SAMPLES = 96000;                              // 6 s
export const SND_HEADER = 20;
export const MAX_SOUND_FILE = SND_HEADER + 3 * MAX_SND_SAMPLES * 2; // 576020, limits.json MAX_SOUND
export const LEVELS = ['low', 'med', 'high'];

export const emptySounds = () => ({ low: null, med: null, high: null });
export const hasSounds = (s) => !!s && LEVELS.some((k) => s[k] && s[k].length > 0);
export const soundSeconds = (pcm) => (pcm ? pcm.length / SND_RATE : 0);

// Float samples (-1..1, 16 kHz mono) to the stored form: leading and trailing silence trimmed, cut at 6 s, normalized to
// 70% of full scale (the board's NORMAL volume plays samples as stored), 5 ms fades in and out. null if there is nothing
// audible.
export function prepareSound(f, { peak = 0.7, silence = 0.005, fadeMs = 5 } = {}) {
  let a = 0, b = f.length;
  while (a < b && Math.abs(f[a]) <= silence) a++;
  while (b > a && Math.abs(f[b - 1]) <= silence) b--;
  if (b <= a) return null;
  b = Math.min(b, a + MAX_SND_SAMPLES);
  let m = 0;
  for (let i = a; i < b; i++) m = Math.max(m, Math.abs(f[i]));
  if (m === 0) return null;
  const n = b - a, gain = peak / m, fade = Math.max(1, Math.round(SND_RATE * fadeMs / 1000));
  const out = new Int16Array(n);
  for (let i = 0; i < n; i++) {
    const e = Math.min(1, Math.min(i, n - 1 - i) / fade);
    out[i] = Math.max(-32767, Math.min(32767, Math.round(f[a + i] * gain * e * 32767)));
  }
  return out;
}

// The sound starting `startSec` into a longer recording (16 kHz floats), prepared as above. A negative or junk start
// means the start; past the end is nothing (null).
export function cutSound(f, startSec = 0, opts) {
  const at = Number.isFinite(startSec) && startSec > 0 ? Math.round(startSec * SND_RATE) : 0;
  return at >= f.length ? null : prepareSound(f.subarray(at), opts);
}

// null when fine, else the reason (the device's refusals).
export function validateSounds(s) {
  if (!hasSounds(s)) return 'alert.snd has no sounds';
  for (const k of LEVELS) if (s[k] && s[k].length > MAX_SND_SAMPLES) return 'alert.snd has a sound over ' + MAX_SND_SAMPLES + ' samples';
  return null;
}

export function soundFile(s) {
  const why = validateSounds(s);
  if (why) throw new Error(why);
  const n = LEVELS.map((k) => (s[k] ? s[k].length : 0));
  const out = new Uint8Array(SND_HEADER + 2 * (n[0] + n[1] + n[2]));
  out.set([0x55, 0x56, 0x53, 0x4E, 1, 0, SND_RATE & 255, SND_RATE >> 8]);
  const dv = new DataView(out.buffer);
  n.forEach((c, k) => dv.setUint32(8 + 4 * k, c, true));
  let off = SND_HEADER;
  LEVELS.forEach((k) => { if (s[k]) for (let i = 0; i < s[k].length; i++, off += 2) dv.setInt16(off, s[k][i], true); });
  return out;
}

// An alert.snd back into sounds, with the device's refusals in the device's order. Throws Error(reason).
export function readSoundFile(b) {
  if (!b || b.length < SND_HEADER) throw new Error('alert.snd is too short');
  if (b[0] !== 0x55 || b[1] !== 0x56 || b[2] !== 0x53 || b[3] !== 0x4E) throw new Error('alert.snd is not a UVSN file');
  if (b[4] !== 1) throw new Error('alert.snd version is not 1');
  if (b[5] !== 0) throw new Error('alert.snd reserved bytes are not zero');
  if ((b[6] | (b[7] << 8)) !== SND_RATE) throw new Error('alert.snd rate must be 16000');
  const dv = new DataView(b.buffer, b.byteOffset, b.byteLength);
  const n = [0, 1, 2].map((k) => dv.getUint32(8 + 4 * k, true));
  if (n.some((c) => c > MAX_SND_SAMPLES)) throw new Error('alert.snd has a sound over ' + MAX_SND_SAMPLES + ' samples');
  const total = n[0] + n[1] + n[2];
  if (total === 0) throw new Error('alert.snd has no sounds');
  if (b.length !== SND_HEADER + 2 * total) throw new Error('alert.snd length does not match its sounds');
  const out = emptySounds();
  let off = SND_HEADER;
  LEVELS.forEach((k, i) => {
    if (!n[i]) return;
    const pcm = new Int16Array(n[i]);
    for (let j = 0; j < n[i]; j++, off += 2) pcm[j] = dv.getInt16(off, true);
    out[k] = pcm;
  });
  return out;
}

// ---- browser only ------------------------------------------------------------------------------------------------
// Any audio file the browser can decode (wav, mp3, ogg, m4a...) to 16 kHz mono floats, resampled by the browser.
export async function decodeToMono16k(file) {
  const Ctx = globalThis.AudioContext || globalThis.webkitAudioContext;
  if (!Ctx) throw new Error('this browser cannot decode audio');
  const ctx = new Ctx();
  let buf;
  try { buf = await ctx.decodeAudioData(await file.arrayBuffer()); }
  catch { throw new Error('the browser could not read this audio file'); }
  finally { ctx.close && ctx.close(); }
  const len = Math.max(1, Math.ceil(buf.duration * SND_RATE));
  const off = new OfflineAudioContext(1, len, SND_RATE);                  // mixes the channels down and resamples
  const src = off.createBufferSource(); src.buffer = buf; src.connect(off.destination); src.start();
  return (await off.startRendering()).getChannelData(0);
}

let playCtx = null;
// Plays stored samples through the speakers, for the preview buttons.
export function playPcm(pcm, gain = 1) {
  const Ctx = globalThis.AudioContext || globalThis.webkitAudioContext;
  if (!Ctx || !pcm || !pcm.length) return;
  playCtx = playCtx || new Ctx();
  const buf = playCtx.createBuffer(1, pcm.length, SND_RATE), ch = buf.getChannelData(0);
  for (let i = 0; i < pcm.length; i++) ch[i] = Math.max(-1, Math.min(1, pcm[i] / 32768 * gain));
  const src = playCtx.createBufferSource(); src.buffer = buf; src.connect(playCtx.destination); src.start();
}
