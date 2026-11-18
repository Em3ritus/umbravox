// The Layout step's model (js/layout.js): pure, with fake device rules. The real rules are exercised against the wasm in
// wasm-parity.test.mjs (presets and drafts must pass the device's checkLayout).
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { newLayout, setPiece, showPiece, isEmpty, toJson, fromJson, rectOf, draftOther } from '../js/layout.js';

const NAMES = ['menu', 'wordmark', 'status.gps', 'status.nodes', 'status.power', 'bridge', 'sd', 'nav.scan', 'nav.domain',
  'nav.codex', 'nav.poi', 'stop', 'count.wifi', 'count.5g', 'count.ble', 'radar', 'spectrum', 'channel', 'badge'];
// A few real defaults (ui/layout_engine.h); anything else off in a corner, so drafting overlap tests stay readable.
const DEF = {
  waveshare: { stop: { x: 384, y: 276, w: 96, h: 44 }, radar: { x: 188, y: 56, w: 209, h: 209 }, menu: { x: 0, y: 0, w: 44, h: 44 } },
  cyd: { stop: { x: 260, y: 196, w: 60, h: 44 }, radar: { x: 151, y: 52, w: 115, h: 115 }, menu: { x: 0, y: 0, w: 44, h: 44 } },
};
const rules = { names: NAMES, defaultRect: (b, n) => DEF[b][n] ?? { x: 0, y: 0, w: 0, h: 0 } };

test('an empty layout has no layout.json', () => {
  assert.equal(isEmpty(newLayout()), true);
  assert.equal(toJson(newLayout(), NAMES), null);
});

test('round trip through layout.json keeps both boards and the device order', () => {
  let L = setPiece(newLayout(), 'waveshare', 'stop', { x: 288, y: 276, w: 96, h: 44 });
  L = setPiece(L, 'waveshare', 'menu', 'hidden');
  L = setPiece(L, 'cyd', 'stop', { x: 208, y: 196, w: 52, h: 44 });
  const text = toJson(L, NAMES);
  assert.equal(text, '{"format":1,"waveshare":{"menu":"hidden","stop":[288,276,96,44]},"cyd":{"stop":[208,196,52,44]}}');
  const back = fromJson(text);
  assert.equal(toJson(back, NAMES), text);
});

test('the radar stays square', () => {
  const L = setPiece(newLayout(), 'waveshare', 'radar', { x: 10, y: 20, w: 100, h: 60 });
  assert.deepEqual(L.boards.waveshare.radar.rect, { x: 10, y: 20, w: 100, h: 100 });
});

test('hiding keeps the last rect; showing puts it back', () => {
  let L = setPiece(newLayout(), 'cyd', 'stop', { x: 1, y: 2, w: 50, h: 44 });
  L = setPiece(L, 'cyd', 'stop', 'hidden');
  assert.equal(rectOf(L, 'cyd', 'stop', rules), null);
  L = showPiece(L, 'cyd', 'stop');
  assert.deepEqual(rectOf(L, 'cyd', 'stop', rules), { x: 1, y: 2, w: 50, h: 44 });
  let M = setPiece(newLayout(), 'cyd', 'menu', 'hidden');
  M = showPiece(M, 'cyd', 'menu');
  assert.equal(M.boards.cyd.menu, undefined, 'never placed: back to the default');
});

test('the first board edited is the master; edits on the other are marked adjusted', () => {
  let L = setPiece(newLayout(), 'waveshare', 'stop', { x: 0, y: 0, w: 44, h: 44 });
  assert.equal(L.master, 'waveshare');
  L = setPiece(L, 'cyd', 'menu', { x: 0, y: 0, w: 44, h: 44 });
  assert.deepEqual(L.adjusted.cyd, ['menu']);
  assert.deepEqual(L.adjusted.waveshare, []);
});

test('drafting: near-edge pieces keep their scaled distance from that edge; centred stay centred', () => {
  let L = setPiece(newLayout(), 'waveshare', 'stop', { x: 380, y: 270, w: 90, h: 44 });   // right and bottom thirds
  L = setPiece(L, 'waveshare', 'radar', { x: 136, y: 56, w: 209, h: 209 });               // centred
  const D = draftOther(L, 'waveshare', 'cyd', rules);
  const k = 2 / 3;
  const s = D.boards.cyd.stop.rect;
  assert.equal(320 - (s.x + s.w), Math.round(10 * k));
  assert.equal(240 - (s.y + s.h), Math.round(6 * k));
  const r = D.boards.cyd.radar.rect;
  assert.equal(r.w, r.h);
  assert.ok(r.w <= 115, 'never past the CYD radar size');
  assert.ok(Math.abs(r.x + r.w / 2 - 160) <= 1, 'centred');
});

