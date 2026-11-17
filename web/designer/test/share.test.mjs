// The Share button's link: the themes repository's submission form with its fields filled.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { shareUrl, THEMES_REPO, THEMES_SITE, FOLDER_RE } from '../js/share.js';

test('the share link opens the submission form with the fields filled', () => {
  const u = new URL(shareUrl({ name: 'MIDNIGHT EMBER', author: 'A & B' }, 'cosmic-two'));
  assert.equal(u.origin + u.pathname, 'https://github.com/' + THEMES_REPO + '/issues/new');
  assert.equal(u.searchParams.get('template'), 'submit-theme.yml');
  assert.equal(u.searchParams.get('title'), 'Theme: MIDNIGHT EMBER');
  assert.equal(u.searchParams.get('theme-name'), 'MIDNIGHT EMBER');
  assert.equal(u.searchParams.get('author'), 'A & B');
  assert.equal(u.searchParams.get('based-on'), 'cosmic-two');
});

test('no author and no based-on leave those fields out', () => {
  const u = new URL(shareUrl({ name: 'X', author: '' }));
  assert.equal(u.searchParams.has('author'), false);
  assert.equal(u.searchParams.has('based-on'), false);
});

test('a based-on that is not a folder name is dropped', () => {
  for (const bad of ['../x', 'a b', 'https://x/y']) assert.equal(new URL(shareUrl({ name: 'X' }, bad)).searchParams.has('based-on'), false);
});

test('the folder rule', () => {
  for (const ok of ['a', 'midnight-ember', 'A_1', 'x'.repeat(31)]) assert.ok(FOLDER_RE.test(ok), ok);
  for (const bad of ['', '..', 'a/b', 'a b', 'x'.repeat(32), 'a.b', 'a\n']) assert.equal(FOLDER_RE.test(bad), false, JSON.stringify(bad));
});

test('the themes site is an https folder address', () => {
  assert.match(THEMES_SITE, /^https:\/\/[a-z0-9.-]+\/[a-z0-9-]+\/$/);
});
