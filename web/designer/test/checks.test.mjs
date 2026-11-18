import test from 'node:test';
import assert from 'node:assert/strict';
import { runChecks } from '../js/checks.js';
import { fromAccent, ROLES } from '../js/palette.js';

const base = () => ({ name: 'TEST', author: 'me', header: 'TEST', dial: 'compact', centrepiece: 'heptagram',
  sweeps: ['radar', 'pulse'], sweep: 'radar', alert: 'ward', colors: { ...fromAccent('#A84DFF') } });
const emb = (w, h, ncol) => ({ w, h, ncol, pal565: Array(ncol).fill(0x1234), px: new Array(w * h).fill(0) });
const find = (rs, level, re) => rs.filter((r) => r.level === level && re.test(r.text));
const errors = (rs) => rs.filter((r) => r.level === 'error');

test('a derived palette passes cleanly', () => {
  assert.deepEqual(errors(runChecks(base(), null)), []);
  assert.equal(find(runChecks(base(), null), 'warn', /./).length, 0);
});

test('an all-black theme does not crash or produce NaN', () => {
  const t = base();
  for (const r of ROLES) t.colors[r] = '#000000';
  const rs = runChecks(t, null);
  assert.ok(rs.length > 0);
  for (const r of rs) assert.ok(!/NaN|Infinity/.test(r.text), r.text);
  assert.ok(find(rs, 'error', /text.*panel|panel.*text/i).length >= 1);
});

test('text == panel is an error with the ratio', () => {
  const t = base(); t.colors.text = t.colors.panel;
  assert.equal(find(runChecks(t, null), 'error', /1\.0:1/).length >= 1, true);
});

test('text/panel warns below 4.5 and errors below 3', () => {
  const t = base(); t.colors.panel = '#000000'; t.colors.text = '#6A6A6A'; // about 3.7:1
  const rs = runChecks(t, null);
  assert.equal(find(rs, 'warn', /text/i).length >= 1, true);
  assert.equal(find(rs, 'error', /text.*panel/i).length, 0);
});

test('low dim contrast warns with the ratio', () => {
  const t = base(); t.colors.dim = t.colors.panel;
  const rs = runChecks(t, null);
  assert.ok(find(rs, 'warn', /dim.*1\.0:1/i).length >= 1);
});

test('missing or invalid colours are errors', () => {
  const t = base(); delete t.colors.grid; t.colors.ice = '#12345';
  const rs = runChecks(t, null);
  assert.ok(find(rs, 'error', /grid/).length >= 1);
  assert.ok(find(rs, 'error', /ice/).length >= 1);
});

test('status colour equal to void gets the device-substitution warning', () => {
  const t = base(); t.colors.amber = t.colors.void;
  const rs = runChecks(t, null);
  assert.ok(find(rs, 'warn', /amber.*(substitut|replace)/i).length >= 1);
  const u = base(); u.colors.red = u.colors.panel;
  assert.ok(find(runChecks(u, null), 'warn', /red.*(substitut|replace)/i).length >= 1);
});

test('witch near the background warns for the alert edge', () => {
  const t = base(); t.colors.witch = t.colors.void;
  assert.ok(find(runChecks(t, null), 'warn', /witch.*(substitut|replace)/i).length >= 1);
});

test('header: 18 ok, 19 error, lowercase is a note, quote and backslash are errors', () => {
  const t = base();
  t.header = 'A'.repeat(18); assert.equal(find(runChecks(t, null), 'error', /header/i).length, 0);
  t.header = 'A'.repeat(19); assert.ok(find(runChecks(t, null), 'error', /header/i).length >= 1);
  t.header = 'Midnight'; const rs = runChecks(t, null);
  assert.equal(find(rs, 'error', /header/i).length, 0);
  assert.ok(find(rs, 'warn', /header.*(capital|uppercase)/i).length >= 1);
  t.header = 'SAY "HI"'; assert.ok(find(runChecks(t, null), 'error', /header.*(quote|backslash)/i).length >= 1);
  t.header = 'A\\B'; assert.ok(find(runChecks(t, null), 'error', /header.*(quote|backslash)/i).length >= 1);
  t.header = 'A{B'; assert.ok(find(runChecks(t, null), 'error', /header/i).length >= 1);
  t.header = 'café'; assert.ok(find(runChecks(t, null), 'error', /header/i).length >= 1);
});

