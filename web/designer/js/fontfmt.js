// fontfmt.js: the device's .fnt pixel font (ui/fontpack.h), and the two ways the designer makes one: from a BDF
// bitmap font (the built-in set, converted at build time by tools/bdf2fnt.mjs) and from an uploaded TTF/OTF,
// rasterised in the browser at the pixel size the layout uses.
//
// A font here is { h, ascent, glyphs }: glyphs holds the 95 chars 32..126 in order, each { code, adv, w, rows },
// where rows has h integers and bit (w - 1 - x) of a row is pixel x (so a row reads left to right as binary).

export const FONT_MIN_H = 6, FONT_MAX_H = 32, FONT_MAX_GLYPH_W = 32, MAX_FONT = 12800;
const FIRST = 32, COUNT = 95, HEADER = 12, ENTRY = 4;

const stride = (w) => (w + 7) >> 3;

export function encodeFont(f) {
  const { h, ascent, glyphs } = f;
  if (!Number.isInteger(h) || h < FONT_MIN_H || h > FONT_MAX_H) throw new Error(`font height ${h} is outside ${FONT_MIN_H}..${FONT_MAX_H}`);
  if (!Array.isArray(glyphs) || glyphs.length !== COUNT) throw new Error('a font holds exactly the 95 characters 32..126');
  const a = Math.max(0, Math.min(h, ascent | 0));
  const bits = [];
  const table = new Uint8Array(COUNT * ENTRY);
  glyphs.forEach((g, i) => {
    if (g.w > FONT_MAX_GLYPH_W) throw new Error(`glyph ${String.fromCharCode(FIRST + i)} is wider than 32 pixels`);
    if (!Array.isArray(g.rows) || g.rows.length !== h) throw new Error(`glyph ${String.fromCharCode(FIRST + i)}: rows must be ${h} long`);
    const off = bits.length;
    table.set([Math.max(0, Math.min(255, g.adv | 0)), g.w, off & 0xff, off >> 8], i * ENTRY);
    const s = stride(g.w);
    for (const r of g.rows) {
      const v = (r >>> 0) * 2 ** (s * 8 - g.w);              // left-aligned, MSB first (no 32-bit shift overflow)
      for (let k = s - 1; k >= 0; k--) bits.push(Math.floor(v / 2 ** (k * 8)) & 0xff);
    }
  });
  const out = new Uint8Array(HEADER + table.length + bits.length);
  out.set([0x55, 0x56, 0x46, 0x4e, 1, h, a, FIRST, COUNT, 0, 0, 0]);
  out.set(table, HEADER);
  out.set(bits, HEADER + table.length);
  if (out.length > MAX_FONT) throw new Error(`font is ${out.length} bytes, over the device's ${MAX_FONT}`);
  return out;
}

export function decodeFont(b) {
  if (!b || b.length < HEADER) throw new Error('font file too short');
  if (b[0] !== 0x55 || b[1] !== 0x56 || b[2] !== 0x46 || b[3] !== 0x4e) throw new Error('not a font file');
  if (b[4] !== 1) throw new Error('unsupported font version');
  const h = b[5], ascent = b[6];
  if (b[7] !== FIRST || b[8] !== COUNT) throw new Error('font must hold chars 32..126');
  const base = HEADER + COUNT * ENTRY;
  if (b.length < base) throw new Error('font glyph table truncated');
  const glyphs = [];
  for (let i = 0; i < COUNT; i++) {
    const e = HEADER + i * ENTRY, adv = b[e], w = b[e + 1], off = b[e + 2] | (b[e + 3] << 8), s = stride(w);
    if (base + off + h * s > b.length) throw new Error('font glyph runs past the end (truncated)');
    const rows = [];
    for (let y = 0; y < h; y++) {
      let v = 0;
      for (let k = 0; k < s; k++) v = v * 256 + b[base + off + y * s + k];
      rows.push(Math.floor(v / 2 ** (s * 8 - w)));
    }
    glyphs.push({ code: FIRST + i, adv, w, rows });
  }
  return { h, ascent, glyphs };
}

// One glyph from an alpha map (w x h, 0..255): pixels at or over the threshold are on; the width ends at the
// rightmost lit column (at most 32).
export function glyphsFromAlpha(alpha, w, h, { threshold = 128 } = {}) {
  let right = 0;
  for (let y = 0; y < h; y++) for (let x = 0; x < w; x++) if (alpha[y * w + x] >= threshold && x + 1 > right) right = x + 1;
  const gw = Math.min(right, FONT_MAX_GLYPH_W);
  const rows = [];
  for (let y = 0; y < h; y++) {
    let v = 0;
    for (let x = 0; x < gw; x++) v = v * 2 + (alpha[y * w + x] >= threshold ? 1 : 0);
    rows.push(v);
  }
  return { w: gw, rows };
}

