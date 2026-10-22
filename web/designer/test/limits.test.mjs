import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { ROOT, read } from './_util.mjs';

const limits = JSON.parse(fs.readFileSync(path.join(ROOT, 'web/designer/js/limits.json'), 'utf8'));
const header = read('ui/themepack.h');
const cpp = read('ui/themepack.cpp');
const NUMERIC = ['MAX_PACKS', 'MAX_NAME', 'MAX_HEADER', 'MAX_AUTHOR', 'MAX_JSON', 'MAX_EMBLEM', 'MAX_PACK',
  'MAX_EMBLEM_DIM', 'MAX_EMBLEM_COLORS'];

test('numeric limits equal the constexprs in ui/themepack.h', () => {
  for (const k of NUMERIC) {
    const m = new RegExp('constexpr\\s+\\w+\\s+' + k + '\\s*=\\s*(\\d+)\\s*;').exec(header);
    assert.ok(m, 'constant ' + k + ' not found in ui/themepack.h');
    assert.equal(limits[k], Number(m[1]), k);
  }
  assert.equal(limits.MAX_FOLDERS_EXAMINED, Number(/constexpr\s+int\s+BAD_MAX\s*=\s*(\d+)/.exec(header)[1]));
  assert.equal(limits.MIN_EDGE_CONTRAST_X10, Number(/MIN_EDGE_CONTRAST_X10\s*=\s*(\d+)/.exec(read('ui/contrast.h'))[1]));
});

test('reserved names equal the list in admit()', () => {
  const m = /reserved\[\]\s*=\s*\{([^}]*)\}/.exec(cpp);
  assert.ok(m, 'reserved[] not found in ui/themepack.cpp');
  const names = [...m[1].matchAll(/"([^"]*)"/g)].map((x) => x[1]);
  assert.equal(names.length, 4);
  assert.deepEqual([...limits.reservedNames].sort(), [...names].sort());
});

test('folder rule matches folderNameOk', () => {
  const re = new RegExp(limits.folderName.pattern);
  for (const ok of ['a', 'A_b-9', 'x'.repeat(31)]) assert.ok(re.test(ok), ok);
  for (const bad of ['', '.', '..', 'a b', 'x'.repeat(32), 'a/b', 'é']) assert.ok(!re.test(bad), bad);
});

test('limitation texts: 14 entries, each with id, text and where', () => {
  const entries = Object.entries(limits.limitations);
  assert.equal(entries.length, 14);
  for (const [k, v] of entries) {
    assert.equal(v.id, k);
    assert.ok(typeof v.text === 'string' && v.text.length > 20 && v.text.length < 400, k);
    assert.ok(Array.isArray(v.where) && v.where.length > 0, k);
    for (const w of v.where) assert.ok(['design', 'install', 'docs', 'device'].includes(w), k + ':' + w);
    assert.ok(v.where.includes('docs'), k + ' must be in the docs');
  }
  assert.match(limits.limitations.packs.text, /32 folders/);
  assert.match(limits.limitations.skipped.text, /N SKIPPED/);
  assert.match(limits.limitations.skipped.text, /DIAG LOG/);
  assert.match(limits.limitations.characters.text, /quote/);
});

const KB = (n) => (n / 1024 === Math.floor(n / 1024) ? [n / 1024 + ' KB'] : [(n / 1024).toFixed(1) + ' KB']);
test('every numeric limit appears in docs/theme-packs.md',
  { skip: 'enabled by Task 12, which writes docs/theme-packs.md' }, () => {
    const doc = read('docs/theme-packs.md');
    for (const k of [...NUMERIC, 'MAX_FOLDERS_EXAMINED']) {
      const n = limits[k];
      const forms = [String(n), n.toLocaleString('en-US'), ...(n >= 1024 ? KB(n) : [])];
      assert.ok(forms.some((f) => doc.includes(f)), k + ' (' + forms.join(' / ') + ') missing from the docs');
    }
  });
