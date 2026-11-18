import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { encodeFont, decodeFont, glyphsFromAlpha, parseBdf, FONT_MIN_H, FONT_MAX_H, MAX_FONT } from '../js/fontfmt.js';
import { ROOT } from './_util.mjs';

const FONTS = path.join(ROOT, 'web/designer/fonts');

function sample() {
  const glyphs = [];
  for (let c = 32; c < 127; c++) glyphs.push({ code: c, adv: 4, w: 0, rows: new Array(8).fill(0) });
  glyphs['A'.charCodeAt(0) - 32] = { code: 65, adv: 6, w: 5, rows: [0b01110, 0b10001, 0b10001, 0b11111, 0b10001, 0b10001, 0b10001, 0] };
  glyphs['a'.charCodeAt(0) - 32] = { code: 97, adv: 5, w: 4, rows: [0, 0, 0b0110, 0b0001, 0b0111, 0b1001, 0b0111, 0] };
  return { h: 8, ascent: 7, glyphs };
}

test('encodeFont writes the device format and decodeFont reads it back', () => {
  const f = sample();
  const bin = encodeFont(f);
  assert.deepEqual([...bin.slice(0, 12)], [0x55, 0x56, 0x46, 0x4e, 1, 8, 7, 32, 95, 0, 0, 0]);
  // the same bytes as sim/fontfix.h: 12 + 95*4 + 8 ('A') + 8 ('a')
  assert.equal(bin.length, 12 + 95 * 4 + 16);
  const back = decodeFont(bin);
  assert.equal(back.h, 8); assert.equal(back.ascent, 7);
  assert.deepEqual(back.glyphs[33], f.glyphs[33]);
  assert.deepEqual(back.glyphs[65], f.glyphs[65]);
  assert.deepEqual(back.glyphs[0], { code: 32, adv: 4, w: 0, rows: new Array(8).fill(0) });
});

test('encodeFont refuses what the device refuses', () => {
  const bad = (mut, re) => { const f = sample(); mut(f); assert.throws(() => encodeFont(f), re); };
  bad((f) => { f.h = FONT_MIN_H - 1; f.glyphs.forEach((g) => { g.rows = g.rows.slice(0, f.h); }); }, /height/);
  bad((f) => { f.h = FONT_MAX_H + 1; }, /height/);
  bad((f) => { f.glyphs[10].w = 33; }, /wider than 32/);
  bad((f) => { f.glyphs.pop(); }, /95/);
  bad((f) => { f.glyphs[3].rows = [1]; }, /rows/);
});

test('decodeFont refuses a truncated or foreign file', () => {
  const bin = encodeFont(sample());
  assert.throws(() => decodeFont(bin.slice(0, 20)), /short|truncated/);
  const v = bin.slice(); v[4] = 2; assert.throws(() => decodeFont(v), /version/);
  const m = bin.slice(); m[0] = 0; assert.throws(() => decodeFont(m), /not a font/);
});

test('glyphsFromAlpha thresholds an alpha map into rows and trims the width', () => {
  // 8x8 alpha with a 3x3 square at x 2..4, y 1..3, one faint pixel that stays off
  const a = new Uint8ClampedArray(64);
  for (let y = 1; y < 4; y++) for (let x = 2; x < 5; x++) a[y * 8 + x] = 255;
  a[6 * 8 + 6] = 90;
  const g = glyphsFromAlpha(a, 8, 8);
  assert.equal(g.w, 5);   // up to the rightmost lit column
  assert.deepEqual(g.rows, [0, 0b00111, 0b00111, 0b00111, 0, 0, 0, 0]);
  assert.equal(glyphsFromAlpha(a, 8, 8, { threshold: 80 }).w, 7);
});