// A BDF bitmap font -> the font shape above. Glyphs sit on the font's baseline (FONT_ASCENT); a char the font lacks
// is blank with the bounding box's advance.
export function parseBdf(text) {
  const lines = text.split(/\r?\n/);
  let bbw = 0, bbh = 0, bby = 0, ascent = null, descent = null;
  const found = new Map();
  for (let i = 0; i < lines.length; i++) {
    const t = lines[i].trim().split(/\s+/);
    if (t[0] === 'FONTBOUNDINGBOX') { bbw = +t[1]; bbh = +t[2]; bby = +t[4]; }
    else if (t[0] === 'FONT_ASCENT') ascent = +t[1];
    else if (t[0] === 'FONT_DESCENT') descent = +t[1];
    else if (t[0] === 'STARTCHAR') {
      let enc = -1, adv = bbw, bw = 0, bh = 0, bx = 0, byo = 0;
      const hex = [];
      for (i++; i < lines.length && lines[i].trim() !== 'ENDCHAR'; i++) {
        const u = lines[i].trim().split(/\s+/);
        if (u[0] === 'ENCODING') enc = +u[1];
        else if (u[0] === 'DWIDTH') adv = +u[1];
        else if (u[0] === 'BBX') { bw = +u[1]; bh = +u[2]; bx = +u[3]; byo = +u[4]; }
        else if (u[0] === 'BITMAP') {
          for (i++; i < lines.length && lines[i].trim() !== 'ENDCHAR'; i++) hex.push(lines[i].trim());
          i--;
        }
      }
      if (enc >= FIRST && enc < FIRST + COUNT) found.set(enc, { adv, bw, bh, bx, byo, hex });
    }
  }
  if (ascent === null) ascent = bbh + bby;
  if (descent === null) descent = -bby;
  const h = ascent + descent;
  const glyphs = [];
  for (let c = FIRST; c < FIRST + COUNT; c++) {
    const g = found.get(c);
    const rows = new Array(h).fill(0);
    if (!g) { glyphs.push({ code: c, adv: bbw, w: 0, rows }); continue; }
    const shift = Math.max(0, g.bx), w = Math.min(FONT_MAX_GLYPH_W, g.bw + shift);
    const top = ascent - (g.bh + g.byo);
    g.hex.forEach((hx, r) => {
      const y = top + r;
      if (y < 0 || y >= h || !hx) return;
      const bitsIn = hx.length * 4, v = parseInt(hx, 16);
      let row = Math.floor(v / 2 ** (bitsIn - g.bw));         // the glyph's bw leftmost bits
      row = row * 2 ** (w - g.bw - shift) * 1;                  // place it after the x offset
      rows[y] = Math.floor(row) % 2 ** w;
    });
    glyphs.push({ code: c, adv: g.adv, w: g.bw ? w : 0, rows: g.bw ? rows : new Array(h).fill(0) });
  }
  return { h, ascent, glyphs };
}

// An uploaded TTF/OTF (a loaded FontFace) rasterised at px pixels in the browser. Pixel fonts drawn at their
// design size come out exact; outline fonts are thresholded, so small sizes look like a pixel font.
export async function rasteriseFont(fontFace, px) {
  if (typeof OffscreenCanvas === 'undefined') throw new Error('this browser cannot draw fonts off screen');
  await fontFace.load();
  if (typeof document !== 'undefined' && document.fonts) document.fonts.add(fontFace);
  const probe = new OffscreenCanvas(8, 8).getContext('2d');
  probe.font = `${px}px "${fontFace.family}"`;
  const m = probe.measureText('Hg');
  const ascent = Math.ceil(m.fontBoundingBoxAscent ?? m.actualBoundingBoxAscent ?? px * 0.8);
  const descent = Math.ceil(m.fontBoundingBoxDescent ?? m.actualBoundingBoxDescent ?? px * 0.2);
  const h = Math.max(FONT_MIN_H, Math.min(FONT_MAX_H, ascent + descent));
  const W = FONT_MAX_GLYPH_W + 8;
  const cv = new OffscreenCanvas(W, h), ctx = cv.getContext('2d', { willReadFrequently: true });
  const glyphs = [];
  for (let c = FIRST; c < FIRST + COUNT; c++) {
    const ch = String.fromCharCode(c);
    ctx.clearRect(0, 0, W, h);
    ctx.font = `${px}px "${fontFace.family}"`;
    ctx.textBaseline = 'alphabetic';
    ctx.fillStyle = '#fff';
    ctx.fillText(ch, 0, Math.min(ascent, h));
    const d = ctx.getImageData(0, 0, W, h).data;
    const alpha = new Uint8ClampedArray(W * h);
    for (let i = 0; i < W * h; i++) alpha[i] = d[i * 4 + 3];
    const g = glyphsFromAlpha(alpha, W, h);
    const adv = Math.max(1, Math.min(255, Math.round(ctx.measureText(ch).width)));
    glyphs.push({ code: c, adv: Math.max(adv, g.w + (ch === ' ' ? 0 : 1)), w: g.w, rows: g.rows });
  }
  return { h, ascent: Math.min(ascent, h), glyphs };
}
