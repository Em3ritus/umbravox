import { test } from 'node:test';
import assert from 'node:assert/strict';
import { installGate } from '../js/gate.js';
import { makeTemplate, blankTheme } from '../js/model.js';

test('a theme with a check error is blocked with the reason', () => {
  const g = installGate({ ...blankTheme(), name: 'UMBRAVOX' }, null);
  assert.equal(g.ok, false);
  assert.match(g.reason, /reserved/i);
});
test('too-long and empty names are blocked', () => {
  assert.equal(installGate({ ...blankTheme(), name: 'A'.repeat(21) }, null).ok, false);
  assert.equal(installGate({ ...blankTheme(), name: '' }, null).ok, false);
});
test('warnings only is allowed', () => {
  const t = makeTemplate('umbravox').theme;
  t.colors.dim = '#303030';                       // low-contrast warning, no error
  const g = installGate(t, null);
  assert.equal(g.ok, true, g.reason);
});
test('a clean template is allowed', () => {
  const m = makeTemplate('cosmic');
  assert.equal(installGate(m.theme, m.emblem).ok, true);
});
test('when buildPack throws the gate blocks with its message', () => {
  const t = makeTemplate('cosmic').theme;        // emblem centrepiece, no emblem
  const g = installGate(t, null);
  assert.equal(g.ok, false);
  assert.ok(g.reason.length > 5);
  const bad = { ...blankTheme(), name: 'OK' }; bad.colors = { ...bad.colors, void: 'nope' };
  const g2 = installGate(bad, null);
  assert.equal(g2.ok, false); assert.match(g2.reason, /void|colour/);
});

import { buildClip as _bc, CLIP_SIZE as _CS } from '../js/radarvideo.js';
import { makeTemplate as _mt } from '../js/model.js';
test('the gate passes a good clip and blocks a damaged one', () => {
  const t = _mt('umbravox').theme;
  const good = _bc([new Uint8Array(_CS * _CS).fill(3), new Uint8Array(_CS * _CS).fill(9)]);
  assert.equal(installGate(t, null, good).ok, true);
  const r = installGate(t, null, { ...good, w: 64 });
  assert.equal(r.ok, false);
  assert.match(r.reason, /102/);
});
