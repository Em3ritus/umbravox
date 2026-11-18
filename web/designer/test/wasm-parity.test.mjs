import { BUILTIN_COUNT } from './_util.mjs';
// The designer's live preview is the device's own ui/ code compiled to
// WebAssembly (web/designer/wasm/bridge.cpp, built by
// tools/build-designer-wasm.sh). This proves it: every frame the wasm draws
// must be byte-identical to the PNG the native simulator writes for the same
// arguments. A difference means the preview is lying about the device.
//
// No silent skip: the build script runs first (it is incremental, so this is
// a timestamp check when nothing changed) and the suite fails if it cannot
// produce the module -- including when emscripten is not installed.
import { test, before } from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import zlib from 'node:zlib';
import { spawnSync } from 'node:child_process';
import { pathToFileURL } from 'node:url';
import { ROOT, SIM } from './_util.mjs';

const WASM_DIR = path.join(ROOT, 'web/designer/wasm');
const WASM_JS = path.join(WASM_DIR, 'uv_preview.js');
const WASM_BIN = path.join(WASM_DIR, 'uv_preview.wasm');
const BUILD = path.join(ROOT, 'tools/build-designer-wasm.sh');
const W = 160, H = 120;

// Built-in theme ids (ui/palette.h ThemeId) and the screens uv_render takes.
const BUILTINS = { UMBRAVOX: 0, BAPH0MAU5: 1, 'COSMIC MUFFIN': 2 };
const PACKS = ['sim/packs/umbravox-clone', 'sim/packs/cosmic-clone'];
const SCREEN = { scan: 0, mesh: 1, alert: 2, menu: 3 };
const SIM_SCREEN = { scan: 'scan', mesh: 'mesh', alert: 'scan', menu: 'grimoire' };
const SWEEP_COUNT = 11;        // ui::SW_COUNT
const ALERT_STYLES = 4;        // ui::ALERT_STYLE_COUNT

let M;          // the instantiated module
let tmp;

function run(cmd, args) {
  const r = spawnSync(cmd, args, { encoding: 'utf8', cwd: ROOT });
  if (r.status !== 0) throw new Error(`${cmd} ${args.join(' ')} failed (${r.status}):\n${r.stdout}${r.stderr}${r.error || ''}`);
  return r;
}

before(async () => {
  // Fresh simulator (make is incremental): a stale binary would compare the
  // wasm against code that no longer exists.
  run('make', ['-s', '-C', path.join(ROOT, 'sim'), 'umbravox', 'ws_preview_png']);
  run('bash', [BUILD]);
  assert.ok(fs.existsSync(WASM_JS) && fs.existsSync(WASM_BIN),
    'the build script ran but did not produce uv_preview.js / uv_preview.wasm');
  const factory = (await import(pathToFileURL(WASM_JS).href)).default;
  M = await factory();
  tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'uvx-wasm-'));
});

// ---- PNG decode (the sim's writer: 8-bit truecolour, stored deflate) -------

function decodePng(buf) {
  const sig = [0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a];
  sig.forEach((b, i) => assert.equal(buf[i], b, 'not a PNG'));
  let p = 8, w = 0, h = 0;
  const idat = [];
  while (p < buf.length) {
    const len = buf.readUInt32BE(p);
    const type = buf.toString('latin1', p + 4, p + 8);
    const data = buf.subarray(p + 8, p + 8 + len);
    if (type === 'IHDR') {
      w = data.readUInt32BE(0); h = data.readUInt32BE(4);
      assert.equal(data[8], 8, 'PNG bit depth must be 8');
      assert.equal(data[9], 2, 'PNG colour type must be truecolour');
      assert.equal(data[12], 0, 'interlaced PNG not supported');
    } else if (type === 'IDAT') idat.push(data);
    else if (type === 'IEND') break;
    p += 12 + len;
  }
  const raw = zlib.inflateSync(Buffer.concat(idat));
  const stride = 1 + w * 3;
  assert.equal(raw.length, h * stride, 'PNG data length');
  const rgba = new Uint8Array(w * h * 4);
  for (let y = 0; y < h; y++) {
    const f = raw[y * stride];
    if (f !== 0) throw new Error(`PNG row ${y} uses filter ${f}; only filter 0 (none) is handled, which is what sim/png_writer.h writes`);
    for (let x = 0; x < w; x++) {
      const s = y * stride + 1 + x * 3, d = (y * w + x) * 4;
      rgba[d] = raw[s]; rgba[d + 1] = raw[s + 1]; rgba[d + 2] = raw[s + 2]; rgba[d + 3] = 255;
    }
  }
  return { w, h, rgba };
}

// ---- the two renderers -----------------------------------------------------

let simN = 0;
function simRender({ theme, pack, screen, frame, sweep, alert }) {
  const out = path.join(tmp, `s${simN++}.png`);
  const args = [out, '--scale', '1', '--frame', String(frame), '--screen', SIM_SCREEN[screen]];
  if (pack) args.push('--pack', path.join(ROOT, pack)); else args.push('--theme', String(theme));
  if (sweep >= 0) args.push('--sweep', String(sweep));
  if (screen === 'alert') args.push('--alert', String(alert));
  run(SIM, args);
  const png = decodePng(fs.readFileSync(out));
  assert.equal(png.w, W); assert.equal(png.h, H);
  return png.rgba;
}

