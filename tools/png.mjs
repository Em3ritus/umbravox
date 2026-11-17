// Minimal PNG writer for Node: RGBA in, 8-bit RGB out (alpha dropped), nearest-neighbour integer scale.
// No dependency: zlib comes with Node. Used by tools/pack-preview.mjs.
import zlib from 'node:zlib';
import { crc32 } from '../web/designer/js/zip.js';

function chunk(type, data) {
  const out = new Uint8Array(12 + data.length), dv = new DataView(out.buffer);
  dv.setUint32(0, data.length);
  for (let i = 0; i < 4; i++) out[4 + i] = type.charCodeAt(i);
  out.set(data, 8);
  dv.setUint32(8 + data.length, crc32(out.subarray(4, 8 + data.length)));
  return out;
}

export function encodePng(rgba, w, h, scale = 1) {
  if (!Number.isInteger(scale) || scale < 1 || scale > 8) throw new Error('encodePng: scale must be 1 to 8');
  if (!Number.isInteger(w) || !Number.isInteger(h) || w < 1 || h < 1 || rgba.length !== w * h * 4) throw new Error('encodePng: buffer size is not w*h*4');
  const W = w * scale, H = h * scale, stride = 1 + 3 * W;
  const raw = new Uint8Array(H * stride);
  for (let y = 0; y < H; y++) {
    const sy = (y / scale) | 0;
    let o = y * stride + 1;                                    // byte 0 of each row is filter type 0 (none)
    for (let x = 0; x < W; x++) {
      const s = 4 * (sy * w + ((x / scale) | 0));
      raw[o++] = rgba[s]; raw[o++] = rgba[s + 1]; raw[o++] = rgba[s + 2];
    }
  }
  const ihdr = new Uint8Array(13), dv = new DataView(ihdr.buffer);
  dv.setUint32(0, W); dv.setUint32(4, H); ihdr[8] = 8; ihdr[9] = 2;   // 8 bits per channel, colour type 2 (RGB)
  const parts = [new Uint8Array([137, 80, 78, 71, 13, 10, 26, 10]), chunk('IHDR', ihdr), chunk('IDAT', zlib.deflateSync(raw, { level: 9 })), chunk('IEND', new Uint8Array(0))];
  const out = new Uint8Array(parts.reduce((n, p) => n + p.length, 0));
  let o = 0;
  for (const p of parts) { out.set(p, o); o += p.length; }
  return out;
}
