// The radar clip: what the designer turns a video into so the Waveshare preview can play it in the radar. The same
// format the board plays (ui/scan_render.h RadarVideo, made by tools/video2radar.py): 102x102 px, 12 fps, 16 grey
// levels, two pixels per byte with the high nibble first. This module is pure (no DOM); the browser step that fills
// frames from a <video> element is `grabFrames`, kept at the bottom.
export const CLIP_SIZE = 102;
export const CLIP_FPS = 12;
export const MAX_BYTES = 786432;                                   // the wasm's clip buffer (bridge.cpp)
export const FRAME_BYTES = Math.ceil(CLIP_SIZE * CLIP_SIZE / 2);   // 5202
export const MAX_FRAMES = Math.floor(MAX_BYTES / FRAME_BYTES);     // 151; capped at 150 = 12.5 s
export const PAL_BYTES = 32;                                       // version 2: 16 RGB565 colours, little-endian
export const MAX_COLOUR_FRAMES = 149;                              // 12 + 32 + 149 frames is the most that fits the 780312-byte limit

// BT.601 luma of an RGBA buffer (the weights ffmpeg's gray uses), one byte per pixel.
export function lumaFromRgba(rgba) {
  const n = rgba.length >> 2, g = new Uint8Array(n);
  for (let i = 0; i < n; i++) g[i] = Math.round(0.299 * rgba[4 * i] + 0.587 * rgba[4 * i + 1] + 0.114 * rgba[4 * i + 2]);
  return g;
}

// Gray (0..255) to 16 levels: `black` becomes 0, `white` becomes 15, clamped.
export function levelsFromGray(gray, black = 24, white = 235) {
  if (!(white > black)) throw new Error('the white point must be above the black point');
  const out = new Uint8Array(gray.length), span = white - black;
  for (let i = 0; i < gray.length; i++) {
    const q = Math.min(1, Math.max(0, (gray[i] - black) / span));
    out[i] = Math.round(q * 15);
  }
  return out;
}

// Two levels per byte, high nibble first; an odd count is padded with a 0 nibble.
export function packLevels(levels) {
  const out = new Uint8Array((levels.length + 1) >> 1);
  for (let i = 0; i < levels.length; i += 2) out[i >> 1] = (levels[i] << 4) | (i + 1 < levels.length ? levels[i + 1] : 0);
  return out;
}

// Frames of CLIP_SIZE*CLIP_SIZE levels each -> the clip the wasm takes.
export function buildClip(frames, palette = null) {
  if (!frames.length) throw new Error('no frames');
  if (palette && palette.length !== PAL_BYTES) throw new Error(`the clip palette must be ${PAL_BYTES} bytes`);
  const most = palette ? MAX_COLOUR_FRAMES : Math.min(150, MAX_FRAMES);
  if (frames.length > most) throw new Error(`too many frames (${frames.length}; at most ${most}${palette ? ' with colours' : ''})`);
  const data = new Uint8Array(frames.length * FRAME_BYTES);
  frames.forEach((f, k) => {
    if (f.length !== CLIP_SIZE * CLIP_SIZE) throw new Error(`frame ${k} is the wrong size (${f.length} px)`);
    data.set(packLevels(f), k * FRAME_BYTES);
  });
  const clip = { w: CLIP_SIZE, h: CLIP_SIZE, frames: frames.length, fps: CLIP_FPS, data };
  if (palette) clip.palette = Uint8Array.from(palette);
  return clip;
}

// radar.vid: the file a pack carries (ui/themepack.h parseClip): 'U','V','R','V', version 1, w, h, fps, frames (u16 LE),
// two reserved zero bytes, then the frames. Version 2 puts a 32-byte palette (16 RGB565, little-endian) after the header
// and the 4-bit pixels index it, keeping the video's own colours; version 1 is grey levels in the theme's colours.
export const CLIP_HEADER = 12;
export const MAX_CLIP_FILE = CLIP_HEADER + 150 * FRAME_BYTES;      // 780312, limits.json MAX_CLIP

export function clipFile(c) {
  const why = validateClip(c);
  if (why) throw new Error(why);
  const head = CLIP_HEADER + (c.palette ? PAL_BYTES : 0);
  const out = new Uint8Array(head + c.data.length);
  out.set([0x55, 0x56, 0x52, 0x56, c.palette ? 2 : 1, c.w, c.h, c.fps, c.frames & 255, c.frames >> 8, 0, 0]);
  if (c.palette) out.set(c.palette, CLIP_HEADER);
  out.set(c.data, head);
  return out;
}

