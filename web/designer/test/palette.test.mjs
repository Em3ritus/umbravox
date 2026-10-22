import test from 'node:test';
import assert from 'node:assert/strict';
import { ROLES, fromAccent, toRgb565, clamp565, contrastRatio, contrastX10 } from '../js/palette.js';
import { runChecks } from '../js/checks.js';

test('ROLES are the 20 device roles in Role order', () => {
  assert.deepEqual(ROLES, ['void', 'panel', 'grid', 'edge', 'edgeLit', 'dim', 'text', 'bright',
    'witch', 'witchHi', 'bone', 'sweep', 'sweepHi', 'live', 'liveHi', 'magenta', 'ice', 'green', 'amber', 'red']);
});

test('toRgb565 packs like the parser', () => {
  assert.equal(toRgb565('#000000'), 0x0000);
  assert.equal(toRgb565('#FFFFFF'), 0xFFFF);
  assert.equal(toRgb565('#ff0000'), 0xF800);
  assert.equal(toRgb565('#00FF00'), 0x07E0);
  assert.equal(toRgb565('#0000FF'), 0x001F);
});

test('clamp565 equals the hardware round trip (high bits replicated)', () => {
  // expected computed by hand: r5=r>>3 -> (r5<<3)|(r5>>2); g6=g>>2 -> (g6<<2)|(g6>>4)
  const table = {
    '#000000': '#000000',
    '#FFFFFF': '#FFFFFF',
    '#09071C': '#080418', // r5=1->0x08, g6=1->0x04, b5=3->0x18
    '#808080': '#848284', // r5=16->0x84, g6=32->0x82, b5=16->0x84
    '#7F7F7F': '#7B7D7B', // r5=15->0x7B, g6=31->0x7D
    '#404040': '#424142',
    '#a84dff': '#AD4DFF', // r5=21->0xAD, g6=19->0x4D, b5=31->0xFF
  };
  for (const [i, o] of Object.entries(table)) assert.equal(clamp565(i), o, i);
});

test('contrast matches ui/contrast.h contrastX10', () => {
  assert.equal(contrastX10(0x0000, 0xFFFF), 210);
  assert.equal(contrastX10(0xFFFF, 0x0000), 210);
  assert.equal(contrastX10(0x1234, 0x1234), 10);
  assert.equal(contrastX10(0x5ACB, 0x0000), 30);
  assert.equal(contrastX10(0x50BF, 0x0000), 29);
  assert.equal(contrastRatio('#000000', '#FFFFFF'), 21);
  assert.equal(contrastRatio('#123456', '#123456'), 1);
});

const HEX = /^#[0-9A-F]{6}$/;
for (const accent of ['#000000', '#FFFFFF', '#FF0000', '#0000FF', '#FFF59D', '#A84DFF']) {
  test('fromAccent ' + accent + ' is complete, readable and error-free', () => {
    const c = fromAccent(accent);
    assert.deepEqual(Object.keys(c), ROLES);
    for (const r of ROLES) assert.match(c[r], HEX, r);
    assert.ok(contrastRatio(c.text, c.panel) >= 4.5, 'text/panel ' + contrastRatio(c.text, c.panel));
    const theme = { name: 'T', header: 'T', dial: 'compact', centrepiece: 'heptagram',
      sweeps: ['radar'], sweep: 'radar', alert: 'ward', colors: c };
    const bad = runChecks(theme, null).filter((r) => r.level === 'error');
    assert.deepEqual(bad, []);
  });
}

test('fromAccent is deterministic and accepts lowercase', () => {
  assert.deepEqual(fromAccent('#a84dff'), fromAccent('#A84DFF'));
  assert.deepEqual(fromAccent('#336699'), fromAccent('#336699'));
});

test('fromAccent differs for different accents', () => {
  assert.notDeepEqual(fromAccent('#FF0000'), fromAccent('#0000FF'));
});

test('helpers throw a clear error for anything but #RRGGBB', () => {
  const bad = ['zzz', '', null, undefined, 123, '#FFF', 'FFFFFF', '#GGGGGG', '#FFFFFFF'];
  for (const b of bad) {
    const re = new RegExp('expected #RRGGBB, got ' + String(b).replace(/[.*+?^${}()|[\]\\]/g, '\\$&'));
    assert.throws(() => fromAccent(b), re, 'fromAccent ' + String(b));
    assert.throws(() => toRgb565(b), re, 'toRgb565 ' + String(b));
    assert.throws(() => clamp565(b), re, 'clamp565 ' + String(b));
  }
  assert.equal(toRgb565('#abcdef'), toRgb565('#ABCDEF'));
  assert.equal(clamp565('#abcdef'), clamp565('#ABCDEF'));
  assert.equal(contrastRatio('#abcdef', '#000000'), contrastRatio('#ABCDEF', '#000000'));
});
