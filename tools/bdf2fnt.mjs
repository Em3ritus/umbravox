#!/usr/bin/env node
// bdf2fnt.mjs FONTS_DIR OUT_DIR: converts the designer's built-in BDF fonts to the device's .fnt format and writes
// OUT_DIR/index.json, [{ id, name, px, licence, file }], which the designer's font picker reads. Names and licences
// come from FONTS_DIR/LICENSES.md (one "## <file>.bdf" entry per font, with "Font:" and "Licence:" lines).
import fs from 'node:fs';
import path from 'node:path';
import { parseBdf, encodeFont } from '../web/designer/js/fontfmt.js';

const [src, out] = process.argv.slice(2);
if (!src || !out) { console.error('usage: bdf2fnt.mjs FONTS_DIR OUT_DIR'); process.exit(2); }
const meta = new Map();
const md = fs.readFileSync(path.join(src, 'LICENSES.md'), 'utf8');
for (const block of md.split(/^## /m).slice(1)) {
  const file = block.split('\n')[0].trim();
  const font = /^Font: (.+?)(?: \(\d+ px line height\))?$/m.exec(block);
  const lic = /^Licen[cs]e: (.+)$/m.exec(block);
  meta.set(file, { name: font ? font[1] : file, licence: lic ? lic[1] : '' });
}
fs.mkdirSync(out, { recursive: true });
const index = [];
for (const file of fs.readdirSync(src).filter((n) => n.endsWith('.bdf')).sort()) {
  const m = meta.get(file);
  if (!m || !m.licence) { console.error(`${file}: no licence entry in LICENSES.md`); process.exit(1); }
  const f = parseBdf(fs.readFileSync(path.join(src, file), 'latin1'));
  const id = file.replace(/\.bdf$/, '').toLowerCase().replace(/[^a-z0-9]+/g, '-').replace(/r$/, '');
  fs.writeFileSync(path.join(out, id + '.fnt'), encodeFont(f));
  index.push({ id, name: m.name, px: f.h, licence: m.licence, file: id + '.fnt' });
}
fs.writeFileSync(path.join(out, 'index.json'), JSON.stringify(index, null, 1) + '\n');
console.log(`bdf2fnt: ${index.length} fonts`);
