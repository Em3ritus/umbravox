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
  assert.match(app, /packMedia\(L, rules\(\)\.names, state\.media\)/);   // layout.json with its fonts and pictures (plan 4)
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
  assert.match(app, /loadDraft\(currentTheme\(\), state\.emblem, state\.clip, layoutText\(\), previewMedia\(\)\)/);
  assert.match(app, /checkLayout\(/);
});

test('the Start page lists every starting layout', () => {
  assert.match(app, /PRESETS\.forEach|for \(const p of PRESETS\)/);
  assert.ok(PRESETS.length >= 5);
});

test('review fixes: the Install step, Download and the opened-pack path all respect the layout verdict', () => {
  assert.match(app, /installGate\(currentTheme\(\), state\.emblem, state\.clip, state\.sounds, layoutText\(\), preview \? \(t\) => preview\.checkLayout\(t\) : null\)/);
  assert.match(app, /function download\(\) \{\n  const m = \$\('action-msg'\);\n  if \(checksNow\.some\(\(c\) => c\.level === 'error'\)\)/);
  assert.match(app, /function adoptLayout\(text\) \{\n  state\.layout = newLayout\(\);/, 'an opened pack never keeps the Start page preset');
  assert.match(app, /The device would refuse this layout/, 'the Layout step says why');
  assert.match(app, /withDrafts\(state\.layout, rules\(\), state\.lvisited\)/);
});

test('plan 5b: the alert mode, the card panel and the effect controls are on the page and wired', () => {
  for (const id of ['layout-modes', 'layout-mode-scan', 'layout-mode-low', 'layout-mode-med', 'layout-mode-high', 'layout-play',
    'layout-card', 'lc-font', 'lc-scale', 'lc-pic', 'lc-builtin', 'lc-warn', 'ls-effect', 'ls-speed', 'ls-fx-note'])
    assert.match(html, new RegExp(`id="${id}"`), id);
  for (const v of ['none', 'flash', 'pulse', 'shake']) assert.match(html, new RegExp(`<select id="ls-effect"[^]*?<option value="${v}"`), v);
  for (const v of ['slow', 'medium', 'fast']) assert.match(html, new RegExp(`<select id="ls-speed"[^]*?<option value="${v}"`), v);
  assert.match(app, /from '\.\/layout-alert\.js'/);
  assert.match(app, /setCard\(/);
  assert.match(app, /effectAllowed\(/);
  assert.match(app, /alertFrame\(/);
  assert.match(app, /nextMode\(/);
  assert.match(app, /renderDraft\('alert'/);
});
