import test from 'node:test';
import assert from 'node:assert/strict';
import { runChecks } from '../js/checks.js';
import { fromAccent, ROLES } from '../js/palette.js';

const base = () => ({ name: 'TEST', author: 'me', header: 'TEST', dial: 'compact', centrepiece: 'heptagram',
  sweeps: ['radar', 'pulse'], sweep: 'radar', alert: 'ward', colors: { ...fromAccent('#A84DFF') } });
const emb = (w, h, ncol) => ({ w, h, ncol, pal565: Array(ncol).fill(0x1234), px: new Array(w * h).fill(0) });
const find = (rs, level, re) => rs.filter((r) => r.level === level && re.test(r.text));
const errors = (rs) => rs.filter((r) => r.level === 'error');

test('a derived palette passes cleanly', () => {
  assert.deepEqual(errors(runChecks(base(), null)), []);
  assert.equal(find(runChecks(base(), null), 'warn', /./).length, 0);
});

test('an all-black theme does not crash or produce NaN', () => {
  const t = base();
  for (const r of ROLES) t.colors[r] = '#000000';
  const rs = runChecks(t, null);
  assert.ok(rs.length > 0);
  for (const r of rs) assert.ok(!/NaN|Infinity/.test(r.text), r.text);
  assert.ok(find(rs, 'error', /text.*panel|panel.*text/i).length >= 1);
});

test('text == panel is an error with the ratio', () => {
  const t = base(); t.colors.text = t.colors.panel;
  assert.equal(find(runChecks(t, null), 'error', /1\.0:1/).length >= 1, true);
});

test('text/panel warns below 4.5 and errors below 3', () => {
  const t = base(); t.colors.panel = '#000000'; t.colors.text = '#6A6A6A'; // about 3.7:1
  const rs = runChecks(t, null);
  assert.equal(find(rs, 'warn', /text/i).length >= 1, true);
  assert.equal(find(rs, 'error', /text.*panel/i).length, 0);
});

test('low dim contrast warns with the ratio', () => {
  const t = base(); t.colors.dim = t.colors.panel;
  const rs = runChecks(t, null);
  assert.ok(find(rs, 'warn', /dim.*1\.0:1/i).length >= 1);
});

test('missing or invalid colours are errors', () => {
  const t = base(); delete t.colors.grid; t.colors.ice = '#12345';
  const rs = runChecks(t, null);
  assert.ok(find(rs, 'error', /grid/).length >= 1);
  assert.ok(find(rs, 'error', /ice/).length >= 1);
});

test('status colour equal to void gets the device-substitution warning', () => {
  const t = base(); t.colors.amber = t.colors.void;
  const rs = runChecks(t, null);
  assert.ok(find(rs, 'warn', /amber.*(substitut|replace)/i).length >= 1);
  const u = base(); u.colors.red = u.colors.panel;
  assert.ok(find(runChecks(u, null), 'warn', /red.*(substitut|replace)/i).length >= 1);
});

test('witch near the background warns for the alert edge', () => {
  const t = base(); t.colors.witch = t.colors.void;
  assert.ok(find(runChecks(t, null), 'warn', /witch.*(substitut|replace)/i).length >= 1);
});

test('header: 18 ok, 19 error, lowercase is a note, quote and backslash are errors', () => {
  const t = base();
  t.header = 'A'.repeat(18); assert.equal(find(runChecks(t, null), 'error', /header/i).length, 0);
  t.header = 'A'.repeat(19); assert.ok(find(runChecks(t, null), 'error', /header/i).length >= 1);
  t.header = 'Midnight'; const rs = runChecks(t, null);
  assert.equal(find(rs, 'error', /header/i).length, 0);
  assert.ok(find(rs, 'warn', /header.*(capital|uppercase)/i).length >= 1);
  t.header = 'SAY "HI"'; assert.ok(find(runChecks(t, null), 'error', /header.*(quote|backslash)/i).length >= 1);
  t.header = 'A\\B'; assert.ok(find(runChecks(t, null), 'error', /header.*(quote|backslash)/i).length >= 1);
  t.header = 'A{B'; assert.ok(find(runChecks(t, null), 'error', /header/i).length >= 1);
  t.header = 'café'; assert.ok(find(runChecks(t, null), 'error', /header/i).length >= 1);
});

