// artfmt.js: the device's .art picture (ui/artpack.h): decor, backgrounds, panel frames and menu icons, converted
// from PNG or GIF at exactly the size it is placed (the device never scales art).
//
// An art here is { w, h, frames, ncol, transparent, pal: [RGB565 x16], delays: [ms], px: [Uint8Array per frame] }
// with px holding 4-bit indices, two per byte, high nibble first.
import { quantizeColour } from './radarvideo.js';

export const MAX_ART = 196608, MAX_ART_FRAMES = 64, MAX_ART_W = 480, MAX_ART_H = 320, HEADER = 16;
export const ART_MIN_DELAY_MS = { waveshare: 50, cyd: 100 };   // ui/scan_render.h artMinDelay
export const ANIM_AREA_MAX = { waveshare: 25600, cyd: 10000 };   // ui/scan_render.h animAreaMax

export const artBytes = (w, h, frames, ncol) => HEADER + ncol * 2 + frames * 2 + frames * Math.ceil(w * h / 2);

// Cover-crop the source to the target's aspect (centred), then box-average down (or repeat up) to exactly w x h.
export function resizeCover(src, sw, sh, w, h) {
  const scale = Math.max(w / sw, h / sh);
  const cw = w / scale, ch = h / scale, cx = (sw - cw) / 2, cy = (sh - ch) / 2;
  const out = new Uint8ClampedArray(w * h * 4);
  for (let y = 0; y < h; y++) {
    const y0 = cy + y * ch / h, y1 = cy + (y + 1) * ch / h;
    for (let x = 0; x < w; x++) {
      const x0 = cx + x * cw / w, x1 = cx + (x + 1) * cw / w;
      const xa = Math.floor(x0), xb = Math.max(xa + 1, Math.ceil(x1)), ya = Math.floor(y0), yb = Math.max(ya + 1, Math.ceil(y1));
      const acc = [0, 0, 0, 0]; let n = 0;
      for (let yy = ya; yy < yb && yy < sh; yy++) for (let xx = xa; xx < xb && xx < sw; xx++) {
        const i = 4 * (yy * sw + xx);
        for (let c = 0; c < 4; c++) acc[c] += src[i + c];
        n++;
      }
      for (let c = 0; c < 4; c++) out[4 * (y * w + x) + c] = Math.round(acc[c] / Math.max(1, n));
    }
  }
  return out;
}

export function imageToArt(rgbaFrames, srcW, srcH, { w, h, delays = [], transparent = false, minDelay = ART_MIN_DELAY_MS.waveshare } = {}) {
  if (!Number.isInteger(w) || !Number.isInteger(h) || w < 1 || h < 1 || w > MAX_ART_W || h > MAX_ART_H) throw new Error(`a picture is 1x1 to ${MAX_ART_W}x${MAX_ART_H} pixels`);
  if (!rgbaFrames.length) throw new Error('a picture needs at least one frame');
  if (rgbaFrames.length > MAX_ART_FRAMES) throw new Error(`a picture has at most ${MAX_ART_FRAMES} frames`);
  if (artBytes(w, h, rgbaFrames.length, 16) > MAX_ART)
    throw new Error(`${rgbaFrames.length} frames at ${w}x${h} come to ${artBytes(w, h, rgbaFrames.length, 16)} bytes, over the device's ${MAX_ART}`);
  const frames = rgbaFrames.map((f) => (srcW === w && srcH === h ? f : resizeCover(f, srcW, srcH, w, h)));
  const n = w * h;
  const opaque = (f, i) => !transparent || f[4 * i + 3] >= 128;
  // One palette over every frame's visible pixels; with transparency, index 0 is kept for "not drawn".
  const count = frames.reduce((t, f) => { let k = 0; for (let i = 0; i < n; i++) if (opaque(f, i)) k++; return t + k; }, 0);
  const strip = new Uint8ClampedArray(Math.max(1, count) * 4);
  let o = 0;
  for (const f of frames) for (let i = 0; i < n; i++) if (opaque(f, i)) { strip.set(f.subarray(4 * i, 4 * i + 4), 4 * o); strip[4 * o + 3] = 255; o++; }
  const q = quantizeColour([strip], { width: Math.max(1, count), height: 1, dither: false, colours: transparent ? 15 : 16 });
  const pal16 = []; for (let i = 0; i < 16; i++) pal16.push(q.palette[2 * i] | (q.palette[2 * i + 1] << 8));
  const pal = transparent ? [0x0000, ...pal16.slice(0, 15)] : pal16;
  const shift = transparent ? 1 : 0;
  let k = 0;
  const px = frames.map((f) => {
    const out = new Uint8Array(Math.ceil(n / 2));
    for (let i = 0; i < n; i++) {
      const v = opaque(f, i) ? q.frames[0][k++] + shift : 0;
      out[i >> 1] |= i & 1 ? v : v << 4;
    }
    return out;
  });
  const ds = frames.map((_, i) => Math.min(65535, Math.max(minDelay, Math.round(delays[i] || 0))));
  return { w, h, frames: frames.length, ncol: 16, transparent, pal, delays: ds, px };
}

