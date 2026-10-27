import test from 'node:test';
import assert from 'node:assert/strict';
import { buildPack } from '../js/pack.js';
import { imageToEmblem } from '../js/emblem.js';
import { fromAccent, ROLES } from '../js/palette.js';
import { writePack, checkPack, ensureSim, gradient } from './_util.mjs';

const theme = (o = {}) => ({ name: 'ORACLE TEST', author: 'me', header: 'oracle', dial: 'large',
  centrepiece: 'heptagram', sweeps: ['radar', 'galaxy'], sweep: 'galaxy', alert: 'supernova',
  colors: fromAccent('#33AAFF'), ...o });
const solid = (w, h, r, g, b) => {
  const d = new Uint8ClampedArray(w * h * 4);
  for (let i = 0; i < w * h; i++) d.set([r, g, b, 255], 4 * i);
  return d;
};
const text = (pack, p) => new TextDecoder().decode(pack.files.find((f) => f.path.endsWith(p)).data);

test('the sim binary is available', () => ensureSim());

test('theme.json: stable order, camelCase keys, format 1', () => {
  const p = buildPack(theme(), null);
  const j = JSON.parse(text(p, '/theme.json'));
  assert.deepEqual(Object.keys(j), ['format', 'name', 'author', 'header', 'dial', 'centrepiece',
    'sweeps', 'sweep', 'alert', 'colors']);
  assert.equal(j.format, 1);
  assert.deepEqual(Object.keys(j.colors), ROLES);
  assert.ok(Object.keys(j.colors).includes('edgeLit'));
  assert.equal(p.files.length, 1);
  assert.equal(p.bytes, p.files[0].data.length);
});

test('folder name is safe and derived from the name', () => {
  assert.equal(buildPack(theme({ name: 'MIDNIGHT EMBER' }), null).files[0].path, 'midnight-ember/theme.json');
  for (const n of ['...', '--', 'A'.repeat(20) + '', '.-.']) {
    const f = buildPack(theme({ name: n }), null).files[0].path.split('/')[0];
    assert.match(f, /^[a-z0-9-]{1,31}$/, n);
  }
});

test('strings that would need escaping are refused', () => {
  assert.throws(() => buildPack(theme({ header: 'a"b' }), null));
  assert.throws(() => buildPack(theme({ author: 'a\\b' }), null));
});

test('emblem.bin is written only for the emblem centrepiece', () => {
  const e = imageToEmblem(solid(8, 8, 9, 9, 9), 8, 8);
  assert.equal(buildPack(theme({ centrepiece: 'heptagram' }), e).files.length, 1);
  const p = buildPack(theme({ centrepiece: 'emblem' }), e);
  assert.deepEqual(p.files.map((f) => f.path.split('/')[1]), ['theme.json', 'emblem.bin']);
  assert.equal(p.bytes, p.files[0].data.length + p.files[1].data.length);
});

// ---- the oracle: the real firmware parser, via the sim binary ----------
const accept = (label, t, e) => test('oracle accepts ' + label, () => {
  const r = checkPack(writePack(buildPack(t, e)));
  assert.equal(r.status, 0, r.out);
  assert.equal(r.out, 'OK ' + t.name);
});
accept('a heptagram pack', theme(), null);
accept('a pack with a lowercase header and no author', theme({ author: '', header: 'lower case' }), null);
accept('an emblem pack from a 1x1 image', theme({ centrepiece: 'emblem' }),
  imageToEmblem(solid(1, 1, 255, 0, 0), 1, 1));
accept('an emblem pack from a 500x300 photo-like gradient', theme({ centrepiece: 'emblem' }),
  imageToEmblem(gradient(500, 300), 500, 300));
accept('an emblem pack from a 96x96 image', theme({ centrepiece: 'emblem' }),
  imageToEmblem(gradient(96, 96), 96, 96));
accept('an emblem pack from a pure one-colour image', theme({ centrepiece: 'emblem' }),
  imageToEmblem(solid(50, 50, 20, 120, 220), 50, 50));
accept('an all-black theme (checks flag it; the parser still loads it)',
  theme({ colors: Object.fromEntries(ROLES.map((r) => [r, '#000000'])) }), null);

const reject = (label, t, re) => test('oracle rejects ' + label, () => {
  const r = checkPack(writePack(buildPack(t, null)));
  assert.equal(r.status, 1, r.out);
  assert.match(r.out, re);
});
reject('a reserved name', theme({ name: 'COSMIC MUFFIN' }), /reserved/);
reject('a 21-character name', theme({ name: 'A'.repeat(21) }), /name/);
reject('a bad header character', theme({ header: 'A{B' }), /header/);
reject('a lowercase name', theme({ name: 'lower' }), /name/);
reject('an empty sweeps list', theme({ sweeps: [] }), /sweeps/);

test('buildPack names the bad colour', () => {
  const t = theme(); t.colors = { ...t.colors, witch: 'zzz' };
  assert.throws(() => buildPack(t, null), /colour 'witch' is not #RRGGBB: zzz/);
  assert.throws(() => buildPack(theme({ colors: {} }), null), /colour 'void' is not #RRGGBB: undefined/);
});
