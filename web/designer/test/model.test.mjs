import { test } from 'node:test';
import assert from 'node:assert/strict';
import { runChecks } from '../js/checks.js';
import { buildPack, themeJson } from '../js/pack.js';
import { emblemToBin } from '../js/emblem.js';
import { ROLES } from '../js/palette.js';
import limits from '../js/limits.json' with { type: 'json' };
import {
  TEMPLATES, makeTemplate, blankTheme, sanitizeText, themeFromState, parseThemeJson,
  emblemFromBin, openPackFiles, sweepId, startSweep,
} from '../js/model.js';
import { writePack, checkPack } from './_util.mjs';

const errors = (t, e) => runChecks(t, e).filter((c) => c.level === 'error');

test('there are three built-in templates, none of them under a reserved name', () => {
  assert.deepEqual(TEMPLATES.map((t) => t.id), ['umbravox', 'baph0mau5', 'cosmic']);
  for (const t of TEMPLATES) assert.ok(!limits.reservedNames.includes(makeTemplate(t.id).theme.name), t.id);
});

for (const id of ['umbravox', 'baph0mau5', 'cosmic']) {
  test(`template ${id}: no check errors, and the real parser accepts the built pack`, () => {
    const { theme, emblem } = makeTemplate(id);
    assert.deepEqual(errors(theme, emblem), []);
    for (const r of ROLES) assert.match(theme.colors[r], /^#[0-9A-F]{6}$/);
    const pack = buildPack(theme, emblem);
    const r = checkPack(writePack(pack));
    assert.equal(r.status, 0, r.out);
  });
}

test('the cosmic template carries the muffin emblem; the others are heptagrams', () => {
  const c = makeTemplate('cosmic');
  assert.equal(c.theme.centrepiece, 'emblem');
  assert.ok(c.emblem && c.emblem.w === 72 && c.emblem.ncol <= 16);
  assert.equal(makeTemplate('umbravox').emblem, null);
  assert.equal(makeTemplate('umbravox').theme.centrepiece, 'heptagram');
});

test('templates are fresh copies: editing one does not change the next', () => {
  const a = makeTemplate('umbravox'); a.theme.colors.void = '#FFFFFF'; a.theme.sweeps.pop();
  const b = makeTemplate('umbravox');
  assert.notEqual(b.theme.colors.void, '#FFFFFF');
  assert.equal(b.theme.sweeps.length, 10);
});

test('the blank theme is valid (no errors) and neutral', () => {
  const t = blankTheme();
  assert.deepEqual(errors(t, null), []);
  assert.equal(buildPack(t, null).files.length, 1);
});

test('sanitizeText strips double quotes and backslashes and says it did', () => {
  assert.deepEqual(sanitizeText('A"B\\C'), { value: 'ABC', stripped: true });
  assert.deepEqual(sanitizeText('PLAIN'), { value: 'PLAIN', stripped: false });
  assert.equal(sanitizeText('""\\\\').value, '');
});

test('themeFromState: the start sweep is always one the pack offers', () => {
  const t = { ...blankTheme(), sweeps: ['pulse', 'star'], sweep: 'radar' };
  assert.equal(themeFromState({ theme: t }).sweep, 'pulse');
  assert.equal(startSweep({ sweeps: ['star'], sweep: 'star' }), 'star');
  assert.equal(startSweep({ sweeps: [], sweep: 'radar' }), null);
});

test('themeFromState does not mutate its input and strips banned characters', () => {
  const st = { theme: { ...blankTheme(), name: 'A"B' } };
  const t = themeFromState(st);
  assert.equal(t.name, 'AB');
  assert.equal(st.theme.name, 'A"B');
});

test('sweepId maps pack names to the device ids (gaze is 1 and never offered)', () => {
  assert.equal(sweepId('radar'), 0);
  assert.equal(sweepId('pulse'), 2);
  assert.equal(sweepId('aurora'), 10);
  assert.equal(sweepId('gaze'), -1);
  for (const n of limits.sweepNames) assert.ok(sweepId(n) >= 0, n);
});

test('round trip: buildPack(parseThemeJson(theme.json)) reproduces the same pack', () => {
  for (const id of ['umbravox', 'baph0mau5', 'cosmic']) {
    const { theme, emblem } = makeTemplate(id);
    const text = themeJson(theme);
    const r = parseThemeJson(text);
    assert.equal(r.ok, true, id);
    assert.deepEqual(r.warnings, []);
    assert.equal(themeJson(r.theme), text, id);
    if (emblem) {
      const e = emblemFromBin(emblemToBin(emblem));
      assert.deepEqual(e, emblem);
      assert.deepEqual(buildPack(r.theme, e).files.map((f) => [...f.data]), buildPack(theme, emblem).files.map((f) => [...f.data]));
    }
  }
});

test('the committed sim fixtures open and rebuild (a pack written by hand, not by pack.js)', async () => {
  const fs = await import('node:fs');
  const { ROOT } = await import('./_util.mjs');
  for (const d of ['umbravox-clone', 'cosmic-clone']) {
    const dir = `${ROOT}/sim/packs/${d}`;
    const files = fs.readdirSync(dir).map((n) => ({ name: n, data: new Uint8Array(fs.readFileSync(`${dir}/${n}`)) }));
    const r = openPackFiles(files);
    assert.equal(r.ok, true, d + ' ' + r.error);
    assert.deepEqual(errors(r.theme, r.emblem), []);
  }
});

test('malformed theme.json gives a clear error and never throws', () => {
  for (const bad of ['', 'not json', '{', '[]', 'null', '42', '{"format":1,"name":5}', '{"format":2,"name":"X"}']) {
    let r;
    assert.doesNotThrow(() => { r = parseThemeJson(bad); }, JSON.stringify(bad));
    assert.equal(r.ok, false, JSON.stringify(bad));
    assert.ok(typeof r.error === 'string' && r.error.length > 8, JSON.stringify(bad));
  }
  assert.match(parseThemeJson('{').error, /not valid JSON/i);
});

test('a theme.json with missing colours loads with a warning naming them', () => {
  const r = parseThemeJson('{"format":1,"name":"X","colors":{"void":"#000000"}}');
  assert.equal(r.ok, true);
  assert.ok(r.warnings.some((w) => /colour/.test(w) && /panel/.test(w)));
  assert.equal(r.theme.colors.void, '#000000');
  assert.deepEqual(errors(r.theme, null), []);
});

test('openPackFiles: emblem centrepiece without emblem.bin is a clear error; wrong files are ignored', () => {
  const { theme } = makeTemplate('cosmic');
  const js = new TextEncoder().encode(themeJson(theme));
  const r = openPackFiles([{ name: 'theme.json', data: js }]);
  assert.equal(r.ok, false);
  assert.match(r.error, /emblem\.bin/);
  const none = openPackFiles([{ name: 'readme.txt', data: js }]);
  assert.equal(none.ok, false);
  assert.match(none.error, /theme\.json/);
});

test('emblemFromBin rejects garbage with null, never throws', () => {
  for (const b of [new Uint8Array(0), new Uint8Array([1, 2, 3]), Uint8Array.from({ length: 64 }, () => 7)]) {
    assert.equal(emblemFromBin(b), null);
  }
});

import { selectPackFiles } from '../js/model.js';
const mk = (name, size = 10, rel = '') => ({ name, webkitRelativePath: rel, size });

test('selectPackFiles: a 100 MB theme.json is rejected by size, naming the file', () => {
  const r = selectPackFiles([mk('theme.json', 100 * 1024 * 1024)]);
  assert.equal(r.ok, false); assert.match(r.error, /theme\.json/); assert.match(r.error, /4096/);
});
test('selectPackFiles: an oversized emblem.bin is rejected by size', () => {
  const r = selectPackFiles([mk('theme.json'), mk('emblem.bin', 50000)]);
  assert.equal(r.ok, false); assert.match(r.error, /emblem\.bin/);
});
test('selectPackFiles: everything but theme.json and emblem.bin is ignored (never read)', () => {
  const r = selectPackFiles([mk('big.csv', 1e9, 'card/big.csv'), mk('theme.json', 100, 'card/p/theme.json'), mk('diag.log', 1e8, 'card/p/diag.log')]);
  assert.equal(r.ok, true);
  assert.deepEqual(r.picked.map((f) => f.name), ['theme.json']);
});
test('selectPackFiles: two theme.json (e.g. /themes picked) is refused', () => {
  const r = selectPackFiles([mk('theme.json', 10, 'themes/a/theme.json'), mk('theme.json', 10, 'themes/b/theme.json')]);
  assert.equal(r.ok, false); assert.match(r.error, /more than one theme\.json/i);
});
test('selectPackFiles: matching pair in one folder accepted; emblem from another folder refused', () => {
  const ok = selectPackFiles([mk('theme.json', 10, 'p/theme.json'), mk('emblem.bin', 10, 'p/emblem.bin')]);
  assert.equal(ok.ok, true); assert.equal(ok.picked.length, 2);
  const bad = selectPackFiles([mk('theme.json', 10, 'p/theme.json'), mk('emblem.bin', 10, 'q/emblem.bin')]);
  assert.equal(bad.ok, false); assert.match(bad.error, /same folder/i);
});
test('selectPackFiles: loose files without a path are accepted', () => {
  const r = selectPackFiles([mk('theme.json'), mk('emblem.bin')]);
  assert.equal(r.ok, true); assert.equal(r.picked.length, 2);
});
test('selectPackFiles: no theme.json is a clear error', () => {
  assert.match(selectPackFiles([mk('readme.txt')]).error, /theme\.json/);
});
test('openPackFiles applies the same rules to data it was handed', () => {
  const d = new Uint8Array(5);
  const r = openPackFiles([{ name: 'a/theme.json', data: d }, { name: 'b/theme.json', data: d }]);
  assert.equal(r.ok, false); assert.match(r.error, /more than one/i);
});

test('a theme.json with no sweeps key warns that the device requires it; the exported pack still lists all sweeps', () => {
  const r = parseThemeJson('{"format":1,"name":"X","colors":{"void":"#000000"}}');
  assert.equal(r.ok, true);
  assert.ok(r.warnings.some((w) => /no sweeps listed/.test(w) && /device requires/.test(w) && /all sweeps were offered/.test(w)));
  assert.deepEqual(r.theme.sweeps, limits.sweepNames);
  const r2 = parseThemeJson('{"format":1,"name":"X","sweeps":["radar"],"colors":{}}');
  assert.ok(!r2.warnings.some((w) => /no sweeps listed/.test(w)));
});