export function artToBin(a) {
  const out = new Uint8Array(artBytes(a.w, a.h, a.frames, a.ncol));
  out.set([0x55, 0x56, 0x41, 0x52, 1, a.transparent ? 1 : 0, a.w & 255, a.w >> 8, a.h & 255, a.h >> 8, a.frames & 255, a.frames >> 8, a.ncol, 0, 0, 0]);
  let o = HEADER;
  for (let i = 0; i < a.ncol; i++) { out[o++] = a.pal[i] & 255; out[o++] = a.pal[i] >> 8; }
  for (const d of a.delays) { out[o++] = d & 255; out[o++] = d >> 8; }
  for (const f of a.px) { out.set(f, o); o += f.length; }
  return out;
}

export function readArt(b) {
  if (!b || b.length < HEADER) throw new Error('picture file too short');
  if (b[0] !== 0x55 || b[1] !== 0x56 || b[2] !== 0x41 || b[3] !== 0x52) throw new Error('not a picture file');
  if (b[4] !== 1) throw new Error('unsupported picture version');
  const w = b[6] | (b[7] << 8), h = b[8] | (b[9] << 8), frames = b[10] | (b[11] << 8), ncol = b[12];
  if (b.length !== artBytes(w, h, frames, ncol)) throw new Error('picture file length does not match its header');
  let o = HEADER;
  const pal = [], delays = [], px = [];
  for (let i = 0; i < ncol; i++) { pal.push(b[o] | (b[o + 1] << 8)); o += 2; }
  for (let i = 0; i < frames; i++) { delays.push(b[o] | (b[o + 1] << 8)); o += 2; }
  const fb = Math.ceil(w * h / 2);
  for (let i = 0; i < frames; i++) { px.push(b.slice(o, o + fb)); o += fb; }
  return { w, h, frames, ncol, transparent: (b[5] & 1) === 1, pal, delays, px };
}

// A GIF's frames at their own timing (not resampled), cover-cropped to w x h with transparency kept. Browser only.
export async function grabGifArt(file, { w, h } = {}) {
  if (typeof ImageDecoder === 'undefined') throw new Error('this browser cannot read GIFs (use Chrome, Edge or Safari)');
  const dec = new ImageDecoder({ data: file.stream(), type: 'image/gif' });
  try {
    await dec.tracks.ready;
    const n = Math.min(dec.tracks.selectedTrack.frameCount, MAX_ART_FRAMES);
    const cv = new OffscreenCanvas(w, h), ctx = cv.getContext('2d', { willReadFrequently: true });
    const frames = [], delays = [];
    for (let i = 0; i < n; i++) {
      const { image } = await dec.decode({ frameIndex: i });
      const sw = image.displayWidth, sh = image.displayHeight, scale = Math.max(w / sw, h / sh);
      const cw = w / scale, ch = h / scale;
      ctx.clearRect(0, 0, w, h);
      ctx.drawImage(image, (sw - cw) / 2, (sh - ch) / 2, cw, ch, 0, 0, w, h);
      frames.push(Uint8ClampedArray.from(ctx.getImageData(0, 0, w, h).data));
      delays.push(image.duration ? image.duration / 1000 : 100);
      image.close();
    }
    return { frames, delays };
  } finally { dec.close(); }
}
