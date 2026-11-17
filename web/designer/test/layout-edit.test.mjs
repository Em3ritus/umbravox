// Pointer maths for the Layout step (js/layout-edit.js): page coordinates to device pixels, picking, drag and resize.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { toDevice, pickPiece, handleAt, dragTo, nudge, Z_ORDER } from '../js/layout-edit.js';

test('page to device pixels at any CSS scale', () => {
  assert.deepEqual(toDevice(150, 90, { left: 100, top: 50, width: 240, height: 160 }, 480, 320), { x: 100, y: 80 });   // 50% scale
  assert.deepEqual(toDevice(300, 200, { left: 100, top: 100, width: 960, height: 640 }, 480, 320), { x: 100, y: 50 }); // 200%
  assert.deepEqual(toDevice(0, 0, { left: 100, top: 100, width: 480, height: 320 }, 480, 320), { x: 0, y: 0 }, 'clamped on the screen');
});

test('the topmost piece is picked: chrome over tiles over the radar group', () => {
  const rects = { radar: { x: 0, y: 0, w: 100, h: 100 }, badge: { x: 0, y: 0, w: 40, h: 40 }, 'count.wifi': { x: 50, y: 50, w: 60, h: 60 }, stop: { x: 90, y: 90, w: 44, h: 44 } };
  assert.equal(pickPiece(rects, 95, 95), 'stop');
  assert.equal(pickPiece(rects, 60, 60), 'count.wifi');
  assert.equal(pickPiece(rects, 10, 10), 'badge', 'the badge is drawn over the radar');
  assert.equal(pickPiece(rects, 70, 20), 'radar');
  assert.equal(pickPiece(rects, 300, 300), null);
  assert.equal(pickPiece({ radar: null }, 1, 1), null, 'hidden pieces are never picked');
  assert.ok(Z_ORDER.indexOf('stop') > Z_ORDER.indexOf('count.wifi') && Z_ORDER.indexOf('count.wifi') > Z_ORDER.indexOf('radar'));
});

test('corner handles within 8 px', () => {
  const r = { x: 100, y: 100, w: 50, h: 40 };
  assert.equal(handleAt(r, 102, 103, 8), 'nw');
  assert.equal(handleAt(r, 148, 138, 8), 'se');
  assert.equal(handleAt(r, 151, 100, 8), 'ne');
  assert.equal(handleAt(r, 100, 141, 8), 'sw');
  assert.equal(handleAt(r, 125, 120, 8), null);
});

test('moving snaps to 2 px and stays on the screen', () => {
  const b = { w: 480, h: 320 };
  assert.deepEqual(dragTo({ x: 10, y: 10, w: 44, h: 44 }, { x: 20, y: 20 }, { x: 33, y: 27 }, 'move', b), { x: 22, y: 16, w: 44, h: 44 });
  assert.deepEqual(dragTo({ x: 10, y: 10, w: 44, h: 44 }, { x: 20, y: 20 }, { x: 900, y: -50 }, 'move', b), { x: 436, y: 0, w: 44, h: 44 });
});

test('resizing from a corner never goes under 1 px or past the screen', () => {
  const b = { w: 480, h: 320 };
  assert.deepEqual(dragTo({ x: 10, y: 10, w: 44, h: 44 }, { x: 54, y: 54 }, { x: 64, y: 74 }, 'se', b), { x: 10, y: 10, w: 54, h: 64 });
  assert.deepEqual(dragTo({ x: 10, y: 10, w: 44, h: 44 }, { x: 10, y: 10 }, { x: 200, y: 200 }, 'nw', b), { x: 52, y: 52, w: 2, h: 2 }, 'nw past the opposite corner stops at 2 px (snap)');
  assert.deepEqual(dragTo({ x: 400, y: 10, w: 44, h: 44 }, { x: 444, y: 54 }, { x: 600, y: 54 }, 'se', b), { x: 400, y: 10, w: 80, h: 44 });
});

test('a radar corner drag stays square', () => {
  const r = dragTo({ x: 100, y: 50, w: 101, h: 101 }, { x: 201, y: 151 }, { x: 241, y: 161 }, 'se', { w: 480, h: 320 }, { square: true });
  assert.equal(r.w, r.h);
  assert.equal(r.x, 100);
  assert.equal(r.y, 50);
});

test('arrow keys nudge 1 px, Shift 8 px', () => {
  const r = { x: 10, y: 10, w: 5, h: 5 };
  assert.deepEqual(nudge(r, 'ArrowLeft', false), { x: 9, y: 10, w: 5, h: 5 });
  assert.deepEqual(nudge(r, 'ArrowDown', true), { x: 10, y: 18, w: 5, h: 5 });
  assert.equal(nudge(r, 'a', false), null);
});
