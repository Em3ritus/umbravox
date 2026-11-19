// The speed meter's page wiring (text checks: app.js needs a DOM to run).
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { read } from './_util.mjs';

const app = read('web/designer/js/app.js');

// The text from `start` up to the first line that begins a new top-level statement.
function block(start) {
  const i = app.indexOf(start);
  assert.notEqual(i, -1, start + ' is in app.js');
  const rest = app.slice(i + start.length), end = rest.search(/\n(function |\$\(|const |let |for |fetch\()/);
  return rest.slice(0, end < 0 ? undefined : end);
}

test('5c minor 1: meterNow runs a pending measurement at once', () => {
  const b = block('function meterNow()');
  assert.match(b, /meter\.timer/);
  assert.match(b, /clearTimeout\(meter\.timer\)/);
  assert.match(b, /runMeter\(\)/);
  assert.match(block('function runMeter()'), /meter\.timer = 0/);
});

for (const [name, start] of [['Download', 'function download()'], ['Share', "$('btn-share').addEventListener('click', () => {"], ['the Install view', 'function show(view)']]) {
  test(`5c minor 1: ${name} measures a pending draft before it reads the verdict`, () => {
    const b = block(start), now = b.indexOf('meterNow()');
    assert.notEqual(now, -1, name + ' calls meterNow()');
    const v = Math.max(b.indexOf('meter.verdict'), b.indexOf('installBlock()'));
    assert.ok(v > now, name + ' calls meterNow() before it reads the verdict');
  });
}
