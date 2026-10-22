import { test } from 'node:test';
import assert from 'node:assert/strict';
import { installToCard, fsAccessSupported, readCardThemes, inspectCard } from '../js/install.js';
import { StubDir } from './_stubfs.mjs';

const enc = (s) => new TextEncoder().encode(s);
const FILES = [
  { path: 'my-pack/theme.json', data: enc('{"format": 1}\n') },
  { path: 'my-pack/emblem.bin', data: Uint8Array.from([0x55, 0x56, 0x45, 1, 2, 2, 2, 0, 9, 9, 9, 9, 0, 1, 1, 0]) },
];

test('installToCard writes /themes/<folder>/ files and verifies by reading back', async () => {
  const root = new StubDir();
  const r = await installToCard(root, FILES);
  assert.equal(r.verified, true);
  assert.deepEqual(r.wrote, ['themes/my-pack/theme.json', 'themes/my-pack/emblem.bin']);
  assert.deepEqual(r.mismatched, []);
  assert.deepEqual([...root.get('themes/my-pack/emblem.bin')], [...FILES[1].data]);
});

test('a corrupted read-back is reported as verified:false and names the file', async () => {
  const root = new StubDir();
  root.readCorrupt = (name, d) => (name === 'emblem.bin' ? d.map((b, i) => (i === 10 ? b ^ 1 : b)) : d);
  const r = await installToCard(root, FILES);
  assert.equal(r.verified, false);
  assert.deepEqual(r.mismatched, ['themes/my-pack/emblem.bin']);
});

test('a truncated write (shorter read-back) is a mismatch too', async () => {
  const root = new StubDir();
  root.corrupt = (name, d) => (name === 'theme.json' ? d.slice(0, d.length - 1) : d);
  const r = await installToCard(root, FILES);
  assert.equal(r.verified, false);
  assert.deepEqual(r.mismatched, ['themes/my-pack/theme.json']);
});

test('a failing write returns ok:false with the error and what was written so far', async () => {
  const root = new StubDir();
  const themes = await root.getDirectoryHandle('themes', { create: true });
  const pack = await themes.getDirectoryHandle('my-pack', { create: true });
  pack.getFileHandle = async () => { throw new Error('card is read only'); };
  const r = await installToCard(root, FILES);
  assert.equal(r.ok, false);
  assert.equal(r.verified, false);
  assert.match(r.error, /read only/);
});

test('missing File System Access API reports unsupported', async () => {
  assert.equal(fsAccessSupported({}), false);
  assert.equal(fsAccessSupported({ showDirectoryPicker() {} }), true);
  const r = await installToCard(null, FILES);
  assert.equal(r.unsupported, true);
  assert.equal(r.verified, false);
  assert.deepEqual(r.wrote, []);
});

test('overwriting a pack whose old emblem.bin is no longer wanted removes it', async () => {
  const root = new StubDir();
  root.put('themes/my-pack/emblem.bin', 'old');
  root.put('themes/my-pack/notes.txt', 'keep me');
  const r = await installToCard(root, [FILES[0]]);
  assert.equal(r.verified, true);
  assert.deepEqual(r.removed, ['themes/my-pack/emblem.bin']);
  assert.ok(root.dirs.get('themes').dirs.get('my-pack').files.has('notes.txt'), 'unrelated files are never deleted');
  assert.equal(root.dirs.get('themes').dirs.get('my-pack').files.has('emblem.bin'), false);
});

test('readCardThemes lists folders under /themes with their theme.json name when readable', async () => {
  const root = new StubDir();
  root.put('themes/a-b/theme.json', '{"format":1,"name":"A.B"}');
  root.put('themes/broken/theme.json', 'not json');
  root.put('themes/empty/readme.txt', 'x');
  root.put('themes/loose-file.txt', 'x');
  const list = await readCardThemes(root);
  const by = Object.fromEntries(list.map((t) => [t.folder, t.name]));
  assert.deepEqual(by, { 'a-b': 'A.B', broken: null, empty: null });
  assert.deepEqual(await readCardThemes(new StubDir()), []);
});

test('inspectCard says what it found at the top of the folder', async () => {
  const root = new StubDir();
  root.put('diag.log', 'x'); root.put('WD-0042.CSV', 'x'); root.put('photo.jpg', 'x');
  const i = await inspectCard(root);
  assert.equal(i.looksLikeCard, true);
  assert.deepEqual(i.found.sort(), ['/WD-0042.CSV', '/diag.log']);
  const j = await inspectCard(new StubDir());
  assert.equal(j.looksLikeCard, false);
});
