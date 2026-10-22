import { test } from 'node:test';
import assert from 'node:assert/strict';
import limits from '../js/limits.json' with { type: 'json' };
import { limitsFor, placeLimitations, renderLimitList, escapeHtml, DESIGN_PLACES } from '../js/limitations.js';

const all = Object.values(limits.limitations);
const ids = (where) => all.filter((l) => l.where.includes(where)).map((l) => l.id);

test('limitsFor(design) returns every entry whose where includes design, and only those', () => {
  assert.deepEqual(limitsFor(limits, 'design').map((l) => l.id), ids('design'));
  for (const id of ['centrepiece', 'header', 'characters', 'rgb565', 'presets', 'builtins']) {
    assert.ok(ids('design').includes(id), id);
  }
});

test('limitsFor(install) includes reboot, packs, browser, fat32, trust', () => {
  const got = limitsFor(limits, 'install').map((l) => l.id);
  assert.deepEqual(got, ids('install'));
  for (const id of ['reboot', 'packs', 'browser', 'fat32', 'trust']) assert.ok(got.includes(id), id);
});

test('the rendered Design list contains the text of every design entry, from limits.json', () => {
  const html = renderLimitList(limitsFor(limits, 'design'));
  for (const l of limitsFor(limits, 'design')) assert.ok(html.includes(escapeHtml(l.text)), l.id);
  for (const l of limitsFor(limits, 'install')) {
    if (!l.where.includes('design')) assert.ok(!html.includes(escapeHtml(l.text)), l.id + ' must not be on Design');
  }
});

test('the rendered Install list contains the text of every install entry', () => {
  const html = renderLimitList(limitsFor(limits, 'install'));
  for (const l of limitsFor(limits, 'install')) assert.ok(html.includes(escapeHtml(l.text)), l.id);
});

test('placeLimitations puts each design entry in a panel; nothing is dropped, nothing duplicated', () => {
  const placed = placeLimitations(limits, 'design', DESIGN_PLACES);
  const flat = Object.values(placed).flat().map((l) => l.id);
  assert.deepEqual([...flat].sort(), ids('design').sort());
  assert.equal(new Set(flat).size, flat.length);
});

test('an entry with no assigned panel still shows up (under "rest")', () => {
  const fake = { limitations: { x: { id: 'x', text: 'new thing', where: ['design'] } } };
  const placed = placeLimitations(fake, 'design', {});
  assert.deepEqual(placed.rest.map((l) => l.id), ['x']);
});

test('limitation text is escaped even though it is trusted', () => {
  const html = renderLimitList([{ id: 'a', text: '<img src=x onerror=1> & "q"', where: ['design'] }]);
  assert.ok(!html.includes('<img'));
  assert.ok(html.includes('&lt;img'));
  assert.ok(html.includes('&amp;'));
});