test('drafting: a 44 px button never drafts under 44; hidden stays hidden; untouched stays default', () => {
  let L = setPiece(newLayout(), 'waveshare', 'menu', { x: 0, y: 0, w: 44, h: 44 });
  L = setPiece(L, 'waveshare', 'sd', 'hidden');
  const D = draftOther(L, 'waveshare', 'cyd', rules);
  assert.ok(D.boards.cyd.menu.rect.w >= 44 && D.boards.cyd.menu.rect.h >= 44);
  assert.equal(D.boards.cyd.sd.hidden, true);
  assert.equal(D.boards.cyd.stop, undefined);
});

test('drafting: pieces the scaling pushes together are nudged apart', () => {
  // Two 44 px buttons side by side with a 4 px gap on the Waveshare: scaled 2/3 they would overlap (44 floor).
  let L = setPiece(newLayout(), 'waveshare', 'nav.scan', { x: 200, y: 140, w: 44, h: 44 });
  L = setPiece(L, 'waveshare', 'nav.domain', { x: 248, y: 140, w: 44, h: 44 });
  const D = draftOther(L, 'waveshare', 'cyd', rules);
  const a = D.boards.cyd['nav.scan'].rect, b = D.boards.cyd['nav.domain'].rect;
  const overlap = a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h;
  assert.equal(overlap, false);
});

test('drafting stays on the target screen', () => {
  let L = newLayout();
  for (const [i, n] of ['nav.scan', 'nav.domain', 'nav.codex'].entries()) L = setPiece(L, 'cyd', n, { x: 276, y: 196 - i * 50, w: 44, h: 44 });
  const D = draftOther(L, 'cyd', 'waveshare', rules);
  for (const n of ['nav.scan', 'nav.domain', 'nav.codex']) {
    const r = D.boards.waveshare[n].rect;
    assert.ok(r.x >= 0 && r.y >= 0 && r.x + r.w <= 480 && r.y + r.h <= 320, n);
  }
});

import { withDrafts } from '../js/layout.js';

test('an opened layout.json is checked and saved as it is until the first edit (review I2)', () => {
  const original = '{"format":7,"waveshare":{"stop":[0,0,96,44],"stop":[0,0,96,44]}}';
  const L = fromJson(original);
  assert.equal(toJson(L, NAMES), original, 'the device rules see the file the author opened');
  const edited = setPiece(L, 'waveshare', 'menu', 'hidden');
  assert.notEqual(toJson(edited, NAMES), original, 'an edit writes a fresh file');
});

test('fromJson ignores prototype keys and non-objects, and reports what it left out', () => {
  const L = fromJson('{"format":1,"waveshare":{"__proto__":[1,2,3,4],"stp":[1,2,3,4],"stop":[1,2,3]},"cyd":"hidden"}', NAMES);
  assert.equal(Object.getPrototypeOf(L.boards.waveshare), Object.prototype);
  assert.equal(Object.keys(L.boards.waveshare).length, 0);
  assert.ok(L.dropped.some((d) => /stp/.test(d)) && L.dropped.some((d) => /stop/.test(d)) && L.dropped.some((d) => /cyd/.test(d)));
  assert.throws(() => fromJson('null'), /layout\.json/);
});

test('before saving, a board the author never opened is drafted from the other (review I4)', () => {
  const L = setPiece(newLayout(), 'waveshare', 'stop', { x: 380, y: 270, w: 90, h: 44 });
  const D = withDrafts(L, rules, new Set(['waveshare']));
  assert.ok(D.boards.cyd.stop, 'the CYD got a draft');
  const visited = withDrafts(L, rules, new Set(['waveshare', 'cyd']));
  assert.equal(visited.boards.cyd.stop, undefined, 'a board the author opened (and left default) is not drafted over');
  assert.equal(withDrafts(newLayout(), rules, new Set()).master, null);
});
