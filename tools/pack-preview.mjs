#!/usr/bin/env node
// Draw a theme pack to preview pictures with the device's own drawing code (the designer's WebAssembly preview).
//
//   node tools/pack-preview.mjs <pack-folder> --out <dir>
//
// Writes <dir>/cyd.png (the CYD's 160x120 canvas at 3x, 480x360) and <dir>/waveshare.png (480x320): the scan screen
// at a fixed frame, so the same pack always gives the same pictures. Reads only theme.json, emblem.bin, radar.vid and
// alert.snd from the folder. Exit 0 on success; exit 1 with one line on stderr otherwise.
// Needs the preview built first: tools/build-designer-wasm.sh. The themes gallery's checks call this.
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';
import { openPackFiles } from '../web/designer/js/model.js';
import { createPreview, BOARDS } from '../web/designer/js/preview.js';
import { encodePng } from './png.mjs';

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const NAMES = ['theme.json', 'emblem.bin', 'radar.vid', 'alert.snd'];
const SHOTS = [{ board: 'cyd', file: 'cyd.png', scale: 3 }, { board: 'waveshare', file: 'waveshare.png', scale: 1 }];
const FRAME = 24;
const fail = (msg) => { process.stderr.write('pack-preview: ' + String(msg).replace(/\s+/g, ' ').trim() + '\n'); process.exit(1); };

const args = process.argv.slice(2);
const o = args.indexOf('--out');
if (args.length !== 3 || o < 0 || o === args.length - 1) fail('usage: node tools/pack-preview.mjs <pack-folder> --out <dir>');
const out = args[o + 1], folder = args.find((a, i) => i !== o && i !== o + 1);

try {
  const st = fs.statSync(folder, { throwIfNoEntry: false });
  if (!st || !st.isDirectory()) fail('not a folder: ' + folder);
  const base = path.basename(path.resolve(folder));
  const files = [];
  for (const n of NAMES) {
    const p = path.join(folder, n), s = fs.lstatSync(p, { throwIfNoEntry: false });
    if (s && s.isFile()) files.push({ name: base + '/' + n, data: new Uint8Array(fs.readFileSync(p)) });
  }
  const r = openPackFiles(files);
  if (!r.ok) fail(r.error);

  const wasm = path.join(ROOT, 'web/designer/wasm/uv_preview.js');
  if (!fs.existsSync(wasm)) fail('the preview is not built: run tools/build-designer-wasm.sh');
  const preview = createPreview(await (await import(pathToFileURL(wasm).href)).default());
  const d = preview.loadDraft(r.theme, r.emblem, r.clip);
  if (!d.ok) fail('the device would skip this pack: ' + d.error);
  const pngs = SHOTS.map((s) => {
    if (!preview.boards().includes(s.board)) fail('this preview build cannot draw the ' + s.board);
    return { file: s.file, data: encodePng(preview.renderDraft('scan', FRAME, { board: s.board }), BOARDS[s.board].w, BOARDS[s.board].h, s.scale) };
  });
  fs.mkdirSync(out, { recursive: true });                     // nothing is written unless every picture rendered
  for (const p of pngs) fs.writeFileSync(path.join(out, p.file), p.data);
} catch (e) { fail(e.message); }
