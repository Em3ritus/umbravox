// Opening a gallery theme by its folder name: only a listed theme, only the four pack files, sizes capped.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { themeParam, fetchGalleryTheme } from '../js/gallery.js';
import { buildPack } from '../js/pack.js';
import { makeTemplate, openPackFiles } from '../js/model.js';

const SITE = 'https://t.example/';
const enc = (s) => new TextEncoder().encode(s);

// A fake fetch over a map of URL -> bytes (or a status number). Records every URL asked for.
function fakeFetch(map, calls, opts = {}) {
  return async (url) => {
    calls.push(String(url));
    const v = map[String(url)];
    if (v === undefined) return { ok: false, status: 404, headers: { get: () => null }, arrayBuffer: async () => new ArrayBuffer(0) };
    if (typeof v === 'number') return { ok: false, status: v, headers: { get: () => null }, arrayBuffer: async () => new ArrayBuffer(0) };
    return {
      ok: true, status: 200,
      headers: { get: (h) => (h.toLowerCase() === 'content-length' && !opts.noLength ? String(v.length) : null) },
      arrayBuffer: async () => v.buffer.slice(v.byteOffset, v.byteOffset + v.length),
    };
  };
}
const index = (themes) => enc(JSON.stringify({ version: 1, themes }));
const entry = (o = {}) => ({ folder: 'ember', name: 'EMBER', author: 'me', basedOn: '', format: 1, files: ['theme.json', 'emblem.bin'], ...o });

test('themeParam accepts only a folder name', () => {
  assert.equal(themeParam('?theme=midnight-ember'), 'midnight-ember');
  assert.equal(themeParam('?x=1&theme=A_1'), 'A_1');
  for (const s of ['', '?x=1', '?theme=', '?theme=../a', '?theme=https://x/y', '?theme=a/b', '?theme=a%2Fb', '?theme=a.b', '?theme=' + 'x'.repeat(32)]) {
    assert.equal(themeParam(s), null, s);
  }
});

test('a listed theme is fetched file by file from the themes site', async () => {
  const calls = [];
  const map = { [SITE + 'index.json']: index([entry()]), [SITE + 'themes/ember/theme.json']: enc('{}'), [SITE + 'themes/ember/emblem.bin']: new Uint8Array([1, 2, 3]) };
  const r = await fetchGalleryTheme('ember', fakeFetch(map, calls), SITE);
  assert.equal(r.ok, true, r.error);
  assert.deepEqual(r.files.map((f) => f.name), ['ember/theme.json', 'ember/emblem.bin']);
  assert.deepEqual([...r.files[1].data], [1, 2, 3]);
  assert.equal(r.entry.name, 'EMBER');
  assert.deepEqual(calls, [SITE + 'index.json', SITE + 'themes/ember/theme.json', SITE + 'themes/ember/emblem.bin']);
});

test('a folder that is not in the index fetches nothing else', async () => {
  const calls = [];
  const r = await fetchGalleryTheme('other', fakeFetch({ [SITE + 'index.json']: index([entry()]) }, calls), SITE);
  assert.equal(r.ok, false);
  assert.equal(r.error, 'That theme is not in the gallery.');
  assert.deepEqual(calls, [SITE + 'index.json']);
});

test('the folder must match the index exactly, and must be a folder name', async () => {
  for (const f of ['EMBER', '../ember', 'ember/', '']) {
    const calls = [];
    const r = await fetchGalleryTheme(f, fakeFetch({ [SITE + 'index.json']: index([entry()]) }, calls), SITE);
    assert.equal(r.ok, false, f);
    assert.ok(calls.length <= 1, 'no theme file asked for: ' + f);
  }
});

test('an index entry naming any other file is refused', async () => {
  for (const files of [['theme.json', '../x'], ['theme.json', 'readme.txt'], ['emblem.bin'], [], 'theme.json', ['theme.json', 'theme.json']]) {
    const calls = [];
    const r = await fetchGalleryTheme('ember', fakeFetch({ [SITE + 'index.json']: index([entry({ files })]) }, calls), SITE);
    assert.equal(r.ok, false, JSON.stringify(files));
    assert.deepEqual(calls, [SITE + 'index.json']);
  }
});

test('a file over its limit is refused, by its declared length or by what arrives', async () => {
  const big = new Uint8Array(800000);
  const map = { [SITE + 'index.json']: index([entry({ files: ['theme.json', 'radar.vid'] })]), [SITE + 'themes/ember/theme.json']: enc('{}'), [SITE + 'themes/ember/radar.vid']: big };
  for (const noLength of [false, true]) {
    const r = await fetchGalleryTheme('ember', fakeFetch(map, [], { noLength }), SITE);
    assert.equal(r.ok, false);
    assert.match(r.error, /radar\.vid/);
  }
});

test('a failed fetch gives a plain message', async () => {
  const r1 = await fetchGalleryTheme('ember', fakeFetch({}, []), SITE);
  assert.equal(r1.ok, false); assert.match(r1.error, /could not be loaded/);
  const r2 = await fetchGalleryTheme('ember', fakeFetch({ [SITE + 'index.json']: index([entry()]), [SITE + 'themes/ember/theme.json']: 500 }, []), SITE);
  assert.equal(r2.ok, false); assert.match(r2.error, /could not be loaded/);
  const r3 = await fetchGalleryTheme('ember', async () => { throw new Error('offline'); }, SITE);
  assert.equal(r3.ok, false); assert.match(r3.error, /could not be loaded/);
  const r4 = await fetchGalleryTheme('ember', fakeFetch({ [SITE + 'index.json']: enc('not json') }, []), SITE);
  assert.equal(r4.ok, false); assert.match(r4.error, /could not be loaded/);
});

test('the fetched files open through openPackFiles', async () => {
  const t = makeTemplate('cosmic');
  const pack = buildPack(t.theme, t.emblem);
  const folder = pack.files[0].path.split('/')[0];
  const map = { [SITE + 'index.json']: index([entry({ folder, files: pack.files.map((f) => f.path.split('/')[1]) })]) };
  for (const f of pack.files) map[SITE + 'themes/' + f.path] = f.data;
  const r = await fetchGalleryTheme(folder, fakeFetch(map, []), SITE);
  assert.equal(r.ok, true, r.error);
  const opened = openPackFiles(r.files);
  assert.equal(opened.ok, true, opened.error);
  assert.equal(opened.theme.name, t.theme.name);
  assert.ok(opened.emblem);
});