const BDF = `STARTFONT 2.1
FONT -test-tiny
SIZE 8 75 75
FONTBOUNDINGBOX 5 8 0 -1
STARTPROPERTIES 2
FONT_ASCENT 7
FONT_DESCENT 1
ENDPROPERTIES
CHARS 3
STARTCHAR space
ENCODING 32
SWIDTH 500 0
DWIDTH 3 0
BBX 1 1 0 0
BITMAP
00
ENDCHAR
STARTCHAR A
ENCODING 65
SWIDTH 500 0
DWIDTH 6 0
BBX 5 7 0 0
BITMAP
70
88
88
F8
88
88
88
ENDCHAR
STARTCHAR g
ENCODING 103
SWIDTH 500 0
DWIDTH 5 0
BBX 4 5 0 -1
BITMAP
70
90
70
10
60
ENDCHAR
ENDFONT
`;

test('parseBdf places glyphs on the font baseline and fills the missing ones', () => {
  const f = parseBdf(BDF);
  assert.equal(f.h, 8); assert.equal(f.ascent, 7); assert.equal(f.glyphs.length, 95);
  const A = f.glyphs[65 - 32];
  assert.equal(A.adv, 6); assert.equal(A.w, 5);
  assert.deepEqual(A.rows, [0b01110, 0b10001, 0b10001, 0b11111, 0b10001, 0b10001, 0b10001, 0]);
  const g = f.glyphs[103 - 32];                               // descends one row below the baseline
  assert.deepEqual(g.rows, [0, 0, 0, 0b0111, 0b1001, 0b0111, 0b0001, 0b0110]);
  assert.equal(f.glyphs[0].adv, 3);
  assert.equal(f.glyphs[1].w, 0);                             // '!' absent: blank, the bounding box's advance
  assert.equal(f.glyphs[1].adv, 5);
  assert.ok(encodeFont(f).length > 12 + 95 * 4);
});

test('every built-in BDF converts to a device font, and each has its licence recorded', () => {
  const bdfs = fs.readdirSync(FONTS).filter((n) => n.endsWith('.bdf')).sort();
  assert.ok(bdfs.length >= 8, `at least 8 built-in fonts (found ${bdfs.length})`);
  const licences = fs.readFileSync(path.join(FONTS, 'LICENSES.md'), 'utf8');
  for (const n of bdfs) {
    const f = parseBdf(fs.readFileSync(path.join(FONTS, n), 'latin1'));
    assert.ok(f.h >= FONT_MIN_H && f.h <= FONT_MAX_H, `${n}: height ${f.h}`);
    const bin = encodeFont(f);
    assert.ok(bin.length <= MAX_FONT, `${n}: ${bin.length} bytes`);
    assert.equal(decodeFont(bin).glyphs.length, 95);
    const entry = new RegExp('^## ' + n.replace(/[.]/g, '\\.') + '$[\\s\\S]*?Licen[cs]e: \\S[\\s\\S]*?Source: https://', 'm');
    assert.match(licences, entry, `${n}: no LICENSES.md entry with Licence and Source`);
  }
  const listed = [...licences.matchAll(/^## (\S+\.bdf)$/gm)].map((m) => m[1]).sort();
  assert.deepEqual(listed, bdfs, 'LICENSES.md lists exactly the shipped BDF files');
});

test('tools/bdf2fnt.mjs writes every built-in font as .fnt with an index the designer reads', async () => {
  const { spawnSync } = await import('node:child_process');
  const os = await import('node:os');
  const out = fs.mkdtempSync(path.join(os.tmpdir(), 'uvfonts-'));
  const r = spawnSync(process.execPath, [path.join(ROOT, 'tools/bdf2fnt.mjs'), FONTS, out], { encoding: 'utf8' });
  assert.equal(r.status, 0, r.stderr);
  const index = JSON.parse(fs.readFileSync(path.join(out, 'index.json'), 'utf8'));
  const bdfs = fs.readdirSync(FONTS).filter((n) => n.endsWith('.bdf'));
  assert.equal(index.length, bdfs.length);
  for (const e of index) {
    assert.match(e.id, /^[a-z0-9-]+$/);
    assert.ok(e.name && e.licence && e.px >= FONT_MIN_H && e.px <= FONT_MAX_H, JSON.stringify(e));
    const f = decodeFont(fs.readFileSync(path.join(out, e.file)));
    assert.equal(f.h, e.px);
  }
  assert.ok(index.some((e) => e.id === 'spleen-8x16' && e.name === 'Spleen 8x16'));
});