test('emblem size and colour limits', () => {
  const t = base(); t.centrepiece = 'emblem';
  assert.equal(errors(runChecks(t, emb(96, 96, 16))).length, 0);
  assert.ok(find(runChecks(t, emb(97, 97, 16)), 'error', /96/).length >= 1);
  assert.ok(find(runChecks(t, emb(96, 96, 17)), 'error', /16 colours/).length >= 1);
  assert.ok(find(runChecks(t, null), 'error', /emblem/i).length >= 1);
});

test('all four reserved names, a 21-char name, bad characters, quote', () => {
  const t = base();
  for (const n of ['UMBRAVOX', 'BAPH0MAU5', 'CVLT OF BAPH0MAU5', 'COSMIC MUFFIN']) {
    t.name = n; assert.ok(find(runChecks(t, null), 'error', /reserved|built-in/i).length >= 1, n);
  }
  t.name = 'A'.repeat(20); assert.equal(find(runChecks(t, null), 'error', /name/i).length, 0);
  t.name = 'A'.repeat(21); assert.ok(find(runChecks(t, null), 'error', /name/i).length >= 1);
  t.name = ''; assert.ok(find(runChecks(t, null), 'error', /name/i).length >= 1);
  t.name = 'lower'; assert.ok(find(runChecks(t, null), 'error', /name/i).length >= 1);
  t.name = 'A"B'; assert.ok(find(runChecks(t, null), 'error', /name/i).length >= 1);
});

test('author limits', () => {
  const t = base();
  t.author = 'a'.repeat(24); assert.equal(find(runChecks(t, null), 'error', /author/i).length, 0);
  t.author = 'a'.repeat(25); assert.ok(find(runChecks(t, null), 'error', /author/i).length >= 1);
  t.author = 'a"b'; assert.ok(find(runChecks(t, null), 'error', /author/i).length >= 1);
  t.author = ''; assert.equal(find(runChecks(t, null), 'error', /author/i).length, 0);
});

test('sweeps: at least one, and the start sweep must be offered', () => {
  const t = base();
  t.sweeps = []; assert.ok(find(runChecks(t, null), 'error', /sweep/i).length >= 1);
  t.sweeps = ['pulse']; t.sweep = 'radar';
  assert.ok(find(runChecks(t, null), 'error', /sweep/i).length >= 1);
  t.sweeps = ['pulse', 'radar']; assert.equal(find(runChecks(t, null), 'error', /sweep/i).length, 0);
});

test('pack size is reported against 64 KB', () => {
  const rs = runChecks(base(), null);
  assert.ok(find(rs, 'ok', /KB|bytes/).length >= 1);
});

test('contrast numbers are shown with one decimal', () => {
  const t = base(); t.colors.panel = '#000000'; t.colors.text = '#FFFFFF';
  assert.ok(find(runChecks(t, null), 'ok', /21\.0:1/).length >= 1);
});

test('malformed colours are named error results and never throw', () => {
  const t = base(); t.colors.witch = 'zzz';
  const rs = runChecks(t, null);
  assert.ok(find(rs, 'error', /colour 'witch' is not #RRGGBB: zzz/).length === 1);
  assert.equal(errors(rs).filter((r) => /not #RRGGBB/.test(r.text)).length, 1);
  for (const r of rs) assert.ok(!/NaN|Infinity/.test(r.text), r.text);
  for (const v of [5, null, undefined, '#FFF', {}, '']) {
    const u = base(); u.colors.text = v; u.colors.amber = v; u.colors.dim = v;
    let out;
    assert.doesNotThrow(() => { out = runChecks(u, null); }, String(v));
    assert.ok(find(out, 'error', /colour 'text' is not #RRGGBB/).length === 1);
    for (const r of out) assert.ok(!/NaN|Infinity/.test(r.text), r.text);
  }
});

test('runChecks survives themes with no or empty colours', () => {
  for (const th of [{}, { colors: {} }, { colors: null }, { colors: 7 }, { name: 'X', colors: undefined }, undefined, null]) {
    let out;
    assert.doesNotThrow(() => { out = runChecks(th, null); });
    assert.ok(find(out, 'error', /colour '\w+' is not #RRGGBB/).length >= 1);
    for (const r of out) assert.ok(!/NaN|Infinity/.test(r.text), r.text);
  }
});

test('a name longer than 12 characters warns that the settings screen cuts it', () => {
  const t = base(); t.name = 'ABCDEFGHIJKLM';
  const rs = runChecks(t, null);
  assert.equal(find(rs, 'warn', /first 12 characters.*13 chars/).length, 1);
  assert.equal(errors(rs).length, 0);
  t.name = 'ABCDEFGHIJKL';
  assert.equal(find(runChecks(t, null), 'warn', /first 12/).length, 0);
});