test('emblem size and colour limits', () => {
  const t = base(); t.centrepiece = 'emblem';
  assert.equal(errors(runChecks(t, emb(96, 96, 16))).length, 0);
  assert.ok(find(runChecks(t, emb(97, 97, 16)), 'error', /96/).length >= 1);
  assert.ok(find(runChecks(t, emb(96, 96, 17)), 'error', /16 colours/).length >= 1);
  assert.ok(find(runChecks(t, null), 'error', /emblem/i).length >= 1);
});

test('reserved names, a 21-char name, bad characters, quote', () => {
  const t = base();
  for (const n of ['UMBRAVOX', 'CVLT OF BAPH0MAU5', 'COSMIC MUFFIN']) {
    t.name = n; assert.ok(find(runChecks(t, null), 'error', /reserved|built-in/i).length >= 1, n);
  }
  t.name = 'A'.repeat(20); assert.equal(find(runChecks(t, null), 'error', /name/i).length, 0);
  t.name = 'A'.repeat(21); assert.ok(find(runChecks(t, null), 'error', /name/i).length >= 1);
  t.name = ''; assert.ok(find(runChecks(t, null), 'error', /name/i).length >= 1);
  t.name = 'lower'; assert.ok(find(runChecks(t, null), 'error', /name/i).length >= 1);
  t.name = 'A"B'; assert.ok(find(runChecks(t, null), 'error', /name/i).length >= 1);
});

test('author limits', () => {
  const t = base();
  t.author = 'a'.repeat(24); assert.equal(find(runChecks(t, null), 'error', /author/i).length, 0);
  t.author = 'a'.repeat(25); assert.ok(find(runChecks(t, null), 'error', /author/i).length >= 1);
  t.author = 'a"b'; assert.ok(find(runChecks(t, null), 'error', /author/i).length >= 1);
  t.author = ''; assert.equal(find(runChecks(t, null), 'error', /author/i).length, 0);
});

test('sweeps: at least one, and the start sweep must be offered', () => {
  const t = base();
  t.sweeps = []; assert.ok(find(runChecks(t, null), 'error', /sweep/i).length >= 1);
  t.sweeps = ['pulse']; t.sweep = 'radar';
  assert.ok(find(runChecks(t, null), 'error', /sweep/i).length >= 1);
  t.sweeps = ['pulse', 'radar']; assert.equal(find(runChecks(t, null), 'error', /sweep/i).length, 0);
});

test('pack size is reported against 64 KB', () => {
  const rs = runChecks(base(), null);
  assert.ok(find(rs, 'ok', /KB|bytes/).length >= 1);
});

test('contrast numbers are shown with one decimal', () => {
  const t = base(); t.colors.panel = '#000000'; t.colors.text = '#FFFFFF';
  assert.ok(find(runChecks(t, null), 'ok', /21\.0:1/).length >= 1);
});