// A radar.vid back into a clip, with the same refusals as the device's parser. Throws Error(reason).
export function readClipFile(b) {
  if (!b || b.length < CLIP_HEADER) throw new Error('radar.vid is too short');
  if (b[0] !== 0x55 || b[1] !== 0x56 || b[2] !== 0x52 || b[3] !== 0x56) throw new Error('radar.vid is not a UVRV file');
  if (b[4] !== 1 && b[4] !== 2) throw new Error('radar.vid version is not 1 or 2');
  const colour = b[4] === 2;
  if (b[5] !== CLIP_SIZE || b[6] !== CLIP_SIZE) throw new Error(`radar.vid must be ${CLIP_SIZE}x${CLIP_SIZE}`);
  if (b[7] < 1 || b[7] > 30) throw new Error('radar.vid fps must be 1 to 30');
  const head = CLIP_HEADER + (colour ? PAL_BYTES : 0);
  if (b.length < head) throw new Error('radar.vid is too short');
  const frames = b[8] | (b[9] << 8);
  const most = colour ? MAX_COLOUR_FRAMES : 150;
  if (frames < 1 || frames > most) throw new Error('radar.vid frames must be 1 to ' + most);
  if (b[10] || b[11]) throw new Error('radar.vid reserved bytes are not zero');
  if (b.length !== head + frames * FRAME_BYTES) throw new Error('radar.vid length does not match its frames');
  const clip = { w: b[5], h: b[6], frames, fps: b[7], data: Uint8Array.from(b.subarray(head)) };
  if (colour) clip.palette = Uint8Array.from(b.subarray(CLIP_HEADER, head));
  return clip;
}

// null when the clip is playable, else why not.
export function validateClip(c) {
  if (!c || c.w !== CLIP_SIZE || c.h !== CLIP_SIZE) return `the clip must be ${CLIP_SIZE}x${CLIP_SIZE}`;
  if (!(c.frames >= 1)) return 'the clip has no frames';
  if (!(c.fps >= 1 && c.fps <= 30)) return 'the clip fps must be 1 to 30';
  if (c.data.length !== c.frames * FRAME_BYTES) return `the clip has ${c.data.length} bytes, expected ${c.frames * FRAME_BYTES}`;
  if (c.data.length > MAX_BYTES) return 'the clip is over the size limit';
  if (c.palette) {
    if (c.palette.length !== PAL_BYTES) return `the clip palette must be ${PAL_BYTES} bytes`;
    if (c.frames > MAX_COLOUR_FRAMES) return `a colour clip has at most ${MAX_COLOUR_FRAMES} frames`;
  }
  return null;
}

// ---- colour -------------------------------------------------------------------------------------------------------
// The captured frames are RGBA; they become either grey levels (greyFrames, painted in the theme's colours) or indexes
// into one 16-colour palette for the whole clip (quantizeColour, kept as the video's own colours).
export function greyFrames(rgbaFrames, black = 24, white = 235) {
  return rgbaFrames.map((f) => levelsFromGray(lumaFromRgba(f), black, white));
}

const BAYER = [0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5];       // 4x4 ordered dither: depends on position only, so it does not shimmer from frame to frame
const W3 = [3, 4, 2];                                                         // how much an error in R, G, B shows

