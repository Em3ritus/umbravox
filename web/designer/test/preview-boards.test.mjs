// The preview module's board selection: the CYD (160x120 half-scale preview of 320x240) and the Waveshare 3.5" (480x320 engine).
import { test, before } from 'node:test';
import assert from 'node:assert/strict';
import path from 'node:path';
import { spawnSync } from 'node:child_process';
import { pathToFileURL } from 'node:url';
import { ROOT } from './_util.mjs';
import { createPreview, BOARDS, W, H } from '../js/preview.js';
import { makeTemplate } from '../js/model.js';

let preview, M;
before(async () => {
  const r = spawnSync('bash', [path.join(ROOT, 'tools/build-designer-wasm.sh')], { encoding: 'utf8' });
  assert.equal(r.status, 0, r.stdout + r.stderr);
  M = await (await import(pathToFileURL(path.join(ROOT, 'web/designer/wasm/uv_preview.js')).href)).default();
  preview = createPreview(M);
});

test('BOARDS describes both boards with their own sizes and screens', () => {
  assert.deepEqual([BOARDS.cyd.w, BOARDS.cyd.h], [160, 120]);
  assert.deepEqual([BOARDS.waveshare.w, BOARDS.waveshare.h], [480, 320]);
  assert.equal(W, BOARDS.cyd.w); assert.equal(H, BOARDS.cyd.h);       // the old exports stay the CYD
  assert.deepEqual(BOARDS.cyd.screens, ['scan', 'mesh', 'alert', 'menu']);
  assert.deepEqual(BOARDS.waveshare.screens, ['scan', 'mesh', 'alert', 'menu', 'aspect']);
  assert.ok(BOARDS.waveshare.name.length && BOARDS.cyd.name.length);
});

test('the preview offers the Waveshare when the wasm has it, and the CYD stays the default', () => {
  assert.deepEqual(preview.boards(), ['cyd', 'waveshare']);
  assert.equal(preview.renderBuiltin(0, 'scan', 24).length, W * H * 4, 'no options: the CYD, as before');
  assert.equal(createPreview({ _uv_render: M._uv_render, _malloc: M._malloc, _free: M._free, HEAPU8: M.HEAPU8 }).boards().join(), 'cyd', 'an older wasm without uv_render_ws offers only the CYD');
});

test('a built-in renders at the Waveshare size, and differs from the CYD frame', () => {
  const ws = preview.renderBuiltin(0, 'scan', 24, { board: 'waveshare' });
  assert.equal(ws.length, 480 * 320 * 4);
  const cyd = preview.renderBuiltin(0, 'scan', 24);
  assert.notEqual(Buffer.from(ws.subarray(0, 4000)).toString('hex'), Buffer.from(cyd.subarray(0, 4000)).toString('hex'));
});

test('a draft renders on the Waveshare, every Waveshare screen draws, and the alert confidence matters', () => {
  const t = makeTemplate('cosmic'); const r = preview.loadDraft(t.theme, t.emblem);
  assert.ok(r.ok, r.error);
  const seen = new Set();
  for (const screen of BOARDS.waveshare.screens) {
    const px = preview.renderDraft(screen, 60, { board: 'waveshare' });
    assert.equal(px.length, 480 * 320 * 4, screen);
    seen.add(Buffer.from(px).toString('base64'));
  }
  assert.equal(seen.size, BOARDS.waveshare.screens.length, 'each screen is its own picture');
  const low = Buffer.from(preview.renderDraft('alert', 60, { board: 'waveshare', alertConf: 'low' }));
  const high = Buffer.from(preview.renderDraft('alert', 60, { board: 'waveshare', alertConf: 'high' }));
  assert.notEqual(low.compare(high), 0, 'LOW takes the hero, HIGH the whole screen');
});