function wasmRender(theme, screen, frame, sweep, alert) {
  const n = W * H * 4;
  const ptr = M._malloc(n);
  try {
    M._uv_render(theme, SCREEN[screen], frame, sweep, screen === 'alert' ? alert : -1, ptr);
    return M.HEAPU8.slice(ptr, ptr + n);
  } finally { M._free(ptr); }
}

function withBytes(bytes, fn) {
  const len = bytes ? bytes.length : 0;
  const ptr = len ? M._malloc(len) : 0;
  if (len) M.HEAPU8.set(bytes, ptr);
  try { return fn(ptr, len); } finally { if (len) M._free(ptr); }
}

function loadPack(json, emblem) {
  return withBytes(json, (jp, jl) => withBytes(emblem, (ep, el) => M._uv_load_pack(jp, jl, ep, el, 0, 0)));
}

function loadFixture(dir) {
  const json = fs.readFileSync(path.join(ROOT, dir, 'theme.json'));
  const ep = path.join(ROOT, dir, 'emblem.bin');
  const emblem = fs.existsSync(ep) ? fs.readFileSync(ep) : null;
  return loadPack(json, emblem);
}

const err = () => M.UTF8ToString(M._uv_error());

// Byte-compare; on a mismatch, say how much and where rather than dumping.
function same(name, a, b) {
  if (Buffer.compare(Buffer.from(a), Buffer.from(b)) === 0) return;
  let px = 0, maxd = 0, first = -1;
  for (let i = 0; i < a.length; i += 4) {
    let d = 0;
    for (let k = 0; k < 4; k++) d = Math.max(d, Math.abs(a[i + k] - b[i + k]));
    if (d) { px++; maxd = Math.max(maxd, d); if (first < 0) first = i / 4; }
  }
  assert.fail(`${name}: ${px} of ${W * H} pixels differ (max channel delta ${maxd}; first at x=${first % W} y=${Math.floor(first / W)})`);
}

// Every case for one theme: scan/mesh/menu at 0, 24, 600; alert at 5, 20, 45
// (ages 100, 400, 900 ms) in style 0.
function cases() {
  const c = [];
  for (const screen of ['scan', 'mesh', 'menu'])
    for (const frame of [0, 24, 600]) c.push({ screen, frame, sweep: -1, alert: -1 });
  for (const frame of [5, 20, 45]) c.push({ screen: 'alert', frame, sweep: -1, alert: 0 });
  return c;
}

function checkTheme(label, theme, pack) {
  for (const k of cases()) {
    const name = `${label} ${k.screen} frame ${k.frame}`;
    same(name, wasmRender(theme, k.screen, k.frame, k.sweep, k.alert), simRender({ theme, pack, ...k }));
  }
}

// ---- parity ----------------------------------------------------------------

test('the module reports the canvas size and the built-in themes', () => {
  assert.equal(M._uv_width(), W);
  assert.equal(M._uv_height(), H);
  M._uv_clear_packs();
  assert.equal(M._uv_theme_count(), BUILTIN_COUNT);
});

for (const [label, id] of Object.entries(BUILTINS)) {
  test(`built-in ${label}: wasm equals the simulator byte for byte`, () => {
    M._uv_clear_packs();
    checkTheme(label, id, null);
  });
}

for (const dir of PACKS) {
  test(`pack ${dir}: loaded through uv_load_pack, wasm equals the simulator`, () => {
    const id = loadFixture(dir);
    assert.equal(id, BUILTIN_COUNT, `uv_load_pack: ${id < 0 ? err() : ''}`);
    checkTheme(dir, id, dir);
  });
}

test('COSMIC MUFFIN: every sweep and every alert style match too', () => {
  M._uv_clear_packs();
  const t = BUILTINS['COSMIC MUFFIN'];
  for (let sw = 0; sw < SWEEP_COUNT; sw++)
    same(`COSMIC scan sweep ${sw}`, wasmRender(t, 'scan', 24, sw, -1),
      simRender({ theme: t, screen: 'scan', frame: 24, sweep: sw, alert: -1 }));
  for (let al = 0; al < ALERT_STYLES; al++)
    same(`COSMIC alert style ${al}`, wasmRender(t, 'alert', 20, -1, al),
      simRender({ theme: t, screen: 'alert', frame: 20, sweep: -1, alert: al }));
});