// One palette for all frames by median cut over the colours actually used (counted at RGB565, the board's depth),
// then every pixel -> nearest palette entry, optionally with ordered dithering. Deterministic. Returns
// { palette: Uint8Array(32) (16 RGB565, little-endian), frames: Uint8Array(w*h) of indexes 0..15 per frame }.
export function quantizeColour(rgbaFrames, { dither = true, strength = 20 } = {}) {
  const n = CLIP_SIZE * CLIP_SIZE;
  const hist = new Map();                                                     // rgb565 -> count
  const to565 = (r, g, b) => ((Math.round(r * 31 / 255) << 11) | (Math.round(g * 63 / 255) << 5) | Math.round(b * 31 / 255));
  for (const f of rgbaFrames) for (let i = 0; i < n; i++) { const k = to565(f[4 * i], f[4 * i + 1], f[4 * i + 2]); hist.set(k, (hist.get(k) || 0) + 1); }
  const expand = (k) => [((k >> 11) & 31) * 255 / 31, ((k >> 5) & 63) * 255 / 63, (k & 31) * 255 / 31];
  const entries = [...hist.entries()].sort((a, b) => a[0] - b[0]).map(([k, c]) => ({ k, c, rgb: expand(k) }));
  let boxes = [entries];
  const spread = (box) => { let best = 0, ch = 0; for (let c = 0; c < 3; c++) { let lo = 255, hi = 0; for (const e of box) { lo = Math.min(lo, e.rgb[c]); hi = Math.max(hi, e.rgb[c]); } const s = (hi - lo) * W3[c]; if (s > best) { best = s; ch = c; } } return { best, ch }; };
  while (boxes.length < 16) {
    let pick = -1, score = 0, pch = 0;
    boxes.forEach((b, i) => { if (b.length < 2) return; const { best, ch } = spread(b); const s = best * b.reduce((t, e) => t + e.c, 0); if (s > score) { score = s; pick = i; pch = ch; } });
    if (pick < 0) break;
    const b = boxes[pick].slice().sort((x, y) => x.rgb[pch] - y.rgb[pch] || x.k - y.k);
    const total = b.reduce((t, e) => t + e.c, 0);
    let acc = 0, cut = 1;
    for (let i = 0; i < b.length - 1; i++) { acc += b[i].c; cut = i + 1; if (acc * 2 >= total) break; }
    boxes.splice(pick, 1, b.slice(0, cut), b.slice(cut));
  }
  const pal565 = boxes.map((b) => {
    let t = 0, r = 0, g = 0, bl = 0;
    for (const e of b) { t += e.c; r += e.rgb[0] * e.c; g += e.rgb[1] * e.c; bl += e.rgb[2] * e.c; }
    return to565(r / t, g / t, bl / t);
  });
  while (pal565.length < 16) pal565.push(pal565[0]);
  const palette = new Uint8Array(PAL_BYTES);
  pal565.forEach((k, i) => { palette[2 * i] = k & 255; palette[2 * i + 1] = k >> 8; });
  const pr = pal565.map(expand);
  const nearest = (r, g, b) => { let bi = 0, bd = Infinity; for (let i = 0; i < 16; i++) { const d = W3[0] * (pr[i][0] - r) ** 2 + W3[1] * (pr[i][1] - g) ** 2 + W3[2] * (pr[i][2] - b) ** 2; if (d < bd) { bd = d; bi = i; } } return bi; };
  const cache = new Map();
  const frames = rgbaFrames.map((f) => {
    const out = new Uint8Array(n);
    for (let i = 0; i < n; i++) {
      const r = f[4 * i], g = f[4 * i + 1], b = f[4 * i + 2];
      if (dither) {
        const bias = ((BAYER[(i % CLIP_SIZE & 3) + 4 * (Math.floor(i / CLIP_SIZE) & 3)] + 0.5) / 16 - 0.5) * strength;
        out[i] = nearest(r + bias, g + bias, b + bias);
      } else {
        const k = to565(r, g, b);
        let v = cache.get(k); if (v === undefined) { v = nearest(r, g, b); cache.set(k, v); }
        out[i] = v;
      }
    }
    return out;
  });
  return { palette, frames };
}

