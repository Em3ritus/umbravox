import { test } from 'node:test';
import assert from 'node:assert/strict';
import limits from '../js/limits.json' with { type: 'json' };
import { suffixedFolder, retargetFiles, planInstall } from '../js/plan.js';

const FOLDER = new RegExp(limits.folderName.pattern);

test('a free folder name is kept; a taken one gets -2, -3, ...', () => {
  assert.equal(suffixedFolder('midnight', []), 'midnight');
  assert.equal(suffixedFolder('midnight', ['midnight']), 'midnight-2');
  assert.equal(suffixedFolder('midnight', ['midnight', 'midnight-2']), 'midnight-3');
});

test('FAT32 is case-insensitive: Midnight collides with midnight', () => {
  assert.equal(suffixedFolder('midnight', ['MIDNIGHT']), 'midnight-2');
});

test('the suffixed name never exceeds 31 characters', () => {
  const base = 'a'.repeat(31);
  const s = suffixedFolder(base, [base]);
  assert.equal(s, 'a'.repeat(29) + '-2');
  assert.equal(s.length, 31);
  const s3 = suffixedFolder(base, [base, s]);
  assert.equal(s3, 'a'.repeat(29) + '-3');
  assert.match(s3, FOLDER);
  // a 10th suffix is two digits
  const taken = [base]; for (let i = 2; i <= 9; i++) taken.push('a'.repeat(29) + '-' + i);
  assert.equal(suffixedFolder(base, taken), 'a'.repeat(28) + '-10');
});

test('truncation does not leave a dash before the suffix', () => {
  const base = 'a'.repeat(28) + '-bc';           // 31 chars, dash at index 28
  const s = suffixedFolder(base, [base]);
  assert.ok(s.length <= 31); assert.doesNotMatch(s, /--/); assert.match(s, FOLDER);
});

test('retargetFiles moves the files to the new folder and nothing else', () => {
  const f = [{ path: 'a/theme.json', data: new Uint8Array([1]) }, { path: 'a/emblem.bin', data: new Uint8Array([2]) }];
  const r = retargetFiles(f, 'a-2');
  assert.deepEqual(r.map((x) => x.path), ['a-2/theme.json', 'a-2/emblem.bin']);
  assert.equal(r[0].data, f[0].data);
  assert.equal(f[0].path, 'a/theme.json', 'input untouched');
});

test('planInstall: no collision when the folder is free', () => {
  const p = planInstall('midnight', 'MIDNIGHT', [{ folder: 'other', name: 'OTHER' }]);
  assert.equal(p.collision, null);
  assert.equal(p.folder, 'midnight');
});

test('planInstall: a collision reports what is there and offers a free alternative', () => {
  const p = planInstall('a-b', 'A B', [{ folder: 'a-b', name: 'A.B' }, { folder: 'a-b-2', name: null }]);
  assert.deepEqual(p.collision, { folder: 'a-b', name: 'A.B' });
  assert.equal(p.alternative, 'a-b-3');
  assert.equal(p.sameName, false);
});

test('planInstall: the same theme name already in the folder is flagged, so a second folder is not offered blindly', () => {
  const p = planInstall('midnight-ember', 'MIDNIGHT EMBER', [{ folder: 'midnight-ember', name: 'MIDNIGHT EMBER' }]);
  assert.equal(p.sameName, true);
});

test('planInstall: the same name in a different folder is a clash even without a folder collision', () => {
  const p = planInstall('new-one', 'X', [{ folder: 'old-one', name: 'X' }]);
  assert.equal(p.collision, null);
  assert.deepEqual(p.nameClash, { folder: 'old-one', name: 'X' });
});

test('planInstall: collision is case-insensitive on the folder', () => {
  const p = planInstall('midnight', 'M', [{ folder: 'Midnight', name: 'Q' }]);
  assert.equal(p.collision.folder, 'Midnight');
  assert.equal(p.folder, 'midnight');
});