test('the comparison is not vacuous: themes, frames and screens draw different pixels', () => {
  M._uv_clear_packs();
  const key = (a) => Buffer.from(a).toString('base64');
  const seen = new Set();
  for (const t of Object.values(BUILTINS))
    for (const [screen, frame] of [['scan', 0], ['scan', 24], ['mesh', 0], ['menu', 0], ['alert', 20]])
      seen.add(key(wasmRender(t, screen, frame, -1, 0)));
  assert.equal(seen.size, 15, 'every theme/screen/frame pair should render a distinct image');
  const a = wasmRender(0, 'scan', 0, -1, -1);
  assert.ok(new Set(Array.from({ length: W * H }, (_, i) => a[4 * i] << 16 | a[4 * i + 1] << 8 | a[4 * i + 2])).size > 8,
    'the scan screen should hold more than a handful of colours');
});

test('loading a draft replaces the previous one', () => {
  M._uv_clear_packs();
  assert.equal(loadFixture(PACKS[0]), BUILTIN_COUNT);
  assert.equal(loadFixture(PACKS[1]), BUILTIN_COUNT);
  assert.equal(M._uv_theme_count(), BUILTIN_COUNT + 1);
});

// ---- hostile input -----------------------------------------------------------

test('uv_load_pack rejects hostile input cleanly and keeps the current draft', () => {
  M._uv_clear_packs();
  assert.equal(loadFixture('sim/packs/cosmic-clone'), BUILTIN_COUNT);
  const before = wasmRender(3, 'scan', 24, -1, -1);
  const count = M._uv_theme_count();

  const good = fs.readFileSync(path.join(ROOT, 'sim/packs/cosmic-clone/theme.json'), 'utf8');
  const enc = (s) => Buffer.from(s, 'utf8');
  const reserved = JSON.parse(good); reserved.name = 'UMBRAVOX';
  const hostile = {
    'empty input': () => loadPack(null, null),
    'null json with a length': () => M._uv_load_pack(0, 100, 0, 0, 0, 0),
    'negative length': () => withBytes(enc(good), (p) => M._uv_load_pack(p, -5, 0, 0, 0, 0)),
    'truncated JSON': () => loadPack(enc(good.slice(0, good.length >> 1)), null),
    'json over the size cap': () => loadPack(enc(good + ' '.repeat(4097)), null),
    'emblem centrepiece with no emblem': () => loadPack(enc(good), null),
    'emblem centrepiece with a 5-byte emblem': () => loadPack(enc(good), Buffer.from([0x55, 0x56, 0x45, 1, 72])),
    'emblem over the size cap': () => loadPack(enc(good), Buffer.alloc(9729, 1)),
    'reserved name': () => loadPack(enc(JSON.stringify(reserved)), fs.readFileSync(path.join(ROOT, 'sim/packs/cosmic-clone/emblem.bin'))),
  };
  for (const [what, call] of Object.entries(hostile)) {
    const r = call();
    assert.ok(r < 0, `${what}: expected a negative code, got ${r}`);
    assert.ok(err().length > 0, `${what}: uv_error() is empty`);
    assert.equal(M._uv_theme_count(), count, `${what}: theme count changed`);
  }
  // The failed loads must not have clobbered the live draft's emblem buffer.
  same('draft after hostile loads', wasmRender(3, 'scan', 24, -1, -1), before);
  assert.equal(loadFixture(PACKS[0]), BUILTIN_COUNT, 'a good pack still loads after failures');
});

// ---- the Waveshare 3.5" engine preview (480x320) ------------------------------
// uv_render_ws draws ui/preview_ws.h; sim/ws_preview_png is the same call on the host.

const WS_W = 480, WS_H = 320;
const WS_SCREEN = { scan: 0, mesh: 1, alert: 2, menu: 3, aspect: 4 };
const CONF = { low: 0, med: 1, high: 2 };
const WS_SIM = path.join(ROOT, 'sim/ws_preview_png');

function wsSim({ theme, pack, screen, frame, sweep, alert, conf }) {
  const out = path.join(tmp, `w${simN++}.rgba`);
  const args = [String(theme), String(WS_SCREEN[screen]), String(frame), String(sweep), String(alert), String(conf), out];
  if (pack) args.push('--pack', path.join(ROOT, pack));
  run(WS_SIM, args);
  const b = fs.readFileSync(out);
  assert.equal(b.length, WS_W * WS_H * 4);
  return new Uint8Array(b);
}

function wsWasm(theme, screen, frame, sweep, alert, conf) {
  const n = WS_W * WS_H * 4;
  const ptr = M._malloc(n);
  try {
    M._uv_render_ws(theme, WS_SCREEN[screen], frame, sweep, alert, conf, ptr);
    return M.HEAPU8.slice(ptr, ptr + n);
  } finally { M._free(ptr); }
}

function wsSame(name, a, b) {
  if (Buffer.compare(Buffer.from(a), Buffer.from(b)) === 0) return;
  let px = 0;
  for (let i = 0; i < a.length; i += 4) if (a[i] !== b[i] || a[i + 1] !== b[i + 1] || a[i + 2] !== b[i + 2]) px++;
  assert.fail(`${name}: ${px} of ${WS_W * WS_H} pixels differ`);
}

