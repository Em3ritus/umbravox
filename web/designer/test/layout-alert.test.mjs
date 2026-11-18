// The Layout step's alert mode (js/layout-alert.js): DOM-free, so the frame clock, mode changes and the effect cap are
// tested here; app.js only wires them (layout-wiring.test.mjs).
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { MODES, PLAY_MS, STILL_MS, alertFrame, nextMode, editableRects, effectAllowed } from '../js/layout-alert.js';
import { newLayout, setStyle } from '../js/layout.js';

test('the modes, and one alert is 5 s as in the console TEST', () => {
  assert.deepEqual(MODES, ['scan', 'low', 'med', 'high']);
  assert.equal(PLAY_MS, 5000); assert.equal(STILL_MS, 3000);
});

test('alertFrame: scan runs the clock; a still alert is 3 s in; Play loops from 0', () => {
  assert.equal(alertFrame('scan', null, 1000), 50);
  assert.equal(alertFrame('med', null, 123456), 150);
  assert.equal(alertFrame('high', 1000, 1000), 0);
  assert.equal(alertFrame('low', 1000, 6200), 10);
  assert.equal(alertFrame('low', 1000, 5999), 249);
});

test('nextMode: changing board or screen goes back to Scan and stops Play', () => {
  assert.deepEqual(nextMode('med', 'board'), { mode: 'scan', playing: false });
  assert.deepEqual(nextMode('high', 'screen'), { mode: 'scan', playing: false });
  assert.deepEqual(nextMode('scan', 'mode:high'), { mode: 'high', playing: false });
  assert.throws(() => nextMode('scan', 'mode:loud'));
});

test('editableRects: Scan edits the pieces; an alert level only its card', () => {
  const pieces = { stop: { x: 1, y: 2, w: 44, h: 44 }, menu: { x: 0, y: 0, w: 44, h: 44 } }, card = { x: 9, y: 9, w: 240, h: 120 };
  assert.equal(editableRects('scan', pieces, card), pieces);
  assert.deepEqual(editableRects('med', pieces, card), { 'alert.card.med': card });
});

test('effectAllowed: at the cap only pieces that already have an effect may change it', () => {
  const r = { x: 0, y: 0, w: 44, h: 44 };
  let L = setStyle(newLayout(), 'cyd', 'stop', { effect: 'pulse' }, r);
  L = setStyle(L, 'cyd', 'menu', { effect: 'flash' }, r);
  assert.equal(effectAllowed(L, 'cyd', 'nav.scan', 2), false);
  assert.equal(effectAllowed(L, 'cyd', 'stop', 2), true);
  assert.equal(effectAllowed(L, 'waveshare', 'nav.scan', 4), true);
});
