// The page is wired to the Layout step (text checks: app.js needs a DOM to run; the logic it calls is tested in
// layout.test.mjs, layout-edit.test.mjs and wasm-parity.test.mjs).
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { read } from './_util.mjs';
import { PRESETS } from '../js/layout-presets.js';

const app = read('web/designer/js/app.js'), html = read('web/designer/index.html');

test('the Layout view and its step exist', () => {
  assert.match(html, /<section id="view-layout" class="view" data-view="layout" hidden>/);
  assert.match(html, /class="step" data-view="layout"/);
  for (const id of ['layout-board-waveshare', 'layout-board-cyd', 'layout-canvas', 'layout-tray', 'layout-x', 'layout-y', 'layout-w', 'layout-h', 'layout-hide', 'layout-piece', 'layout-msg', 'layout-presets'])
    assert.match(html, new RegExp(`id="${id}"`), id);
});

test('the CYD tab says the default CYD release ignores layouts', () => {
  assert.match(html, /id="layout-cyd-note"[^>]*>[^<]*default CYD release[^<]*ignores/);
});

test('app.js uses the layout model, the pointer maths and the presets', () => {
  assert.match(app, /from '\.\/layout\.js'/);
  assert.match(app, /from '\.\/layout-edit\.js'/);
  assert.match(app, /from '\.\/layout-presets\.js'/);
  assert.match(app, /draftOther\(/);
  assert.match(app, /toJson\(state\.layout/);
});

test('the preview takes pointer and keyboard input', () => {
  for (const ev of ['pointerdown', 'pointermove', 'pointerup', 'keydown'])
    assert.match(app, new RegExp(`addEventListener\\('${ev}'`), ev);
  assert.match(app, /toDevice\(/);
  assert.match(app, /pickPiece\(/);
  assert.match(app, /handleAt\(/);
  assert.match(app, /dragTo\(/);
  assert.match(app, /nudge\(/);
});

test('the layout draws on the engine preview for the active board', () => {
  assert.match(app, /'cyd-engine'/);
  assert.match(app, /state\.view === 'layout'/);
});

test('the loaded draft carries the layout, and a refused layout is an error in the checks', () => {
  assert.match(app, /loadDraft\(currentTheme\(\), state\.emblem, state\.clip, layoutText\(\)\)/);
  assert.match(app, /checkLayout\(/);
});

test('the Start page lists every starting layout', () => {
  assert.match(app, /PRESETS\.forEach|for \(const p of PRESETS\)/);
  assert.ok(PRESETS.length >= 5);
});
