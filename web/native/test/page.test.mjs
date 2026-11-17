// The installer page's own links and version stamp (static checks on the shipped files).
import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';

const read = (p) => readFileSync(new URL(p, import.meta.url), 'utf8');
const html = read('../index.html');
const app = read('../app.mjs');

test('no link depends on GitHub "latest": it skips pre-releases, so it can point at a release with no files', () => {
  assert.equal(html.includes('releases/latest'), false, 'index.html');
  assert.equal(read('../../../docs/XIAO-NODE.md').includes('releases/latest'), false, 'docs/XIAO-NODE.md');
});

test('the XIAO download is the ZIP this site hosts next to the page', () => {
  // tools/assemble-native-installer.py copies each package to the site root under its own name.
  assert.match(html, /<a href="umbravox-xiao-c5-node\.zip">Download XIAO node firmware<\/a>/);
});

test('one installer stamp: the page text, the script tag and the module imports agree', () => {
  const shown = html.match(/<p>Installer ([0-9]{8}-[0-9]+)<\/p>/);
  assert.ok(shown, 'the page shows an installer stamp');
  const tags = [...html.matchAll(/\.mjs\?v=([0-9A-Za-z-]+)/g), ...app.matchAll(/\.mjs\?v=([0-9A-Za-z-]+)/g)].map((m) => m[1]);
  assert.equal(tags.length, 3, 'app.mjs in the page, flash.mjs and c5-loader.mjs in app.mjs');
  for (const t of tags) assert.equal(t, shown[1]);
});
