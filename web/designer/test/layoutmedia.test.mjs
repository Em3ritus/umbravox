import test from 'node:test';
import assert from 'node:assert/strict';
import { newLayout, setPiece, setStyle, setBoardStyle, addDecor, toJson, fromJson, draftOther, isEmpty, rectOf, MAX_DECOR } from '../js/layout.js';
import { packMedia } from '../js/layoutmedia.js';
import { pickPiece } from '../js/layout-edit.js';
import { readArt } from '../js/artfmt.js';

const NAMES = ['menu', 'wordmark', 'status.gps', 'status.nodes', 'status.power', 'bridge', 'sd', 'nav.scan', 'nav.domain',
  'nav.codex', 'nav.poi', 'stop', 'count.wifi', 'count.5g', 'count.ble', 'radar', 'spectrum', 'channel', 'badge'];
const DEF = { waveshare: { menu: { x: 0, y: 0, w: 44, h: 44 }, wordmark: { x: 44, y: 0, w: 164, h: 44 }, stop: { x: 384, y: 276, w: 96, h: 44 } },
  cyd: { menu: { x: 0, y: 0, w: 44, h: 44 }, wordmark: { x: 44, y: 0, w: 100, h: 44 }, stop: { x: 256, y: 196, w: 64, h: 44 } } };
const rules = { names: NAMES, defaultRect: (b, n) => DEF[b][n] || { x: 200, y: 100, w: 50, h: 50 } };
const font = (n) => Uint8Array.from([0x55, 0x56, 0x46, 0x4e, 1, 8, 7, 32, 95, 0, 0, 0, ...new Array(380).fill(0).map((_, i) => (i % 4 === 0 ? 4 : 0)), n]);
const pic = (w, h, frames = 1) => ({ w, h, delays: new Array(frames).fill(100), frames: new Array(frames).fill(0).map((_, k) => new Uint8ClampedArray(w * h * 4).map((_, i) => (i % 4 === 3 ? 255 : (i * 13 + k * 50) & 255))) });

test('styles, the style block and decor survive toJson and fromJson', () => {
  let L = newLayout();
  L = setStyle(L, 'waveshare', 'menu', { font: 'spleen-8x16', ink: 'bright', fill: '#102030', scale: 2 }, rules.defaultRect('waveshare', 'menu'));
  L = setBoardStyle(L, 'waveshare', { title: 'spleen-8x16', highlight: { fill: 'witch' } });
  const d = addDecor(L, 'waveshare', 'pic:moon', { x: 10, y: 60, w: 40, h: 30 }); L = d.L;
  assert.equal(d.name, 'decor.0');
  const { json, files, errors } = packMedia(L, NAMES, { fonts: { 'spleen-8x16': font(1) }, pictures: { 'pic:moon': pic(64, 48) } });
  assert.deepEqual(errors, []);
  const j = JSON.parse(json);
  assert.deepEqual(j.waveshare.menu, { rect: [0, 0, 44, 44], font: 'f0', scale: 2, ink: 'bright', fill: '#102030' });
  assert.deepEqual(j.waveshare.style, { title: 'f0', highlight: { fill: 'witch' } });
  assert.deepEqual(j.waveshare['decor.0'], { rect: [10, 60, 40, 30], art: 'a0' });
  assert.deepEqual(files.map((f) => f.name).sort(), ['a0.art', 'f0.fnt']);
  const a0 = readArt(files.find((f) => f.name === 'a0.art').data);
  assert.equal(a0.w, 40); assert.equal(a0.h, 30); assert.equal(a0.transparent, true);
  const back = fromJson(json, NAMES);
  assert.deepEqual(back.dropped, []);
  assert.equal(back.boards.waveshare.menu.style.font, 'file:f0');
  assert.equal(back.boards.waveshare['decor.0'].pic, 'file:a0');
  assert.equal(back.style.waveshare.title, 'file:f0');
});

test('a style block alone is a layout', () => {
  const L = setBoardStyle(newLayout(), 'cyd', { body: 'gohufont-11' });
  assert.equal(isEmpty(L), false);
  const { json } = packMedia(L, NAMES, { fonts: { 'gohufont-11': font(2) }, pictures: {} });
  assert.deepEqual(JSON.parse(json).cyd, { style: { body: 'f0' } });
});