function wsCases() {
  const c = [];
  for (const screen of ['scan', 'mesh', 'menu', 'aspect'])
    for (const frame of [0, 24, 600]) c.push({ screen, frame, sweep: -1, alert: -1, conf: 2 });
  for (const conf of ['low', 'med', 'high'])
    for (const frame of [5, 45, 160]) c.push({ screen: 'alert', frame, sweep: -1, alert: -1, conf: CONF[conf] });
  return c;
}

function wsCheckTheme(label, theme, pack) {
  for (const k of wsCases())
    wsSame(`WS ${label} ${k.screen} frame ${k.frame} conf ${k.conf}`, wsWasm(theme, k.screen, k.frame, k.sweep, k.alert, k.conf), wsSim({ theme, pack, ...k }));
}

test('the module reports the Waveshare canvas size', () => {
  assert.equal(M._uv_ws_width(), WS_W);
  assert.equal(M._uv_ws_height(), WS_H);
});

for (const [label, id] of Object.entries(BUILTINS)) {
  test(`Waveshare built-in ${label}: wasm equals the host engine byte for byte`, () => {
    M._uv_clear_packs();
    wsCheckTheme(label, id, null);
  });
}

for (const dir of PACKS) {
  test(`Waveshare pack ${dir}: wasm equals the host engine`, () => {
    const id = loadFixture(dir);
    assert.equal(id, BUILTIN_COUNT, `uv_load_pack: ${id < 0 ? err() : ''}`);
    wsCheckTheme(dir, id, dir);
  });
}

test('Waveshare COSMIC MUFFIN: every sweep and every alert style match', () => {
  M._uv_clear_packs();
  const t = BUILTINS['COSMIC MUFFIN'];
  for (let sw = 0; sw < SWEEP_COUNT; sw++)
    wsSame(`WS COSMIC sweep ${sw}`, wsWasm(t, 'scan', 24, sw, -1, 2), wsSim({ theme: t, screen: 'scan', frame: 24, sweep: sw, alert: -1, conf: 2 }));
  for (let al = 0; al < ALERT_STYLES; al++)
    for (const conf of [0, 2])
      wsSame(`WS COSMIC alert style ${al} conf ${conf}`, wsWasm(t, 'alert', 60, -1, al, conf), wsSim({ theme: t, screen: 'alert', frame: 60, sweep: -1, alert: al, conf }));
});

test('the Waveshare comparison is not vacuous: screens, themes and frames differ, and it is not the CYD image', () => {
  M._uv_clear_packs();
  const seen = new Set();
  for (const t of Object.values(BUILTINS))
    for (const [screen, frame] of [['scan', 0], ['scan', 24], ['mesh', 0], ['menu', 0], ['aspect', 0], ['alert', 60]])
      seen.add(Buffer.from(wsWasm(t, screen, frame, -1, -1, 2)).toString('base64'));
  assert.equal(seen.size, 18, 'every theme/screen/frame pair should render a distinct image');
  const a = wsWasm(0, 'scan', 0, -1, -1, 2);
  assert.ok(new Set(Array.from({ length: WS_W * WS_H }, (_, i) => a[4 * i] << 16 | a[4 * i + 1] << 8 | a[4 * i + 2])).size > 8);
});

// ---- the radar clip in a pack (radar.vid) ----------------------------------------
// A pack carrying radar.vid, loaded through uv_load_pack, must draw exactly what the host loader draws for the same folder.
import { buildClip, clipFile } from '../js/radarvideo.js';

function syntheticClip(frames = 7) {
  const fs_ = [];
  for (let f = 0; f < frames; f++) {
    const l = new Uint8Array(102 * 102);
    for (let y = 0; y < 102; y++) for (let x = 0; x < 102; x++)
      l[y * 102 + x] = (Math.abs(x - (10 + 12 * f)) < 3 || ((x + y + f * 5) % 23 === 0)) ? 15 : (f * 2 + (x >> 4)) % 9;
    fs_.push(l);
  }
  return buildClip(fs_);
}
const PACK_SRC = 'sim/packs/umbravox-clone';
function packWithClip(bytes) {
  const dir = path.join(tmp, `clip-pack-${simN++}`, 'vidpack');
  fs.mkdirSync(dir, { recursive: true });
  for (const f of fs.readdirSync(path.join(ROOT, PACK_SRC))) fs.copyFileSync(path.join(ROOT, PACK_SRC, f), path.join(dir, f));
  if (bytes) fs.writeFileSync(path.join(dir, 'radar.vid'), bytes);
  return dir;
}
function loadWithClip(dir, clip) {
  const json = fs.readFileSync(path.join(dir, 'theme.json'));
  const emb = fs.existsSync(path.join(dir, 'emblem.bin')) ? fs.readFileSync(path.join(dir, 'emblem.bin')) : null;
  return withBytes(json, (jp, jl) => withBytes(emb, (ep, el) => withBytes(clip, (cp, cl) => M._uv_load_pack(jp, jl, ep, el, cp, cl))));
}
function wsSimDir(dir, k) {
  const out = path.join(tmp, `v${simN++}.rgba`);
  run(WS_SIM, ['0', String(WS_SCREEN[k.screen]), String(k.frame), '-1', '-1', '2', out, '--pack', dir]);
  return new Uint8Array(fs.readFileSync(out));
}

