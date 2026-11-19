// The Layout step's folded sections: the one-line summary each shows while closed (js/folds.js), and the page wiring.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { read } from './_util.mjs';
import { foldSummaries } from '../js/folds.js';
import { newLayout, setPiece, setBoardStyle, addDecor } from '../js/layout.js';

const NAMES = ['menu', 'stop', 'sd', 'bridge'];

test('a fresh layout: no uploads, no pictures, built-in screens, nothing hidden', () => {
  const s = foldSummaries(newLayout(), 'cyd', { fonts: 0, names: NAMES, screen: 'scan' });
  assert.equal(s.media, 'no uploads');
  assert.equal(s.screens, 'built-in look · preview Scan');
  assert.equal(s.hidden, 'none');
});

test('counts uploaded fonts and this board\'s pictures, lists what the screen style sets, counts hidden pieces', () => {
  let L = setPiece(newLayout(), 'waveshare', 'sd', 'hidden');
  L = setPiece(L, 'waveshare', 'bridge', 'hidden');
  L = addDecor(L, 'waveshare', 'pic:1:a.png', { x: 0, y: 0, w: 10, h: 10 }).L;
  L = setBoardStyle(L, 'waveshare', { title: 'upload:x', bg: 'pic:2:b.png', highlight: { fill: 'violet' }, icons: ['pic:3:c.png'] });
  const s = foldSummaries(L, 'waveshare', { fonts: 2, names: NAMES, screen: 'mesh' });
  assert.equal(s.media, '2 fonts · 1 picture');
  assert.equal(s.screens, 'titles, highlight, background, icons · preview Mesh');
  assert.equal(s.hidden, '2 hidden');
  assert.equal(foldSummaries(L, 'cyd', { fonts: 1, names: NAMES, screen: 'scan' }).hidden, 'none', 'per board');
  assert.equal(foldSummaries(L, 'cyd', { fonts: 1, names: NAMES, screen: 'scan' }).media, '1 font');
});

test('the page folds Fonts and pictures, Other screens and Hidden pieces, each with its summary', () => {
  const html = read('web/designer/index.html'), app = read('web/designer/js/app.js');
  for (const [id, sum] of [['fold-media', 'sum-media'], ['fold-screens', 'sum-screens'], ['fold-hidden', 'sum-hidden']]) {
    assert.match(html, new RegExp(`<details class="card fold" id="${id}"[^>]*>\\s*<summary><h3>[^<]+</h3><span class="fold-sum" id="${sum}"></span></summary>`), id);
  }
  assert.match(app, /foldSummaries\(/);
  for (const id of ['lb-title', 'lb-body', 'lb-numbers', 'lb-hifill', 'lb-hiedge', 'lb-bg', 'lb-panel', 'lb-slice', 'lb-icons', 'lb-screen', 'lb-clear', 'layout-tray', 'lf-file', 'lf-px', 'ld-file', 'lf-msg'])
    assert.match(html, new RegExp(`id="${id}"`), id);
});
