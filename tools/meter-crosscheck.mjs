#!/usr/bin/env node
// Do the designer's pixel counts agree with the board's? (plan 5c)
//
//   node tools/meter-crosscheck.mjs PROBE.csv [MORE.csv ...] PACKS_DIR
//
// For every board, pack and mode in the tools/meterprobe.py CSVs, replay the same pack in the wasm (ui/meter.h via
// uv_meter) at the board's measured frame period, and compare the p95 frame's pixels drawn and pushed with the
// board's (the meter judges p95). Steady frames only, on both sides: the board's first frame of every mode run is a
// transition the replay cannot reproduce (MED and LOW rise over the previous card), so it is left out, as
// tools/meterfit.py does; the replay leaves out its scan entry repaint and the alert's rise. More than 15% apart on
// any row exits 1. Pack 'builtin' is theme 0 (no pack); every other pack is PACKS_DIR/<pack>.
// Needs the preview built: tools/build-designer-wasm.sh.
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';
import { openPackFiles } from '../web/designer/js/model.js';
import { createPreview } from '../web/designer/js/preview.js';
const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const TOL = 0.15, FRAMES = 30, SKIP_SCAN = 2;
const SIZE = { waveshare: [480, 320], 'cyd-engine': [320, 240] };
const MODE = { scan: -1, low: 0, med: 1, high: 2 };

const args = process.argv.slice(2);
if (args.length < 2) { console.error('usage: meter-crosscheck.mjs PROBE.csv [...] PACKS_DIR'); process.exit(2); }
const packsDir = args.pop();

const rows = [];
for (const f of args) {
  const [head, ...lines] = fs.readFileSync(f, 'utf8').trim().split('\n');
  const cols = head.split(',');
  for (const l of lines) { const v = l.split(','); const r = {}; cols.forEach((c, i) => { r[c] = v[i]; }); rows.push(r); }
}

const wasm = path.join(ROOT, 'web/designer/wasm/uv_preview.js');
if (!fs.existsSync(wasm)) { console.error('the preview is not built: run tools/build-designer-wasm.sh'); process.exit(2); }
const M = await (await import(pathToFileURL(wasm).href)).default();
const preview = createPreview(M);

function loadPack(name) {
  if (name === 'builtin') return 0;
  const dir = path.join(packsDir, name), base = path.basename(dir);
  const files = [];
  for (const n of ['theme.json', 'emblem.bin', 'radar.vid']) {
    const p = path.join(dir, n);
    if (fs.existsSync(p)) files.push({ name: base + '/' + n, data: new Uint8Array(fs.readFileSync(p)) });
  }
  const r = openPackFiles(files);
  if (!r.ok) throw new Error(`${name}: ${r.error}`);
  const media = { fonts: {}, arts: {} };
  for (const n of fs.readdirSync(dir)) {
    const m = /^([fa])(\d+)\.(fnt|art)$/.exec(n);
    if (m) (m[1] === 'f' ? media.fonts : media.arts)[m[1] + m[2]] = new Uint8Array(fs.readFileSync(path.join(dir, n)));
  }
  const lp = path.join(dir, 'layout.json');
  const d = preview.loadDraft(r.theme, r.emblem, r.clip, fs.existsSync(lp) ? fs.readFileSync(lp, 'utf8') : null, media);
  if (!d.ok) throw new Error(`${name}: the device would skip this pack: ${d.error}`);
  return d.id;
}

function replay(id, board, mode, frameMs) {
  const [w, h] = SIZE[board];
  const p = M._malloc(FRAMES * 8);
  try {
    const n = M._uv_meter(id, w, h, MODE[mode], FRAMES, frameMs, p);
    const v = new Uint32Array(M.HEAPU8.buffer, p, FRAMES * 2);
    const out = [];
    for (let i = mode === 'scan' ? SKIP_SCAN : 1; i < n; i++) out.push({ drawn: v[2 * i], pushed: v[2 * i + 1] });
    return out;
  } finally { M._free(p); }
}

const p95 = (list, k) => {
  const s = list.map((r) => Number(r[k])).sort((x, y) => x - y);
  return s.length ? s[Math.max(0, Math.ceil(0.95 * s.length) - 1)] : 0;
};
// The board's rows without the first frame of every run of the same board, pack and mode (file order).
const steady = rows.filter((r, i) => i > 0 && rows[i - 1].board === r.board && rows[i - 1].pack === r.pack && rows[i - 1].mode === r.mode);
const median = (a) => { const s = [...a].sort((x, y) => x - y); return s.length ? s[Math.floor(s.length / 2)] : 0; };
let bad = 0;
const ids = new Map();
for (const board of [...new Set(rows.map((r) => r.board))]) {
  if (!SIZE[board]) { console.error(`unknown board ${board}`); process.exit(2); }
  const br = steady.filter((r) => r.board === board);
  const frameMs = Math.max(1, Math.round(median(br.filter((r) => r.mode === 'scan' && Number(r.gap) > 0).map((r) => Number(r.gap)))) || 20);
  for (const pack of [...new Set(br.map((r) => r.pack))]) {
    if (!ids.has(pack)) ids.set(pack, loadPack(pack));
    for (const mode of Object.keys(MODE)) {
      const dev = br.filter((r) => r.pack === pack && r.mode === mode);
      if (!dev.length) continue;
      const sim = replay(ids.get(pack), board, mode, frameMs);
      for (const k of ['drawn', 'pushed']) {
        const a = p95(dev, k), b = p95(sim, k);
        const off = Math.abs(a - b) / Math.max(a, 1);
        const ok = off <= TOL;
        if (!ok) bad++;
        console.log(`${ok ? 'ok  ' : 'FAIL'} ${board} ${pack} ${mode} ${k} p95: board ${a}, wasm ${b} (${(off * 100).toFixed(1)}%)`);
      }
    }
  }
}
console.log(bad ? `${bad} rows more than ${TOL * 100}% apart` : 'the wasm counts agree with the board');
process.exit(bad ? 1 : 0);
