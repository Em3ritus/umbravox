#!/usr/bin/env node
// Set the speed meter's scan limits from the built-in theme (plan 5c), after tools/meterfit.py:
//
//   node tools/meter-limits.mjs web/designer/js/meter-fit.json [--check]
//
// The scan limit of each board is what the meter PREDICTS for the built-in theme (theme 0, no pack: the wasm replay
// at the fit's frame period, through the board's scan model) at p95, + 15% (js/meter.js builtinScanLimit). Using the
// prediction rather than the board's measured p95 cancels the fit's bias, so "the built-in theme + 15%" means the
// same thing in the designer as on the board. --check writes nothing and exits 1 if a limit in the file is stale.
// Needs the preview built: tools/build-designer-wasm.sh.
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';
import { loadFit, builtinScanLimit, METER_FRAMES } from '../web/designer/js/meter.js';
const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const SIZE = { waveshare: [480, 320], 'cyd-engine': [320, 240] };

const args = process.argv.slice(2);
const check = args.includes('--check');
const file = args.find((a) => a !== '--check');
if (!file) { console.error('usage: meter-limits.mjs FIT.json [--check]'); process.exit(2); }
const j = JSON.parse(fs.readFileSync(file, 'utf8'));
const f = loadFit(j);
if (!f.ok) { console.error(f.why); process.exit(2); }
const wasm = path.join(ROOT, 'web/designer/wasm/uv_preview.js');
if (!fs.existsSync(wasm)) { console.error('the preview is not built: run tools/build-designer-wasm.sh'); process.exit(2); }
const M = await (await import(pathToFileURL(wasm).href)).default();

function builtinScan(board, frameMs) {
  const [w, h] = SIZE[board];
  const p = M._malloc(METER_FRAMES * 8);
  try {
    const n = M._uv_meter(0, w, h, -1, METER_FRAMES, frameMs, p);
    const v = new Uint32Array(M.HEAPU8.buffer, p, METER_FRAMES * 2);
    return Array.from({ length: n }, (_, i) => ({ drawn: v[2 * i], pushed: v[2 * i + 1] }));
  } finally { M._free(p); }
}

let stale = 0;
for (const [b, fb] of Object.entries(j.boards)) {
  if (!SIZE[b]) { console.error(`unknown board ${b}`); process.exit(2); }
  const lim = builtinScanLimit(j, b, builtinScan(b, fb.frameMs));
  const same = fb.limits.scanMs === lim;
  if (!same) stale++;
  console.log(`${b}: scan limit ${lim} ms (built-in predicted p95 + 15%)${same ? '' : `, file has ${fb.limits.scanMs}`}`);
  fb.limits.scanMs = lim;
}
if (check) process.exit(stale ? 1 : 0);
fs.writeFileSync(file, JSON.stringify(j, null, 2) + '\n');
