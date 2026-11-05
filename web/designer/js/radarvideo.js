// The radar clip: what the designer turns a video into so the Waveshare preview can play it in the radar. The same
// format the board plays (ui/scan_render.h RadarVideo, made by tools/video2radar.py): 102x102 px, 12 fps, 16 grey
// levels, two pixels per byte with the high nibble first. This module is pure (no DOM); the browser step that fills
// frames from a <video> element is `grabFrames`, kept at the bottom.
export const CLIP_SIZE = 102;
export const CLIP_FPS = 12;
export const MAX_BYTES = 786432;                                   // the wasm's clip buffer (bridge.cpp)
export const FRAME_BYTES = Math.ceil(CLIP_SIZE * CLIP_SIZE / 2);   // 5202
export const MAX_FRAMES = Math.floor(MAX_BYTES / FRAME_BYTES);     // 151; capped at 150 = 12.5 s

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
export function buildClip(frames) {
  if (!frames.length) throw new Error('no frames');
  if (frames.length > Math.min(150, MAX_FRAMES)) throw new Error(`too many frames (${frames.length}; at most 150)`);
  const data = new Uint8Array(frames.length * FRAME_BYTES);
  frames.forEach((f, k) => {
    if (f.length !== CLIP_SIZE * CLIP_SIZE) throw new Error(`frame ${k} is the wrong size (${f.length} px)`);
    data.set(packLevels(f), k * FRAME_BYTES);
  });
  return { w: CLIP_SIZE, h: CLIP_SIZE, frames: frames.length, fps: CLIP_FPS, data };
}

// radar.vid: the file a pack carries (ui/themepack.h parseClip): 'U','V','R','V', version 1, w, h, fps, frames (u16 LE),
// two reserved zero bytes, then the frames.
export const CLIP_HEADER = 12;
export const MAX_CLIP_FILE = CLIP_HEADER + 150 * FRAME_BYTES;      // 780312, limits.json MAX_CLIP

export function clipFile(c) {
  const why = validateClip(c);
  if (why) throw new Error(why);
  const out = new Uint8Array(CLIP_HEADER + c.data.length);
  out.set([0x55, 0x56, 0x52, 0x56, 1, c.w, c.h, c.fps, c.frames & 255, c.frames >> 8, 0, 0]);
  out.set(c.data, CLIP_HEADER);
  return out;
}

// A radar.vid back into a clip, with the same refusals as the device's parser. Throws Error(reason).
export function readClipFile(b) {
  if (!b || b.length < CLIP_HEADER) throw new Error('radar.vid is too short');
  if (b[0] !== 0x55 || b[1] !== 0x56 || b[2] !== 0x52 || b[3] !== 0x56) throw new Error('radar.vid is not a UVRV file');
  if (b[4] !== 1) throw new Error('radar.vid version is not 1');
  if (b[5] !== CLIP_SIZE || b[6] !== CLIP_SIZE) throw new Error(`radar.vid must be ${CLIP_SIZE}x${CLIP_SIZE}`);
  if (b[7] < 1 || b[7] > 30) throw new Error('radar.vid fps must be 1 to 30');
  const frames = b[8] | (b[9] << 8);
  if (frames < 1 || frames > 150) throw new Error('radar.vid frames must be 1 to 150');
  if (b[10] || b[11]) throw new Error('radar.vid reserved bytes are not zero');
  if (b.length !== CLIP_HEADER + frames * FRAME_BYTES) throw new Error('radar.vid length does not match its frames');
  return { w: b[5], h: b[6], frames, fps: b[7], data: Uint8Array.from(b.subarray(CLIP_HEADER)) };
}

// null when the clip is playable, else why not.
export function validateClip(c) {
  if (!c || c.w !== CLIP_SIZE || c.h !== CLIP_SIZE) return `the clip must be ${CLIP_SIZE}x${CLIP_SIZE}`;
  if (!(c.frames >= 1)) return 'the clip has no frames';
  if (!(c.fps >= 1 && c.fps <= 30)) return 'the clip fps must be 1 to 30';
  if (c.data.length !== c.frames * FRAME_BYTES) return `the clip has ${c.data.length} bytes, expected ${c.frames * FRAME_BYTES}`;
  if (c.data.length > MAX_BYTES) return 'the clip is over the size limit';
  return null;
}

// Browser only: seek a <video> through its length at CLIP_FPS, crop a square (full width, `vpos` 0..1 from the top
// to the bottom of the spare height) and scale it to CLIP_SIZE. Resolves to an array of level frames.
// onProgress(done, total) is called after each frame. Never touched by the Node tests.
export async function grabFrames(video, { vpos = 0.5, black = 24, white = 235, maxSeconds = 12, onProgress } = {}) {
  const cv = document.createElement('canvas'); cv.width = cv.height = CLIP_SIZE;
  const ctx = cv.getContext('2d', { willReadFrequently: true });
  ctx.imageSmoothingEnabled = true; ctx.imageSmoothingQuality = 'high';
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
    frames.push(levelsFromGray(lumaFromRgba(ctx.getImageData(0, 0, CLIP_SIZE, CLIP_SIZE).data), black, white));
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
export async function grabGifFrames(file, { vpos = 0.5, black = 24, white = 235, onProgress } = {}) {
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
      frames.push(levelsFromGray(lumaFromRgba(ctx.getImageData(0, 0, CLIP_SIZE, CLIP_SIZE).data), black, white));
      if (onProgress) onProgress(k + 1, plan.length);
    }
    imgs.forEach((i) => i.close());
    return frames;
  } finally { dec.close(); }
}