test('pack clip: wasm equals the host engine for several frames, scan and alert', () => {
  M._uv_clear_packs();
  const bytes = clipFile(syntheticClip());
  const dir = packWithClip(bytes);
  const id = loadWithClip(dir, bytes);
  assert.ok(id >= BUILTIN_COUNT, `uv_load_pack: ${id < 0 ? err() : ''}`);
  for (const frame of [0, 3, 7, 40, 133])
    wsSame(`WS pack clip frame ${frame}`, wsWasm(id, 'scan', frame, -1, -1, 2), wsSimDir(dir, { screen: 'scan', frame }));
  wsSame('WS pack clip under a HIGH alert', wsWasm(id, 'alert', 60, -1, 0, 2), wsSimDir(dir, { screen: 'alert', frame: 60 }));
  M._uv_clear_packs();
});

test('pack clip: it shows, plays over time, and the same pack without it is different', () => {
  M._uv_clear_packs();
  const bytes = clipFile(syntheticClip());
  const dir = packWithClip(bytes);
  const withId = loadWithClip(dir, bytes);
  const a = wsWasm(withId, 'scan', 7, -1, -1, 2), b = wsWasm(withId, 'scan', 12, -1, -1, 2);
  assert.notEqual(Buffer.compare(Buffer.from(a), Buffer.from(b)), 0, 'it advances with the frame counter');
  const plainId = loadWithClip(packWithClip(null), null);
  const none = wsWasm(plainId, 'scan', 7, -1, -1, 2);
  assert.notEqual(Buffer.compare(Buffer.from(none), Buffer.from(a)), 0, 'the clip shows');
  M._uv_clear_packs();
});

test('pack clip: the CYD preview never shows it, and a bad clip is refused with the draft intact', () => {
  M._uv_clear_packs();
  const cyd = (t) => { const n = W * H * 4, p = M._malloc(n); try { M._uv_render(t, 0, 7, -1, -1, p); return M.HEAPU8.slice(p, p + n); } finally { M._free(p); } };
  const good = clipFile(syntheticClip(2));
  const plainId = loadWithClip(packWithClip(null), null);
  const cydPlain = cyd(plainId);
  const id = loadWithClip(packWithClip(good), good);
  assert.equal(Buffer.compare(Buffer.from(cyd(id)), Buffer.from(cydPlain)), 0, 'the CYD draws the pack the same with or without a clip');
  const shown = wsWasm(id, 'scan', 5, -1, -1, 2);
  const dir = packWithClip(good);
  const flip = (i, v) => { const x = Uint8Array.from(good); x[i] = v; return x; };
  for (const [why, bytes, word] of [['wrong magic', flip(0, 0), 'UVRV'], ['not square', flip(5, 64), 'square'], ['fps 0', flip(7, 0), 'fps'], ['truncated', good.slice(0, good.length - 1), 'length'], ['over-size', new Uint8Array(780313), 'over']]) {
    const rc = loadWithClip(dir, bytes);
    assert.equal(rc, -6, why);
    assert.match(err(), new RegExp(word), why);
  }
  wsSame('a refused clip leaves the last good draft on screen', wsWasm(id, 'scan', 5, -1, -1, 2), shown);
  M._uv_clear_packs();
});

// ---- a colour clip (radar.vid version 2) ------------------------------------------------------------------------
test('colour clip: wasm equals the host engine, shows its own colours, and differs from the grey version', () => {
  M._uv_clear_packs();
  const frames = [];
  for (let f = 0; f < 5; f++) frames.push(Uint8Array.from({ length: 102 * 102 }, (_, i) => ((i % 102) + (Math.floor(i / 102) >> 3) + f) % 16));
  const pal = Uint8Array.from({ length: 32 }, (_, i) => (i % 2 ? 0xF8 - i * 3 : (i * 37 + 5) & 255));          // 16 very different colours
  const colour = clipFile(buildClip(frames, pal)), grey = clipFile(buildClip(frames));
  const dir = packWithClip(colour);
  const id = loadWithClip(dir, colour);
  assert.ok(id >= BUILTIN_COUNT, `uv_load_pack: ${id < 0 ? err() : ''}`);
  for (const frame of [0, 4, 9, 50])
    wsSame(`WS colour clip frame ${frame}`, wsWasm(id, 'scan', frame, -1, -1, 2), wsSimDir(dir, { screen: 'scan', frame }));
  const a = wsWasm(id, 'scan', 3, -1, -1, 2);
  M._uv_clear_packs();
  const id2 = loadWithClip(packWithClip(grey), grey);
  const b = wsWasm(id2, 'scan', 3, -1, -1, 2);
  assert.notEqual(Buffer.compare(Buffer.from(a), Buffer.from(b)), 0, 'the palette changes the picture');
  M._uv_clear_packs();
});

