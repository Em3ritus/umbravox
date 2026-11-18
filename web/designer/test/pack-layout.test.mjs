// layout.json travels with the pack: built, opened again, installed (folder and USB), and refused for sharing until the
// community repository accepts it (plan 6).
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { read } from './_util.mjs';
import { buildPack } from '../js/pack.js';
import { selectPackFiles, openPackFiles, makeTemplate } from '../js/model.js';
import { checkFiles } from '../js/usbinstall.js';
import limits from '../js/limits.json' with { type: 'json' };

const LAYOUT = '{"format":1,"waveshare":{"wordmark":"hidden"},"cyd":{"wordmark":"hidden"}}';
const t = makeTemplate('umbravox').theme;

test('a pack with a layout carries layout.json; without one it is byte-identical to before', () => {
  const plain = buildPack(t, null);
  const same = buildPack(t, null, null, null, null);
  assert.deepEqual(same.files.map((f) => [f.path, [...f.data]]), plain.files.map((f) => [f.path, [...f.data]]));
  const p = buildPack(t, null, null, null, LAYOUT);
  const lay = p.files.find((f) => f.path.endsWith('/layout.json'));
  assert.ok(lay);
  assert.equal(new TextDecoder().decode(lay.data), LAYOUT);
  assert.equal(p.files.length, plain.files.length + 1);
});

test('opening a folder with layout.json brings the layout back', () => {
  const p = buildPack(t, null, null, null, LAYOUT);
  const files = p.files.map((f) => ({ name: f.path, data: f.data, size: f.data.length }));
  assert.ok(selectPackFiles(files).picked.some((f) => f.name.endsWith('layout.json')));
  const r = openPackFiles(files);
  assert.equal(r.ok, true);
  assert.equal(r.layout, LAYOUT);
  assert.equal(openPackFiles(buildPack(t, null).files.map((f) => ({ name: f.path, data: f.data }))).layout, null);
});

test('an over-size layout.json is not read', () => {
  const big = { name: 'x/layout.json', data: new Uint8Array(limits.MAX_LAYOUT_JSON + 1), size: limits.MAX_LAYOUT_JSON + 1 };
  const theme = buildPack(t, null).files[0];
  const r = selectPackFiles([{ name: 'x/theme.json', data: theme.data, size: theme.data.length }, big]);
  assert.equal(r.ok, false);
  assert.match(r.error, /layout\.json/);
});

test('USB install sends layout.json before theme.json', () => {
  const r = checkFiles(buildPack(t, null, null, null, LAYOUT).files);
  assert.equal(r.ok, true, r.error);
  const names = r.ordered.map((f) => f.name);
  assert.ok(names.includes('layout.json'));
  assert.equal(names[names.length - 1], 'theme.json');
});

test('a folder install removes a stale layout.json the new pack does not have', () => {
  assert.match(read('web/designer/js/install.js'), /\['emblem\.bin', 'radar\.vid', 'alert\.snd', 'layout\.json'\]/);
});

test('Share is refused, with a message, while the theme has a layout', () => {
  const app = read('web/designer/js/app.js');
  assert.match(app, /layoutText\(\)[^;]*\n?[^]*?share/i);
  assert.match(app, /Sharing a theme with a layout/);
});

test('the limits know layout.json and say what a layout changes', () => {
  assert.equal(limits.MAX_LAYOUT_JSON, 16384);
  assert.doesNotMatch(limits.limitations.presets.text, /no custom screen layout/);
  assert.match(limits.limitations.layouts.text, /engine screens/);
});
