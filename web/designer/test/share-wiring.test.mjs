// The page is wired to the share link and the gallery loader (text checks: app.js needs a DOM to run).
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { read } from './_util.mjs';
import { renameWarning } from '../js/share.js';
import limits from '../js/limits.json' with { type: 'json' };

const app = read('web/designer/js/app.js'), html = read('web/designer/index.html');

test('the Share button no longer says sharing arrives later', () => {
  assert.equal(/Sharing arrives in a later release/.test(app), false);
});

test('app.js uses the share link and the gallery loader', () => {
  assert.match(app, /from '\.\/share\.js'/);
  assert.match(app, /from '\.\/gallery\.js'/);
  assert.match(app, /themeParam\(location\.search\)/);
  assert.match(app, /shareUrl\(/);
  assert.match(app, /'noopener'/);
});

test('Share is disabled with Download and Install while the checks have errors', () => {
  assert.match(app, /\['btn-download', 'btn-install', 'btn-share'\]/);
});

test('the Start view links to the gallery', () => {
  assert.match(html, /id="gallery-link"/);
  assert.match(app, /gallery-link.*THEMES_SITE|THEMES_SITE.*gallery-link/s);
});

test('a gallery theme shared under its gallery name gets a rename warning', () => {
  assert.equal(renameWarning('EMBER', 'EMBER'), 'Change the name before sharing: the gallery already has a theme called EMBER.');
  assert.equal(renameWarning('ember ', 'EMBER'), 'Change the name before sharing: the gallery already has a theme called EMBER.');
  assert.equal(renameWarning('EMBER TWO', 'EMBER'), '');
  assert.equal(renameWarning('EMBER', ''), '');
});

test('the sharing limitation is shown on Design and in the docs', () => {
  const l = limits.limitations.sharing;
  assert.equal(l.text, 'Sharing needs a GitHub account. Shared themes are public, and you must have the right to share any picture, video or sound in them.');
  assert.deepEqual(l.where, ['design', 'docs']);
});