// ---- Plan 3: the layout rules and layout.json through the bridge (the device's own themelayout code) ----
const PIECES = ['menu', 'wordmark', 'status.gps', 'status.nodes', 'status.power', 'bridge', 'sd', 'nav.scan', 'nav.domain',
  'nav.codex', 'nav.poi', 'stop', 'count.wifi', 'count.5g', 'count.ble', 'radar', 'spectrum', 'channel', 'badge'];

test('layout: piece names, defaults and the verdict come from the device code', async () => {
  const { createPreview } = await import('../js/preview.js');
  const P = createPreview(M);
  assert.deepEqual(P.pieceNames(), PIECES);
  assert.deepEqual(P.defaultRect('waveshare', 'stop'), { x: 384, y: 276, w: 96, h: 44 });
  assert.deepEqual(P.defaultRect('cyd', 'radar'), { x: 151, y: 52, w: 115, h: 115 });
  const ok = JSON.stringify({ format: 1, waveshare: { wordmark: 'hidden' }, cyd: { wordmark: 'hidden' } });
  assert.deepEqual(P.checkLayout(ok), { ok: true });
  const bad = P.checkLayout('{"format":1,"cyd":{"menu":"hidden"}}');
  assert.equal(bad.ok, false);
  assert.match(bad.why, /menu cannot be hidden/);
  assert.match(P.checkLayout('{"format":1,').why, /layout\.json/);
});

test('layout: a pack with layout.json draws in wasm exactly as the simulator draws it', () => {
  const dir = path.join(tmp, `layout-pack-${simN++}`, 'lay');
  fs.mkdirSync(dir, { recursive: true });
  const json = fs.readFileSync(path.join(ROOT, 'sim/packs/umbravox-clone/theme.json'));
  fs.writeFileSync(path.join(dir, 'theme.json'), json);
  const layout = Buffer.from(JSON.stringify({ format: 1, waveshare: { stop: [288, 276, 96, 44], 'nav.poi': [384, 276, 96, 44], wordmark: 'hidden' } }));
  fs.writeFileSync(path.join(dir, 'layout.json'), layout);
  M._uv_clear_packs();
  const id = withBytes(new Uint8Array(json), (jp, jl) => withBytes(new Uint8Array(layout), (lp, ll) => M._uv_load_pack(jp, jl, 0, 0, 0, 0, lp, ll)));
  assert.ok(id >= 0, M.UTF8ToString(M._uv_error()));
  for (const frame of [0, 24]) {
    const out = path.join(tmp, `w${simN++}.rgba`);
    run(WS_SIM, [String(id), String(WS_SCREEN.scan), String(frame), '-1', '-1', '2', out, '--pack', dir]);
    wsSame(`layout pack scan frame ${frame}`, wsWasm(id, 'scan', frame, -1, -1, 2), new Uint8Array(fs.readFileSync(out)));
  }
  // and the layout really moved STOP: the wasm frame differs from the same pack without it
  const withLayout = wsWasm(id, 'scan', 24, -1, -1, 2);
  M._uv_clear_packs();
  const plain = withBytes(new Uint8Array(json), (jp, jl) => M._uv_load_pack(jp, jl, 0, 0, 0, 0, 0, 0));
  assert.notDeepEqual(wsWasm(plain, 'scan', 24, -1, -1, 2), withLayout);
});

test('layout: a refused layout.json refuses the draft with the reason, keeping the last good one', () => {
  M._uv_clear_packs();
  const json = fs.readFileSync(path.join(ROOT, 'sim/packs/umbravox-clone/theme.json'));
  const bad = Buffer.from('{"format":1,"cyd":{"stop":"hidden"}}');
  const id = withBytes(new Uint8Array(json), (jp, jl) => withBytes(new Uint8Array(bad), (lp, ll) => M._uv_load_pack(jp, jl, 0, 0, 0, 0, lp, ll)));
  assert.equal(id, -7);
  assert.match(M.UTF8ToString(M._uv_error()), /layout\.json: stop cannot be hidden/);
});

// ---- Plan 3 Task 2: the engine UI at 320x240 (the CYD engine build), for the Layout step's CYD tab ----
const CYD_W = 320, CYD_H = 240;
function engWasm(theme, screen, frame, conf = 2) {
  const n = CYD_W * CYD_H * 4;
  const ptr = M._malloc(n);
  try {
    M._uv_render_engine(theme, WS_SCREEN[screen], frame, -1, -1, conf, CYD_W, CYD_H, ptr);
    return M.HEAPU8.slice(ptr, ptr + n);
  } finally { M._free(ptr); }
}
function engSim(theme, screen, frame, packDir = null, conf = 2) {
  const out = path.join(tmp, `e${simN++}.rgba`);
  const args = [String(theme), String(WS_SCREEN[screen]), String(frame), '-1', '-1', String(conf), out, '--size', '320x240'];
  if (packDir) args.push('--pack', packDir);
  run(WS_SIM, args);
  const b = fs.readFileSync(out);
  assert.equal(b.length, CYD_W * CYD_H * 4);
  return new Uint8Array(b);
}
function engSame(name, a, b) {
  if (Buffer.compare(Buffer.from(a), Buffer.from(b)) === 0) return;
  let px = 0;
  for (let i = 0; i < a.length; i += 4) if (a[i] !== b[i] || a[i + 1] !== b[i + 1] || a[i + 2] !== b[i + 2]) px++;
  assert.fail(`${name}: ${px} of ${CYD_W * CYD_H} pixels differ`);
}

