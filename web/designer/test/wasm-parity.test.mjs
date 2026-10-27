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
  run('make', ['-s', '-C', path.join(ROOT, 'sim'), 'umbravox']);
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
  assert.equal(M._uv_theme_count(), 3);
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
    assert.equal(id, 3, `uv_load_pack: ${id < 0 ? err() : ''}`);
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
  assert.equal(loadFixture(PACKS[0]), 3);
  assert.equal(loadFixture(PACKS[1]), 3);
  assert.equal(M._uv_theme_count(), 4);
});

// ---- hostile input -----------------------------------------------------------

test('uv_load_pack rejects hostile input cleanly and keeps the current draft', () => {
  M._uv_clear_packs();
  assert.equal(loadFixture('sim/packs/cosmic-clone'), 3);
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
  assert.equal(loadFixture(PACKS[0]), 3, 'a good pack still loads after failures');
});