test('malformed colours are named error results and never throw', () => {
  const t = base(); t.colors.witch = 'zzz';
  const rs = runChecks(t, null);
  assert.ok(find(rs, 'error', /colour 'witch' is not #RRGGBB: zzz/).length === 1);
  assert.equal(errors(rs).filter((r) => /not #RRGGBB/.test(r.text)).length, 1);
  for (const r of rs) assert.ok(!/NaN|Infinity/.test(r.text), r.text);
  for (const v of [5, null, undefined, '#FFF', {}, '']) {
    const u = base(); u.colors.text = v; u.colors.amber = v; u.colors.dim = v;
    let out;
    assert.doesNotThrow(() => { out = runChecks(u, null); }, String(v));
    assert.ok(find(out, 'error', /colour 'text' is not #RRGGBB/).length === 1);
    for (const r of out) assert.ok(!/NaN|Infinity/.test(r.text), r.text);
  }
});

test('runChecks survives themes with no or empty colours', () => {
  for (const th of [{}, { colors: {} }, { colors: null }, { colors: 7 }, { name: 'X', colors: undefined }, undefined, null]) {
    let out;
    assert.doesNotThrow(() => { out = runChecks(th, null); });
    assert.ok(find(out, 'error', /colour '\w+' is not #RRGGBB/).length >= 1);
    for (const r of out) assert.ok(!/NaN|Infinity/.test(r.text), r.text);
  }
});

test('a name longer than 12 characters warns that the settings screen cuts it', () => {
  const t = base(); t.name = 'ABCDEFGHIJKLM';
  const rs = runChecks(t, null);
  assert.equal(find(rs, 'warn', /first 12 characters.*13 chars/).length, 1);
  assert.equal(errors(rs).length, 0);
  t.name = 'ABCDEFGHIJKL';
  assert.equal(find(runChecks(t, null), 'warn', /first 12/).length, 0);
});

test('art switches: only on or off are accepted', () => {
  const t = base(); t.heptagram = 'off'; t.contacts = 'on';
  assert.deepEqual(errors(runChecks(t, null)), []);
  t.heptagram = 'maybe';
  assert.ok(find(runChecks(t, null), 'error', /Heptagram must be on or off/).length === 1);
  t.heptagram = 'on'; t.contacts = 'nope';
  assert.ok(find(runChecks(t, null), 'error', /Contacts must be on or off/).length === 1);
});

// ---- plan 4: the layout's look ----
import { lookChecks } from '../js/checks.js';
import { newLayout as nl4, setStyle as ss4, addDecor as ad4 } from '../js/layout.js';
test('lookChecks: text on menu, stop, bridge and nav must be 3:1 against its fill', () => {
  const theme = { colors: { void: '#000000', panel: '#202020', text: '#E0E0E0', bright: '#FFFFFF', dim: '#808080' } };
  const r = { x: 0, y: 0, w: 44, h: 44 };
  let L = ss4(nl4(), 'waveshare', 'menu', { ink: 'panel', fill: 'panel' }, r);
  let c = lookChecks(theme, L);
  assert.equal(c.length, 1); assert.equal(c[0].level, 'error'); assert.match(c[0].text, /menu.*3:1/);
  L = ss4(nl4(), 'cyd', 'stop', { ink: '#111111' }, r);
  assert.equal(lookChecks(theme, L)[0].level, 'error');   // dark ink on the theme's panel
  L = ss4(nl4(), 'cyd', 'stop', { ink: 'bright' }, r);
  assert.deepEqual(lookChecks(theme, L), []);
  L = ss4(nl4(), 'cyd', 'wordmark', { ink: 'panel', fill: 'panel' }, r);
  assert.deepEqual(lookChecks(theme, L), [], 'the wordmark is not checked');
});
test('lookChecks: one animated picture per board moves on the device, within an area', () => {
  const pic = (w, h, n) => ({ w, h, delays: new Array(n).fill(100), frames: new Array(n).fill(new Uint8ClampedArray(w * h * 4)) });
  const media = { pictures: { spin: pic(20, 20, 3), still: pic(20, 20, 1) } };
  let L = ad4(nl4(), 'cyd', 'spin', { x: 0, y: 60, w: 50, h: 50 }).L;
  assert.deepEqual(lookChecks({ colors: {} }, L, media), []);
  L = ad4(L, 'cyd', 'spin', { x: 60, y: 60, w: 50, h: 50 }).L;
  L = ad4(L, 'cyd', 'still', { x: 120, y: 60, w: 50, h: 50 }).L;
  let c = lookChecks({ colors: {} }, L, media);
  assert.equal(c.length, 1); assert.equal(c[0].level, 'warn'); assert.match(c[0].text, /decor\.1.*first frame/);
  L = ad4(nl4(), 'cyd', 'spin', { x: 0, y: 60, w: 120, h: 100 }).L;
  c = lookChecks({ colors: {} }, L, media);
  assert.equal(c[0].level, 'warn'); assert.match(c[0].text, /too large to move/);
});
test('lookChecks: STOP\'s fill must read under its red and amber labels, and every must-read fill under BRIGHT', () => {
  const theme = { colors: { void: '#000000', panel: '#202020', text: '#E0E0E0', bright: '#FFFFFF', red: '#FF0000', amber: '#FFA500' } };
  const r = { x: 0, y: 0, w: 44, h: 44 };
  let c = lookChecks(theme, ss4(nl4(), 'waveshare', 'stop', { ink: '#FFFFFF', fill: '#FF0000' }, r));
  assert.equal(c.length, 1); assert.match(c[0].text, /stop.*red/);
  assert.deepEqual(lookChecks(theme, ss4(nl4(), 'waveshare', 'stop', { fill: '#000000' }, r)), []);
  c = lookChecks({ colors: { ...theme.colors, bright: '#202020' } }, ss4(nl4(), 'cyd', 'nav.scan', { fill: 'panel' }, r));
  assert.equal(c.length, 1); assert.match(c[0].text, /nav\.scan.*bright/);
});

// ---- plan 5b: alert card warnings (never errors: the device accepts these and behaves as the text says) ----
// (lookChecks is imported above)
import { newLayout, setCard, rectOf } from '../js/layout.js';
const CNAMES = ['menu', 'stop', 'nav.scan', 'nav.domain', 'nav.codex', 'nav.poi', 'radar', 'count.ble', 'status.gps'];
const CDEF = { waveshare: { menu: { x: 0, y: 0, w: 44, h: 44 }, stop: { x: 384, y: 276, w: 96, h: 44 }, 'nav.scan': { x: 0, y: 276, w: 96, h: 44 },
  'nav.domain': { x: 96, y: 276, w: 96, h: 44 }, 'nav.codex': { x: 192, y: 276, w: 96, h: 44 }, 'nav.poi': { x: 288, y: 276, w: 96, h: 44 }, radar: { x: 188, y: 56, w: 209, h: 209 },
    'count.ble': { x: 0, y: 200, w: 120, h: 60 }, 'status.gps': { x: 300, y: 0, w: 40, h: 44 } },
  cyd: { menu: { x: 0, y: 0, w: 44, h: 44 }, stop: { x: 260, y: 196, w: 60, h: 44 }, 'nav.scan': { x: 0, y: 196, w: 65, h: 44 },
  'nav.domain': { x: 65, y: 196, w: 65, h: 44 }, 'nav.codex': { x: 130, y: 196, w: 65, h: 44 }, 'nav.poi': { x: 195, y: 196, w: 65, h: 44 }, radar: { x: 151, y: 52, w: 115, h: 115 },
    'count.ble': { x: 0, y: 150, w: 100, h: 40 }, 'status.gps': { x: 200, y: 0, w: 30, h: 44 } } };
const CCARD = { waveshare: { low: { x: 124, y: 52, w: 348, h: 216 }, high: { x: 0, y: 0, w: 480, h: 320 } }, cyd: { low: { x: 104, y: 52, w: 208, h: 136 }, high: { x: 0, y: 0, w: 320, h: 240 } } };
const crules = { names: CNAMES, defaultRect: (b, n) => CDEF[b][n], cardDefault: (b, l) => CCARD[b][l === 'med' ? 'low' : l] };
const warns = (L, media = null) => lookChecks({ colors: {} }, L, media, crules).filter((c) => c.level === 'warn').map((c) => c.text);
const fnt = (h) => Uint8Array.from([0x55, 0x56, 0x46, 0x4e, 1, h, h - 1, 32, 95, 0, 0, 0, ...new Array(380).fill(0).map((_, i) => (i % 4 === 0 ? 4 : 0)), 0]);

test('a MED card over STOP warns that a tap there dismisses the alert first', () => {
  const L = setCard(newLayout(), 'waveshare', 'med', { rect: { x: 200, y: 160, w: 280, h: 160 } });
  const w = warns(L);
  assert.ok(w.some((t) => /med alert card on the Waveshare board covers stop: while the alert shows, a tap there dismisses the alert first/i.test(t)), w.join('\n'));
  assert.ok(w.some((t) => /covers nav\.poi/.test(t)));
  assert.ok(!w.some((t) => /covers menu/.test(t)));
});

test('5b minor 1: a built-in card never warns about covering a control (the device hits the chrome first)', () => {
  const over = { ...crules, cardDefault: () => ({ x: 0, y: 0, w: 480, h: 320 }) };   // a built-in LOW/MED card over everything
  const w = lookChecks({ colors: {} }, newLayout(), null, over).filter((c) => c.level === 'warn').map((c) => c.text);
  assert.ok(!w.some((t) => /covers/.test(t)), w.join('\n'));
});

test('5b minor 2: a theme card warns over every control it takes taps from: count tiles, status cells, bridge', () => {
  const L = setCard(newLayout(), 'waveshare', 'low', { rect: { x: 0, y: 0, w: 480, h: 270 } });
  const w = warns(L);
  assert.ok(w.some((t) => /low alert card on the Waveshare board covers count\.ble:/.test(t)), w.join('\n'));
  assert.ok(w.some((t) => /covers status\.gps:/.test(t)), w.join('\n'));
  assert.ok(!w.some((t) => /covers radar/.test(t)), 'the radar is not a control');
});

test('the CYD cover check uses the CYD rects', () => {
  const card = CCARD.cyd.low;
  const w = warns(setCard(newLayout(), 'cyd', 'low', { rect: card }));
  for (const n of ['stop', 'menu', 'nav.scan', 'nav.domain', 'nav.codex', 'nav.poi', 'count.ble', 'status.gps']) {
    const r = rectOf(newLayout(), 'cyd', n, crules);
    const hit = r.x < card.x + card.w && card.x < r.x + r.w && r.y < card.y + card.h && card.y < r.y + r.h;
    assert.equal(w.some((t) => t.includes('CYD board covers ' + n + ':')), hit, n);
  }
});

test('a HIGH card over everything, and a card touching no control, give no cover warning', () => {
  let L = setCard(newLayout(), 'waveshare', 'high', { rect: CCARD.waveshare.high });
  L = setCard(L, 'waveshare', 'low', { rect: { x: 124, y: 52, w: 248, h: 150 } });
  L = setCard(L, 'waveshare', 'med', { rect: { x: 124, y: 52, w: 248, h: 150 } });
  assert.ok(!warns(L).some((t) => /Waveshare board covers/.test(t)), warns(L).join('\n'));
});

test('a card font under 7 px warns; 7 px does not', () => {
  const L = setCard(newLayout(), 'cyd', 'low', { rect: CCARD.cyd.low, font: 'tiny' });
  assert.ok(warns(L, { fonts: { tiny: fnt(6) }, pictures: {} }).some((t) => /low alert card font is 6 px; the device needs 7 px and uses its built-in font/.test(t)));
  assert.ok(!warns(L, { fonts: { tiny: fnt(7) }, pictures: {} }).some((t) => /card font/.test(t)));
});

test('an opened card picture that does not fit warns; an uploaded one is refitted and does not', () => {
  const pic = (w, h) => ({ w, h, frames: [new Uint8ClampedArray(w * h * 4)], delays: [0] });
  const L = setCard(newLayout(), 'cyd', 'high', { rect: { x: 0, y: 0, w: 200, h: 120 }, pic: 'file:a3' });
  assert.ok(warns(L, { fonts: {}, pictures: { 'file:a3': pic(120, 100) } }).some((t) => /high alert card picture does not fit; the device plays the rite instead/.test(t)));
  assert.ok(!warns(L, { fonts: {}, pictures: { 'file:a3': pic(100, 90) } }).some((t) => /picture does not fit/.test(t)));
  const U = setCard(newLayout(), 'cyd', 'high', { rect: { x: 0, y: 0, w: 200, h: 120 }, pic: 'pic:1:big.png' });
  assert.ok(!warns(U, { fonts: {}, pictures: { 'pic:1:big.png': pic(400, 400) } }).some((t) => /picture does not fit/.test(t)));
});
