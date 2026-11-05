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
  return withBytes(json, (jp, jl) => withBytes(emblem, (ep, el) => M._uv_load_pack(jp, jl, ep, el)));
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
    'null json with a length': () => M._uv_load_pack(0, 100, 0, 0),
    'negative length': () => withBytes(enc(good), (p) => M._uv_load_pack(p, -5, 0, 0)),
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

// ---- the radar clip in the Waveshare preview ------------------------------------
// A synthetic 102x102 clip, loaded through uv_set_video, must draw exactly what the host reference draws with the same bytes.
import { packLevels, buildClip } from '../js/radarvideo.js';

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
function loadClip(c) {
  const ptr = M._malloc(c.data.length);
  try { M.HEAPU8.set(c.data, ptr); return M._uv_set_video(ptr, c.frames, c.w, c.h, c.fps); } finally { M._free(ptr); }
}
function clipFile(c) {
  const f = path.join(tmp, `clip${simN++}.vid`);
  const h = Buffer.alloc(16); [c.w, c.h, c.frames, c.fps].forEach((v, i) => h.writeInt32LE(v, 4 * i));
  fs.writeFileSync(f, Buffer.concat([h, Buffer.from(c.data)]));
  return f;
}
function wsSimVideo(k, file) {
  const out = path.join(tmp, `v${simN++}.rgba`);
  run(WS_SIM, [String(k.theme), String(WS_SCREEN[k.screen]), String(k.frame), '-1', '-1', '2', out, '--video', file]);
  return new Uint8Array(fs.readFileSync(out));
}

test('radar clip: wasm equals the host engine for built-ins, several frames, scan and alert', () => {
  M._uv_clear_packs();
  const c = syntheticClip();
  assert.equal(loadClip(c), 0);
  const file = clipFile(c);
  for (const theme of [1, 2]) for (const frame of [0, 3, 7, 40, 133])
    wsSame(`WS video theme ${theme} frame ${frame}`, wsWasm(theme, 'scan', frame, -1, -1, 2), wsSimVideo({ theme, screen: 'scan', frame }, file));
  wsSame('WS video under a HIGH alert', wsWasm(2, 'alert', 60, -1, 0, 2), wsSimVideo({ theme: 2, screen: 'alert', frame: 60 }, file));
  M._uv_clear_video();
});

test('radar clip: it changes the picture, plays over time, and clears', () => {
  M._uv_clear_packs();
  M._uv_clear_video();
  const none = wsWasm(1, 'scan', 7, -1, -1, 2);
  assert.equal(loadClip(syntheticClip()), 0);
  const a = wsWasm(1, 'scan', 7, -1, -1, 2), b = wsWasm(1, 'scan', 7 + 5, -1, -1, 2);
  assert.notEqual(Buffer.compare(Buffer.from(none), Buffer.from(a)), 0, 'the clip shows');
  assert.notEqual(Buffer.compare(Buffer.from(a), Buffer.from(b)), 0, 'it advances with the frame counter');
  const mesh = wsWasm(1, 'mesh', 7, -1, -1, 2);
  M._uv_clear_video();
  assert.equal(Buffer.compare(Buffer.from(wsWasm(1, 'scan', 7, -1, -1, 2)), Buffer.from(none)), 0, 'clearing restores the old picture');
  assert.equal(Buffer.compare(Buffer.from(wsWasm(1, 'mesh', 7, -1, -1, 2)), Buffer.from(mesh)), 0, 'other screens never had it');
});

test('radar clip: the CYD preview never shows it, and a bad clip is refused', () => {
  const cyd = () => { const n = W * H * 4, p = M._malloc(n); try { M._uv_render(1, 0, 7, -1, -1, p); return M.HEAPU8.slice(p, p + n); } finally { M._free(p); } };
  M._uv_clear_video();
  const before = cyd();
  assert.equal(loadClip(syntheticClip()), 0);
  assert.equal(Buffer.compare(Buffer.from(cyd()), Buffer.from(before)), 0);
  const bad = syntheticClip(2);
  assert.ok(loadClip({ ...bad, w: 64 }) < 0, 'a wrong size is refused');
  assert.ok(loadClip({ ...bad, frames: 0 }) < 0, 'no frames is refused');
  assert.ok(loadClip({ ...bad, frames: 100000 }) < 0, 'an over-size clip is refused');
  M._uv_clear_video();
});
