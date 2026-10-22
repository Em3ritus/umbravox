// Picture -> emblem.bin. Pipeline (as tools/gen_muffin.py): cover-crop to a
// square, BOX (area average) downsample, median-cut quantise, RGB565. Pure and
// deterministic; takes RGBA bytes, so it runs in Node and in a browser.
import limits from './limits.json' with { type: 'json' };
import { expand565 } from './palette.js';

const MAXD = limits.MAX_EMBLEM_DIM, MAXC = limits.MAX_EMBLEM_COLORS;
const rgb565 = (r, g, b) => ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);

// Centre square of the image, area-averaged to t x t. Returns Uint8 RGB (t*t*3).
function squareBox(rgba, w, h, t) {
  const side = Math.min(w, h), ox = Math.floor((w - side) / 2), oy = Math.floor((h - side) / 2);
  const out = new Uint8Array(t * t * 3), scale = side / t;
  const px = (x, y) => { // composite over black: alpha < 128 is transparent
    const i = 4 * ((oy + y) * w + ox + x);
    return rgba[i + 3] < 128 ? null : i;
  };
  for (let ty = 0; ty < t; ty++) {
    const y0 = ty * scale, y1 = (ty + 1) * scale;
    for (let tx = 0; tx < t; tx++) {
      const x0 = tx * scale, x1 = (tx + 1) * scale;
      let r = 0, g = 0, b = 0, wsum = 0;
      for (let y = Math.floor(y0); y < Math.ceil(y1); y++) {
        const wy = Math.min(y + 1, y1) - Math.max(y, y0);
        for (let x = Math.floor(x0); x < Math.ceil(x1); x++) {
          const wgt = wy * (Math.min(x + 1, x1) - Math.max(x, x0));
          const i = px(Math.min(x, side - 1), Math.min(y, side - 1));
          if (i !== null) { r += rgba[i] * wgt; g += rgba[i + 1] * wgt; b += rgba[i + 2] * wgt; }
          wsum += wgt;
        }
      }
      const o = 3 * (ty * t + tx);
      out[o] = Math.round(r / wsum); out[o + 1] = Math.round(g / wsum); out[o + 2] = Math.round(b / wsum);
    }
  }
  return out;
}

// Median cut over the distinct colours (weighted by pixel count) -> <= n boxes.
function medianCut(rgbBytes, n) {
  const counts = new Map();
  for (let i = 0; i < rgbBytes.length; i += 3) {
    const k = (rgbBytes[i] << 16) | (rgbBytes[i + 1] << 8) | rgbBytes[i + 2];
    counts.set(k, (counts.get(k) || 0) + 1);
  }
  const colours = [...counts].map(([k, c]) => ({ c: [k >> 16, (k >> 8) & 255, k & 255], n: c, k }));
  let boxes = [colours];
  while (boxes.length < n) {
    let best = -1, bestN = 0;
    boxes.forEach((bx, i) => {
      const total = bx.reduce((s, e) => s + e.n, 0);
      if (bx.length > 1 && total > bestN) { best = i; bestN = total; }
    });
    if (best < 0) break;
    const bx = boxes[best];
    let ch = 0, span = -1;
    for (let c = 0; c < 3; c++) {
      const vs = bx.map((e) => e.c[c]), s = Math.max(...vs) - Math.min(...vs);
      if (s > span) { span = s; ch = c; }
    }
    bx.sort((a, b) => a.c[ch] - b.c[ch] || a.k - b.k);
    let acc = 0, cut = 1;
    for (let i = 0; i < bx.length - 1; i++) { acc += bx[i].n; cut = i + 1; if (acc * 2 >= bestN) break; }
    boxes.splice(best, 1, bx.slice(0, cut), bx.slice(cut));
  }
  return boxes.map((bx) => {
    const t = bx.reduce((s, e) => s + e.n, 0);
    const m = [0, 1, 2].map((c) => Math.round(bx.reduce((s, e) => s + e.c[c] * e.n, 0) / t));
    return rgb565(m[0], m[1], m[2]);
  });
}

export function imageToEmblem(rgba, w, h) {
  const side = Math.min(w, h), t = Math.min(MAXD, side);
  const small = squareBox(rgba, w, h, t);
  const pal = medianCut(small, MAXC);
  const exp = pal.map(expand565);
  const idx = new Array(t * t), used = new Set();
  for (let p = 0; p < t * t; p++) {
    let bi = 0, bd = Infinity;
    for (let i = 0; i < exp.length; i++) {
      const d = (exp[i][0] - small[3 * p]) ** 2 + (exp[i][1] - small[3 * p + 1]) ** 2 + (exp[i][2] - small[3 * p + 2]) ** 2;
      if (d < bd) { bd = d; bi = i; }
    }
    idx[p] = bi; used.add(bi);
  }
  // Keep only colours that are used, once each (two boxes can round to one RGB565).
  const pal565 = [], remap = new Map(), seen = new Map();
  for (const i of [...used].sort((a, b) => a - b)) {
    if (!seen.has(pal[i])) { seen.set(pal[i], pal565.length); pal565.push(pal[i]); }
    remap.set(i, seen.get(pal[i]));
  }
  // The device needs >= 2 colours: pad with black (or white if black is taken).
  if (pal565.length < 2) pal565.push(pal565[0] === 0 ? 0xFFFF : 0);
  return { w: t, h: t, ncol: pal565.length, pal565, px: idx.map((i) => remap.get(i)) };
}

// The device format: 'U','V','E',1, w, h, ncol, 0, RGB565 LE palette, indices.
export function emblemToBin(e) {
  const out = new Uint8Array(8 + 2 * e.ncol + e.w * e.h);
  out.set([0x55, 0x56, 0x45, 1, e.w, e.h, e.ncol, 0]);
  e.pal565.forEach((v, i) => { out[8 + 2 * i] = v & 255; out[9 + 2 * i] = v >> 8; });
  out.set(e.px, 8 + 2 * e.ncol);
  return out;
}
