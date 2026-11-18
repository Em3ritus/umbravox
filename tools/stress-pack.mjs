// The worst-case pack the format allows, for the hardware gate and the draw-time benchmark:
//
//   node tools/stress-pack.mjs OUT_DIR        writes OUT_DIR/stress-p4/ (then: tools/sdput.py PORT put OUT_DIR/stress-p4)
//
// The Waveshare look references 8 fonts and 16 pictures (98% of MAX_ART_TOTAL): a full-screen background, seven
// overlapping 240x240 decor pieces and one animated 160x160 decor (the animation area limit). The CYD release build
// ignores layout.json, so this pack only exercises the engine builds. On 2026-11-18 it loaded in 2.6 s on the
// Waveshare and drew in ~101 ms a frame (29 ms with the built-in theme).
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
const repo = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const out = process.argv[2];
if (!out) { console.error('usage: stress-pack.mjs OUT_DIR'); process.exit(2); }
const { parseBdf, encodeFont, MAX_FONT } = await import(path.join(repo, 'web/designer/js/fontfmt.js'));
const { imageToArt, artToBin, artBytes, MAX_ART } = await import(path.join(repo, 'web/designer/js/artfmt.js'));
const MAX_ART_TOTAL = 393216;
const dir = path.join(out, 'stress-p4');
fs.rmSync(dir, { recursive: true, force: true }); fs.mkdirSync(dir, { recursive: true });

const theme = JSON.parse(fs.readFileSync(path.join(repo, 'packs/obsidian/theme.json'), 'utf8'));
theme.name = 'STRESS P4'; theme.header = 'STRESS P4'; theme.author = 'probe';
fs.writeFileSync(path.join(dir, 'theme.json'), JSON.stringify(theme, null, 2) + '\n');

const bdfs = ['cozette.bdf', 'gohufont-11.bdf', 'gohufont-14.bdf', 'scientifica-11.bdf', 'spleen-5x8.bdf', 'spleen-6x12.bdf', 'spleen-8x16.bdf', 'spleen-12x24.bdf'];
bdfs.forEach((b, i) => {
  const bin = encodeFont(parseBdf(fs.readFileSync(path.join(repo, 'web/designer/fonts', b), 'utf8')));
  if (bin.length > MAX_FONT) throw new Error(`${b}: ${bin.length} > ${MAX_FONT}`);
  fs.writeFileSync(path.join(dir, `f${i}.fnt`), bin);
  console.log(`f${i}.fnt ${b} ${bin.length} B`);
});

// 16-colour synthetic pictures; frame k shifts the pattern so every frame differs.
function rgba(w, h, seed, k = 0) {
  const p = new Uint8ClampedArray(w * h * 4);
  for (let y = 0; y < h; y++) for (let x = 0; x < w; x++) {
    const v = (((x + k * 7) >> 3) ^ ((y + seed * 5) >> 3) ^ seed) & 15, o = (y * w + x) * 4;
    p[o] = v * 16; p[o + 1] = (15 - v) * 12; p[o + 2] = (v * 37 + seed * 29) & 255; p[o + 3] = 255;
  }
  return p;
}
const arts = [];
const art = (w, h, frames = 1, delay = 50) => {
  const i = arts.length, fr = Array.from({ length: frames }, (_, k) => rgba(w, h, i + 1, k));
  const bin = artToBin(imageToArt(fr, w, h, { w, h, delays: Array(frames).fill(delay) }));
  if (bin.length > MAX_ART) throw new Error(`a${i}: ${bin.length} > ${MAX_ART}`);
  fs.writeFileSync(path.join(dir, `a${i}.art`), bin); arts.push(bin.length);
  return `a${i}`;
};
const decor = {};
decor['decor.0'] = { rect: [300, 60, 160, 160], art: art(160, 160, 8, 50) };          // animated, 25,600 px
const spots = [[0, 44], [240, 44], [0, 80], [240, 80], [120, 44], [120, 80], [60, 60]];
for (let k = 1; k <= 7; k++) decor[`decor.${k}`] = { rect: [...spots[k - 1], 240, 240], art: art(240, 240) };
const bg = art(480, 320), panel = art(48, 48);
const icons = Array.from({ length: 6 }, () => art(32, 32));
const total = arts.reduce((a, b) => a + b, 0);
console.log(`16 pictures, ${total} B of ${MAX_ART_TOTAL} (${(100 * total / MAX_ART_TOTAL).toFixed(1)}%)`);
if (arts.length !== 16 || total > MAX_ART_TOTAL) throw new Error('art budget');

const layout = { format: 1, waveshare: {
  menu: { rect: [0, 0, 44, 44], font: 'f0' },
  wordmark: { rect: [44, 0, 164, 44], font: 'f1' },
  'count.wifi': { rect: [16, 52, 100, 68], font: 'f2' },
  'count.5g': { rect: [16, 124, 100, 68], font: 'f3' },
  'count.ble': { rect: [16, 196, 100, 68], font: 'f4' },
  style: { title: 'f5', body: 'f6', numbers: 'f7', bg, panel, panelSlice: [4, 4, 4, 4], icons },
  ...decor,
} };
fs.writeFileSync(path.join(dir, 'layout.json'), JSON.stringify(layout) + '\n');
console.log('wrote', dir);