test('engine 320x240: every built-in theme draws as the simulator draws it', () => {
  M._uv_clear_packs();
  for (let t = 0; t < BUILTIN_COUNT; t++)
    for (const screen of ['scan', 'menu'])
      for (const frame of [0, 24]) engSame(`engine 320x240 theme ${t} ${screen} ${frame}`, engWasm(t, screen, frame), engSim(t, screen, frame));
});

test('engine 320x240: a CYD layout shows in the 320x240 preview', () => {
  const dir = path.join(tmp, `cyd-layout-${simN++}`, 'lay');
  fs.mkdirSync(dir, { recursive: true });
  const json = fs.readFileSync(path.join(ROOT, 'sim/packs/umbravox-clone/theme.json'));
  fs.writeFileSync(path.join(dir, 'theme.json'), json);
  const layout = Buffer.from(JSON.stringify({ format: 1, cyd: { wordmark: 'hidden', sd: 'hidden' } }));
  fs.writeFileSync(path.join(dir, 'layout.json'), layout);
  M._uv_clear_packs();
  const id = withBytes(new Uint8Array(json), (jp, jl) => withBytes(new Uint8Array(layout), (lp, ll) => M._uv_load_pack(jp, jl, 0, 0, 0, 0, lp, ll)));
  assert.ok(id >= 0, M.UTF8ToString(M._uv_error()));
  const withLayout = engWasm(id, 'scan', 24);
  engSame('cyd layout pack', withLayout, engSim(id, 'scan', 24, dir));
  M._uv_clear_packs();
  const plain = withBytes(new Uint8Array(json), (jp, jl) => M._uv_load_pack(jp, jl, 0, 0, 0, 0, 0, 0));
  assert.notDeepEqual(engWasm(plain, 'scan', 24), withLayout);
});

test('engine 320x240: the preview wrapper draws the cyd-engine board', async () => {
  const { createPreview } = await import('../js/preview.js');
  const P = createPreview(M);
  assert.ok(P.hasEngine());
  M._uv_clear_packs();
  engSame('wrapper cyd-engine', P.renderBuiltin(0, 'scan', 24, { board: 'cyd-engine' }), engWasm(0, 'scan', 24));
});

// ---- Plan 3 Task 3: every starting layout, and drafts of them, pass the device's own rules ----
test('layout presets pass the device rules on both boards; drafts of them are checked too', async () => {
  const { createPreview } = await import('../js/preview.js');
  const { PRESETS, presetJson } = await import('../js/layout-presets.js');
  const { fromJson, draftOther, toJson } = await import('../js/layout.js');
  const P = createPreview(M);
  const rules = { names: P.pieceNames(), defaultRect: (b, n) => P.defaultRect(b, n) };
  assert.ok(PRESETS.length >= 5);
  const draftVerdicts = [];
  for (const p of PRESETS) {
    const text = presetJson(p);
    if (!text) continue;
    const v = P.checkLayout(text);
    assert.equal(v.ok, true, `${p.id}: ${v.why}`);
    for (const [from, to] of [['waveshare', 'cyd'], ['cyd', 'waveshare']]) {
      const drafted = draftOther(fromJson(text), from, to, rules);
      const d = P.checkLayout(toJson(drafted, rules.names));
      draftVerdicts.push(`${p.id} ${from}->${to}: ${d.ok ? 'ok' : d.why}`);
    }
  }
  // Presets carry both boards, so they never rely on drafting; drafting them is a regression check of the drafting rule.
  // Radar-left packs the spectrum against the tiles on the smaller screen: a known case the author fixes on that tab.
  assert.deepEqual(draftVerdicts.filter((v) => !v.endsWith(': ok') && !v.startsWith('radar-left ')), [], 'presets draft onto the other board');
});