// Browser only: seek a <video> through its length at CLIP_FPS, crop a square (full width, `vpos` 0..1 from the top
// to the bottom of the spare height) and scale it to CLIP_SIZE. Resolves to an array of RGBA frames.
// onProgress(done, total) is called after each frame. Never touched by the Node tests.
export async function grabFrames(video, { vpos = 0.5, maxSeconds = 12, onProgress } = {}) {
  const cv = document.createElement('canvas'); cv.width = cv.height = CLIP_SIZE;
  const ctx = cv.getContext('2d', { willReadFrequently: true });
  ctx.imageSmoothingEnabled = true; ctx.imageSmoothingQuality = 'high';
  if (!video.videoWidth || !video.videoHeight) throw new Error('this browser cannot show the picture of this video (some codecs, like HEVC, only play with sound here): try another browser, or convert it to H.264 or a GIF');
  const side = Math.min(video.videoWidth, video.videoHeight);
  const sx = (video.videoWidth - side) / 2, sy = (video.videoHeight - side) * vpos;
  const secs = Math.min(video.duration, maxSeconds), total = Math.max(1, Math.min(150, Math.floor(secs * CLIP_FPS)));
  const frames = [];
  for (let k = 0; k < total; k++) {
    await new Promise((res, rej) => {
      const ok = () => { video.removeEventListener('error', bad); res(); };
      const bad = () => { video.removeEventListener('seeked', ok); rej(new Error('the browser could not read this video')); };
      video.addEventListener('seeked', ok, { once: true }); video.addEventListener('error', bad, { once: true });
      video.currentTime = Math.min(video.duration - 0.001, k / CLIP_FPS);
    });
    ctx.drawImage(video, sx, sy, side, side, 0, 0, CLIP_SIZE, CLIP_SIZE);
    frames.push(Uint8ClampedArray.from(ctx.getImageData(0, 0, CLIP_SIZE, CLIP_SIZE).data));
    if (onProgress) onProgress(k + 1, total);
  }
  return frames;
}

// ---- GIF ---------------------------------------------------------------------------------------------------------
// A <video> element cannot play a GIF, so the browser's ImageDecoder (WebCodecs) reads it frame by frame. `delaysMs` is each
// GIF frame's display time; the result is the GIF frame index showing at each 12 fps tick, cut at 12 s. A zero or missing
// delay counts as 100 ms, as browsers treat it.
export function gifPlan(delaysMs, maxSeconds = 12) {
  if (!delaysMs.length) throw new Error('no frames');
  const d = delaysMs.map((x) => (x > 0 ? x : 100));
  const total = d.reduce((a, b) => a + b, 0);
  const ticks = Math.max(1, Math.min(Math.floor(maxSeconds * CLIP_FPS), 150, Math.ceil(total * CLIP_FPS / 1000 - 1e-9)));
  const out = [];
  let idx = 0, end = d[0];
  for (let k = 0; k < ticks; k++) {
    const t = k * 1000 / CLIP_FPS;
    while (idx < d.length - 1 && t >= end - 1e-9) { idx++; end += d[idx]; }
    out.push(idx);
  }
  return out;
}

export function isGifFile(f) { return !!f && (f.type === 'image/gif' || /\.gif$/i.test(f.name || '')); }

export const gifSupported = () => typeof ImageDecoder !== 'undefined';

// Same square centre crop and levels as grabFrames, from a GIF File.
export async function grabGifFrames(file, { vpos = 0.5, onProgress } = {}) {
  if (!gifSupported()) throw new Error('this browser cannot read GIFs (use Chrome, Edge or Safari, or convert it to a video)');
  const dec = new ImageDecoder({ data: file.stream(), type: 'image/gif' });
  try {
    await dec.tracks.ready;
    const n = dec.tracks.selectedTrack.frameCount;
    const imgs = [], delays = [];
    for (let i = 0; i < n; i++) {                                   // decode each frame once; the decoder composites it
      const { image } = await dec.decode({ frameIndex: i });
      delays.push(image.duration ? image.duration / 1000 : 0);
      imgs.push(image);
    }
    const plan = gifPlan(delays);
    const cv = document.createElement('canvas'); cv.width = cv.height = CLIP_SIZE;
    const ctx = cv.getContext('2d', { willReadFrequently: true });
    ctx.imageSmoothingEnabled = true; ctx.imageSmoothingQuality = 'high';
    const w = imgs[0].displayWidth, h = imgs[0].displayHeight, side = Math.min(w, h);
    const sx = (w - side) / 2, sy = (h - side) * vpos;
    const frames = [];
    for (let k = 0; k < plan.length; k++) {
      ctx.fillStyle = '#000'; ctx.fillRect(0, 0, CLIP_SIZE, CLIP_SIZE);   // transparent pixels read as black
      ctx.drawImage(imgs[plan[k]], sx, sy, side, side, 0, 0, CLIP_SIZE, CLIP_SIZE);
      frames.push(Uint8ClampedArray.from(ctx.getImageData(0, 0, CLIP_SIZE, CLIP_SIZE).data));
      if (onProgress) onProgress(k + 1, plan.length);
    }
    imgs.forEach((i) => i.close());
    return frames;
  } finally { dec.close(); }
}