test('bad requests throw instead of drawing the wrong thing', () => {
  { const t = makeTemplate('cosmic'); preview.loadDraft(t.theme, t.emblem); }
  assert.throws(() => preview.renderDraft('scan', 0, { board: 'toaster' }), /board/);
  assert.throws(() => preview.renderDraft('aspect', 0), /aspect/, 'the CYD has no engine ASPECT screen in the preview');
  assert.throws(() => preview.renderDraft('scan', 0, { board: 'waveshare', alertConf: 'purple' }), /alertConf/);
});

test('the Dial preset changes the CYD picture but not the Waveshare one (so the page says so)', () => {
  const t = makeTemplate('cosmic');
  const draw = (dial, board) => { preview.loadDraft({ ...t.theme, dial }, t.emblem); return Buffer.from(preview.renderDraft('scan', 24, { board })); };
  assert.notEqual(draw('compact', 'cyd').compare(draw('large', 'cyd')), 0, 'the CYD honours the dial preset');
  assert.equal(draw('compact', 'waveshare').compare(draw('large', 'waveshare')), 0, 'the Waveshare engine lays the dial out itself');
});

// ---- the radar clip (part of the pack) -----------------------------------------
import { buildClip, CLIP_SIZE, clipFile } from '../js/radarvideo.js';
const clip = n => buildClip(Array.from({ length: n }, (_, f) => Uint8Array.from({ length: CLIP_SIZE * CLIP_SIZE }, (_, i) => (i + f * 7) % 16)));

test('a draft carrying a radar clip shows it on the Waveshare, never on the CYD, and loses it when edited out', () => {
  const t = makeTemplate('cosmic');
  assert.equal(preview.loadDraft(t.theme, t.emblem).ok, true);
  const plainWs = preview.renderDraft('scan', 7, { board: 'waveshare' });
  const plainCyd = preview.renderDraft('scan', 7);
  assert.equal(preview.loadDraft(t.theme, t.emblem, clip(4)).ok, true);
  assert.notDeepEqual(preview.renderDraft('scan', 7, { board: 'waveshare' }), plainWs, 'the clip shows');
  assert.deepEqual(preview.renderDraft('scan', 7), plainCyd, 'the CYD preview is untouched');
  assert.equal(preview.loadDraft(t.theme, t.emblem).ok, true);
  assert.deepEqual(preview.renderDraft('scan', 7, { board: 'waveshare' }), plainWs, 'a draft without the clip has none');
});

test('a refused clip says why and leaves the previous draft on screen', () => {
  const t = makeTemplate('cosmic');
  assert.equal(preview.loadDraft(t.theme, t.emblem, clip(3)).ok, true);
  const shown = preview.renderDraft('scan', 5, { board: 'waveshare' });
  const r = preview.loadDraft(t.theme, t.emblem, { ...clip(2), w: 64 });
  assert.equal(r.ok, false); assert.match(r.error, /102/);
  assert.deepEqual(preview.renderDraft('scan', 5, { board: 'waveshare' }), shown);
  preview.loadDraft(t.theme, t.emblem);
  assert.ok(clipFile(clip(1)).length > 12);
});

test('the Waveshare Scan tab never flips to a demo alert: that belongs to the Alert tab', () => {
  // The CYD demo raises an alert every other 60-frame block; on the 480x320 engine that is a takeover of the whole screen,
  // nav bar included. The nav bar (SCAN, DOMAIN, CODEX...) is static, so it must look the same in every frame.
  const nav = (img) => { const rows = []; for (let y = 285; y < 315; y++) rows.push(Buffer.from(img.slice(4 * (y * 480 + 8), 4 * (y * 480 + 300))).toString('base64')); return rows.join(); };
  const ref = nav(preview.renderBuiltin(2, 'scan', 10, { board: 'waveshare' }));
  for (const f of [70, 130, 250, 370]) assert.equal(nav(preview.renderBuiltin(2, 'scan', f, { board: 'waveshare' })), ref, `frame ${f}: a demo alert covers the nav bar`);
});
