// A pack that uses every alert feature of layout.json (plan 5a), for the hardware check with the console TEST alert:
//
//   node tools/alert-pack.mjs OUT_DIR        writes OUT_DIR/alert-fx/ (then: tools/sdput.py PORT put OUT_DIR/alert-fx)
//
// Waveshare: a LOW card moved off the radar (over the WIFI/5G tiles), the built-in MED card, a HIGH card smaller than
// the screen with a pack font and an animated picture in place of the rite, and four effects (the cap). CYD engine
// build: a moved LOW card, a whole-screen HIGH card with a picture, two effects (the cap). Select the theme, then send
// TEST on the console: it walks HIGH, MED, LOW and three more test alerts, 5 s each.
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
const repo = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const out = process.argv[2];
if (!out) { console.error('usage: alert-pack.mjs OUT_DIR'); process.exit(2); }
const { parseBdf, encodeFont } = await import(path.join(repo, 'web/designer/js/fontfmt.js'));
const { imageToArt, artToBin } = await import(path.join(repo, 'web/designer/js/artfmt.js'));
const dir = path.join(out, 'alert-fx');
fs.rmSync(dir, { recursive: true, force: true }); fs.mkdirSync(dir, { recursive: true });

const theme = JSON.parse(fs.readFileSync(path.join(repo, 'packs/obsidian/theme.json'), 'utf8'));
theme.name = 'ALERT FX'; theme.header = 'ALERT FX'; theme.author = 'probe';
fs.writeFileSync(path.join(dir, 'theme.json'), JSON.stringify(theme, null, 2) + '\n');
fs.writeFileSync(path.join(dir, 'f0.fnt'), encodeFont(parseBdf(fs.readFileSync(path.join(repo, 'web/designer/fonts/spleen-8x16.bdf'), 'utf8'))));

// An animated ring of 16 colours, 8 frames, so a playing picture is obvious at a glance.
function frames(w, h, n) {
  return Array.from({ length: n }, (_, k) => {
    const p = new Uint8ClampedArray(w * h * 4);
    for (let y = 0; y < h; y++) for (let x = 0; x < w; x++) {
      const dx = x - w / 2, dy = y - h / 2, v = ((Math.floor(Math.hypot(dx, dy) / 4) + k) & 15), o = (y * w + x) * 4;
      p[o] = v * 16; p[o + 1] = 40 + v * 8; p[o + 2] = 255 - v * 16; p[o + 3] = 255;
    }
    return p;
  });
}
const art = (name, w, h) => fs.writeFileSync(path.join(dir, name), artToBin(imageToArt(frames(w, h, 8), w, h, { w, h, delays: Array(8).fill(100) })));
art('a0.art', 120, 90);   // Waveshare HIGH card picture: 10,800 px (cap 25,600)
art('a1.art', 80, 60);    // CYD picture: 4,800 px (cap 10,000)

const layout = { format: 1,
  waveshare: {
    'alert.card.low': { rect: [0, 48, 300, 150], font: 'f0' },
    'alert.card.high': { rect: [40, 40, 400, 240], font: 'f0', clip: 'a0' },
    'count.wifi': { rect: [16, 52, 100, 68], effect: 'flash' },
    'count.ble': { rect: [16, 196, 100, 68], effect: 'pulse', speed: 'slow' },
    stop: { rect: [384, 276, 96, 44], effect: 'shake', speed: 'fast' },
    'nav.scan': { rect: [0, 276, 96, 44], effect: 'flash', speed: 'fast' },
  },
  cyd: {
    'alert.card.low': { rect: [0, 44, 200, 110] },
    'alert.card.high': { rect: [0, 0, 320, 240], clip: 'a1' },
    'count.ble': { rect: [8, 142, 88, 46], effect: 'pulse' },
    stop: { rect: [260, 196, 60, 44], effect: 'shake' },
  },
};
fs.writeFileSync(path.join(dir, 'layout.json'), JSON.stringify(layout) + '\n');
console.log('wrote', dir);