test('one font file per distinct font, at most 8; pictures at the placed size, at most 16', () => {
  let L = newLayout();
  L = setStyle(L, 'waveshare', 'menu', { font: 'k1' }, rules.defaultRect('waveshare', 'menu'));
  L = setStyle(L, 'waveshare', 'stop', { font: 'k1' }, rules.defaultRect('waveshare', 'stop'));
  const fonts = {}; for (let i = 1; i <= 9; i++) fonts['k' + i] = font(i);
  let r = packMedia(L, NAMES, { fonts, pictures: {} });
  assert.equal(r.files.filter((f) => f.name.endsWith('.fnt')).length, 1);
  assert.equal(JSON.parse(r.json).waveshare.stop.font, 'f0');
  L = setBoardStyle(L, 'waveshare', { title: 'k2', body: 'k3', numbers: 'k4' });
  L = setBoardStyle(L, 'cyd', { title: 'k5', body: 'k6', numbers: 'k7' });
  L = setStyle(L, 'cyd', 'menu', { font: 'k8' }, rules.defaultRect('cyd', 'menu'));
  assert.deepEqual(packMedia(L, NAMES, { fonts, pictures: {} }).errors, []);
  L = setStyle(L, 'cyd', 'stop', { font: 'k9' }, rules.defaultRect('cyd', 'stop'));
  r = packMedia(L, NAMES, { fonts, pictures: {} });
  assert.match(r.errors.join(' '), /at most 8 fonts/);
  // the same picture as decor on both boards at two sizes: two files, each exactly its size
  let M = newLayout();
  M = addDecor(M, 'waveshare', 'p', { x: 0, y: 60, w: 50, h: 40 }).L;
  M = addDecor(M, 'cyd', 'p', { x: 0, y: 60, w: 30, h: 24 }).L;
  r = packMedia(M, NAMES, { fonts: {}, pictures: { p: pic(100, 80) } });
  const sizes = r.files.map((f) => readArt(f.data)).map((a) => `${a.w}x${a.h}`).sort();
  assert.deepEqual(sizes, ['30x24', '50x40']);
  let many = newLayout();
  for (let i = 0; i < MAX_DECOR; i++) many = addDecor(many, 'waveshare', 'p', { x: i * 50, y: 60, w: 10 + i, h: 10 }).L;
  for (let i = 0; i < MAX_DECOR; i++) many = addDecor(many, 'cyd', 'p', { x: i * 30, y: 60, w: 10 + i, h: 10 }).L;
  many = setBoardStyle(many, 'cyd', { bg: 'p' });
  assert.match(packMedia(many, NAMES, { fonts: {}, pictures: { p: pic(20, 20) } }).errors.join(' '), /at most 16 pictures/);
  assert.throws(() => addDecor(many, 'cyd', 'p', { x: 0, y: 0, w: 5, h: 5 }), /8 pictures/);
});

test('the style block background is converted at the board size; an animated decor keeps its frames', () => {
  let L = setBoardStyle(newLayout(), 'cyd', { bg: 'sky' });
  L = addDecor(L, 'cyd', 'spin', { x: 4, y: 60, w: 20, h: 20 }).L;
  const r = packMedia(L, NAMES, { fonts: {}, pictures: { sky: pic(400, 300), spin: pic(20, 20, 3) } });
  const arts = r.files.map((f) => readArt(f.data));
  assert.ok(arts.some((a) => a.w === 320 && a.h === 240 && !a.transparent));
  const spin = arts.find((a) => a.frames === 3);
  assert.ok(spin && spin.delays.every((d) => d >= 100), 'CYD frame delays floored to 100 ms');
});

test('drafting the other board carries styles and scales decor', () => {
  let L = setStyle(newLayout(), 'waveshare', 'menu', { ink: 'bright' }, rules.defaultRect('waveshare', 'menu'));
  L = addDecor(L, 'waveshare', 'p', { x: 400, y: 200, w: 60, h: 60 }).L;
  L = setBoardStyle(L, 'waveshare', { title: 'k1' });
  const D = draftOther(L, 'waveshare', 'cyd', rules);
  assert.equal(D.boards.cyd.menu.style.ink, 'bright');
  const r = D.boards.cyd['decor.0'];
  assert.equal(r.pic, 'p');
  assert.ok(r.rect.w === 40 && r.rect.h === 40 && r.rect.x + r.rect.w <= 320 && r.rect.y + r.rect.h <= 240, JSON.stringify(r.rect));
  assert.equal(D.style.cyd.title, 'k1');
});

test('moving a styled piece or a decor keeps its style and picture; decor is picked under every piece', () => {
  let L = setStyle(newLayout(), 'waveshare', 'menu', { fill: '#ff0000' }, rules.defaultRect('waveshare', 'menu'));
  L = setPiece(L, 'waveshare', 'menu', { x: 0, y: 276, w: 44, h: 44 });
  assert.equal(L.boards.waveshare.menu.style.fill, '#ff0000');
  L = addDecor(L, 'waveshare', 'p', { x: 0, y: 270, w: 60, h: 50 }).L;
  L = setPiece(L, 'waveshare', 'decor.0', { x: 5, y: 265, w: 60, h: 50 });
  assert.equal(L.boards.waveshare['decor.0'].pic, 'p');
  const rects = { menu: rectOf(L, 'waveshare', 'menu', rules), 'decor.0': rectOf(L, 'waveshare', 'decor.0', rules) };
  assert.equal(pickPiece(rects, 10, 290), 'menu');
  assert.equal(pickPiece(rects, 55, 300), 'decor.0');
  assert.equal(toJson(newLayout(), NAMES), null);
});