// ---- pack fonts and pictures (plan 4) -----------------------------------------
// The same bytes as sim/fontfix.h and sim/artfix.h (h=8 font; 4x4 picture, 2 frames).
function fixtureFont() {
  const head = [0x55, 0x56, 0x46, 0x4e, 1, 8, 7, 32, 95, 0, 0, 0];
  const A = [0x70, 0x88, 0x88, 0xf8, 0x88, 0x88, 0x88, 0x00];
  const table = [], bits = [];
  for (let i = 0; i < 95; i++) {
    const c = String.fromCharCode(32 + i);
    const off = bits.length;
    if (c === 'A') { table.push(6, 5, off & 0xff, off >> 8); bits.push(...A); }
    else table.push(c === ' ' ? 3 : 4, 0, off & 0xff, off >> 8);
  }
  return Uint8Array.from([...head, ...table, ...bits]);
}
function fixtureArt(w, h, frames) {
  const d = [0x55, 0x56, 0x41, 0x52, 1, 1, w & 0xff, w >> 8, h & 0xff, h >> 8, frames & 0xff, frames >> 8, 4, 0, 0, 0];
  for (const c of [0x0000, 0xf800, 0x07e0, 0x001f]) d.push(c & 0xff, c >> 8);
  for (let f = 0; f < frames; f++) { const ms = 100 * (f + 1); d.push(ms & 0xff, ms >> 8); }
  for (let f = 0; f < frames; f++) {
    const px = new Array(Math.ceil(w * h / 2)).fill(0);
    for (let i = 0; i < w * h; i++) {
      const x = i % w, y = Math.floor(i / w), v = x === 0 ? 0 : ((x + y + f) % 3) + 1;
      px[i >> 1] |= (i & 1) ? v : v << 4;
    }
    d.push(...px);
  }
  return Uint8Array.from(d);
}

test('uv_load_media takes the draft\'s fonts and pictures, and refuses bad ones with a reason', () => {
  M._uv_clear_packs();
  const id = loadFixture(PACKS[0]);
  assert.equal(id, BUILTIN_COUNT);
  const err = () => M.UTF8ToString(M._uv_error());
  const media = (kind, slot, bytes) => withBytes(bytes, (p, l) => M._uv_load_media(id, kind, slot, p, l));
  assert.equal(media(0, 0, fixtureFont()), 0, err());
  assert.equal(media(1, 15, fixtureArt(4, 4, 2)), 0, err());
  const bad = fixtureFont(); bad[4] = 9;
  assert.equal(media(0, 1, bad), -8);
  assert.match(err(), /f1: .*version/);
  assert.equal(media(1, 0, Uint8Array.from([1, 2, 3])), -8);
  assert.match(err(), /a0: /);
  assert.equal(media(0, 8, fixtureFont()), -1, 'f8 is not a slot');
  assert.equal(media(1, 16, fixtureArt(4, 4, 1)), -1, 'a16 is not a slot');
  assert.equal(withBytes(fixtureFont(), (p, l) => M._uv_load_media(999, 0, 0, p, l)), -1, 'no such theme');
});

test('styles: a pack with fonts, a background and decor draws in wasm exactly as the simulator draws it, on both boards', () => {
  const dir = path.join(tmp, `styled-${simN++}`, 'sty');
  fs.mkdirSync(dir, { recursive: true });
  const json = fs.readFileSync(path.join(ROOT, 'sim/packs/umbravox-clone/theme.json'));
  fs.writeFileSync(path.join(dir, 'theme.json'), json);
  const layout = Buffer.from(JSON.stringify({ format: 1,
    waveshare: { wordmark: { rect: [44, 0, 164, 44], fill: '#ff00ff', font: 'f0' }, style: { bg: 'a0' }, 'decor.0': { rect: [200, 120, 30, 30], art: 'a2' } },
    cyd: { style: { bg: 'a1', title: 'f0' } } }));
  fs.writeFileSync(path.join(dir, 'layout.json'), layout);
  const media = { f0: fixtureFont(), a0: fixtureArt(480, 320, 1), a1: fixtureArt(320, 240, 1), a2: fixtureArt(30, 30, 2) };
  for (const [n, b] of Object.entries(media)) fs.writeFileSync(path.join(dir, n + (n[0] === 'f' ? '.fnt' : '.art')), b);
  M._uv_clear_packs();
  const id = withBytes(new Uint8Array(json), (jp, jl) => withBytes(new Uint8Array(layout), (lp, ll) => M._uv_load_pack(jp, jl, 0, 0, 0, 0, lp, ll)));
  assert.ok(id >= 0, M.UTF8ToString(M._uv_error()));
  const bare = { ws: wsWasm(id, 'scan', 24, -1, -1, 2), cyd: engWasm(id, 'scan', 24) };
  for (const [n, b] of Object.entries(media))
    assert.equal(withBytes(b, (p, l) => M._uv_load_media(id, n[0] === 'f' ? 0 : 1, Number(n.slice(1)), p, l)), 0, M.UTF8ToString(M._uv_error()));
  for (const frame of [0, 24]) {
    const out = path.join(tmp, `w${simN++}.rgba`);
    run(WS_SIM, [String(id), String(WS_SCREEN.scan), String(frame), '-1', '-1', '2', out, '--pack', dir]);
    wsSame(`styled pack waveshare frame ${frame}`, wsWasm(id, 'scan', frame, -1, -1, 2), new Uint8Array(fs.readFileSync(out)));
    engSame(`styled pack cyd frame ${frame}`, engWasm(id, 'scan', frame), engSim(id, 'scan', frame, dir));
  }
  // not vacuous: the media changed both boards' pictures
  assert.notDeepEqual(wsWasm(id, 'scan', 24, -1, -1, 2), bare.ws);
  assert.notDeepEqual(engWasm(id, 'scan', 24), bare.cyd);
});
