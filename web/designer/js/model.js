// The designer's state and the pure rules around it: templates, text cleaning,
// opening an existing pack. Imports nothing from the DOM.
import limits from './limits.json' with { type: 'json' };
import { ROLES, isHex } from './palette.js';

// ---- sweeps: pack names -> the device's ui::Sweep ids (gaze = 1 is not offered) ----
const SWEEP_IDS = { radar: 0, pulse: 2, star: 3, candle: 4, galaxy: 5, orbit: 6, starfall: 7, pulsar: 8, comet: 9, aurora: 10 };
export const sweepId = (name) => (Object.hasOwn(SWEEP_IDS, name) ? SWEEP_IDS[name] : -1);

// The sweep the preview starts on: the pack's own, or the first it offers.
export function startSweep(t) {
  const s = Array.isArray(t.sweeps) ? t.sweeps : [];
  if (!s.length) return null;
  return s.includes(t.sweep) ? t.sweep : s[0];
}

// ---- text the device cannot read -------------------------------------------
export function sanitizeText(s) {
  const value = String(s ?? '').replace(/["\\]/g, '');
  return { value, stripped: value !== String(s ?? '') };
}
// Names and headers are shown as capitals on the device, so type them that way.
export function sanitizeName(s) {
  const r = sanitizeText(s);
  return { value: r.value.toUpperCase(), stripped: r.stripped };
}

// ---- templates --------------------------------------------------------------
const C = (o) => ({
  void: o[0], panel: o[1], grid: o[2], edge: o[3], edgeLit: o[4], dim: o[5], text: o[6], bright: o[7],
  witch: o[8], witchHi: o[9], bone: o[10], sweep: o[11], sweepHi: o[12], live: o[13], liveHi: o[14],
  magenta: o[15], ice: o[16], green: o[17], amber: o[18], red: o[19],
});
const ALL_SWEEPS = limits.sweepNames.slice();

// Colours are the built-ins' own (ui/palette.cpp). BAPH0MAU5's seal is not
// available to packs, so its template is a heptagram in the seal's colours.
const PALETTES = {
  umbravox: C(['#07040E', '#130B22', '#180F2C', '#2E1F52', '#563697', '#6B4F9E', '#C2ABE4', '#F2E8FF', '#9D7BFF', '#D6C2FF',
    '#C9B897', '#9D7BFF', '#D6C2FF', '#D6C2FF', '#F2E8FF', '#D02BB4', '#6FD3F5', '#4ADE80', '#F59E0B', '#EF4444']),
  baph0mau5: C(['#06030C', '#130921', '#1D1133', '#3C2062', '#743CB4', '#7E56BC', '#CAB0EA', '#FFFAFF', '#9B62F0', '#D6BCFF',
    '#E4DAF6', '#3BFF6E', '#C4FFD4', '#24C450', '#3BFF6E', '#3BFF6E', '#F2EEFF', '#9B62F0', '#F59E0B', '#D42420']),
  cosmic: C(['#09071C', '#16103A', '#1E1646', '#3A2A6E', '#A84DFF', '#8A7BD0', '#E8DEFF', '#FFFFFF', '#A84DFF', '#FF1FCC',
    '#FFD04A', '#1EF5EE', '#FF1FCC', '#FF58B0', '#FFF0D0', '#FF1FCC', '#1EF5EE', '#3BFF9E', '#FFC24A', '#FF4D6A']),
  blank: C(['#0A0A0A', '#161616', '#1E1E1E', '#3A3A3A', '#8A8A8A', '#8C8C8C', '#E0E0E0', '#FFFFFF', '#B0B0B0', '#D8D8D8',
    '#C8C8C8', '#B0B0B0', '#E0E0E0', '#C0C0C0', '#F0F0F0', '#C0C0C0', '#D0D0D0', '#A0A0A0', '#F59E0B', '#EF4444']),
};

const COSMIC_EMBLEM_B64 = 'VVZFAUhIEAA1hbNr0XLSKu957kkuGowhroCsSAsxSjAqISkgKglJCAwMDAwNDQwMDw8PDg4ODw8PDg4ODw8PDw8ODg8PDgwMDAwMDg4ODAwMDAoKDA0NDAwMDw8PDw8MDg4PDw8PDw8ODw4HBQoMDAoMDAwMDAwODw8ODg4PDw4ODg4ODw8PDw8NDQ0PDgwHBwwMDAwMDAwMDAoKDQ0NDQwNDQ8PDw8PDw4ODw8PDw8PDgwFAQMMDAoMDQwMBwwPDw4ODg4ODg4ODg4ODw8PDw0NDAwMBwcMDAwMDAwMDAwMDAwMDQwPDw8NDA8PDw8NDQ0ODw8PDw8PDAcBAAMHDAoMDAwNDw4ODg4ODg4ODg4ODg4ODw8NDQ0MDAwHBwwMDAwMDAwMDAwMDAwMDg4ODg8PDw4ODg8PDQoKDA0PDw8PDAcAAAEHDAoMDgwPDw8PDg4ODg4ODg4ODg4ODw8NCgkKDAoFAgEDBwwMDAwMDAwMCgoMDgMDDg4PDg8PDg4ODwoEBA0NDw8ODAcBAAADBw4ODg4ODg4PDg4ODg4ODg4ODg4ODgwMCgkJCgUFAgABBQwMBwcDBwwMDA4ODgwHDAwKCQkJCgwMDAwMCgwNDw8ODAwKAQAAAQ8ODgwMDg4ODg4ODg4ODg4ODg4OBwwMCgUFBQUFBQIBBwcFBwcHAwYHAgIMDgwJCQIEAgQJCgwMDAwMDAwMDA4ODAwMBwYFCg4OBQIFDg4ODg4ODg4ODgwMDgYDDAkJBQIFBQUFBQUFBgUAAgIBAgIDAwYHBwkCAAACCQwMDAwODg4ODgwNDg4ODg4ODAwMDA4HAgEFDg4ODg4ODg4ODAwHBgMDDAkFBQUFAgEBAQEBAAABAAACBAoDAwMDBwUCAAEEBQUFCgwODg4ODgwMDg4ODg4ODg4ODg4OBwYMDAcMDg4FAAEFDAwGAAEGBwMDAwMBAQEBAAECAQEBAAAECQsKBQMDAwMCAgIJBAUFBQcMDAwFAwwNDQ4ODg4ODg4PDw4ODg4OBwwODgwAAAIECgcGAQMDAQAAAAEAAAAAAQECAgUFAQQICQsLBQMDAwMDAwMDAgUHBwcMDAUBAwwMDQ8ODw4PDw8PDwwODg4OBwwMDAwCBAQJCgcGBgMDAAAAAQEBAQECAgQFBQwJBQoFCg0MAwMHBgMBAQAAAwMFBQcHDAwKCgwNDA8PDg4PDw8PDwwMDg4ODAwMDAwFBAUFBwYDAQMAAAABBQkEBAQFBQcMBgIMAwMHDAwMDA0KAgEBAAAAAwMDBgcHBwwMDAwMDA0NDw4PDQ8PDwwMDg4MDAwHBQwMBwcHBQMBAQAAAAMFCQUFAgICBQMDAwEDAwEDAwMDAwYMBQEAAAAAAwMDAwYFAgoMCgwMCg0NDQ8PDw8PDwwODg4HDAcCAgUHBwYFAwAAAAABAgUFCgUAAAEDAwMDAwEAAQEDAwMDAwMFDAUAAAADCgMDAwMDAgoMCQoMDA0NDQ0PDw8PDw4ODg4MBwUBAgUFBwMBAAAAAAIBBQACAwEAAAMDAwICAgABBAgJCQkJBQQJDQsEAgIDBQMBAQMDAwMFBAkKDA0NDQ0PDw8PDg4ODg4HBwcFBQUFAwEAAAAAAAIFCgIDAwMDAwMFAgIEBAQIBAgICAgICAgJCwsLCQoKAwMDAQEBAwMCAAIJDAwMDA8PDw8PDA4ODgcHBwcFBQMDAQEAAAACBAkJAgcDAwMGBQICAgQIBAgEBAQICAgICAgICQkLCw0PDAcKBQIBAQMCAAIJDAwMDA8PDw8ODA4ODgcHBwUFAwMBAQABAgIEAgQFAwMDAwYKBQICBAQEBAQCBAQICAQICQgICAgJCwwMDQUKBQUDAQEBAgIMDAwMDg4ODw8ODA4HBwYFBQUBAQEAAAEEBAQEBAQDAwMAAwYFAgAABAQAAAAAAAAAAQECBAQEBAgICAsLDQsMBgMDAQABAwMKCgwMDAcODg4MDAcGBQUFAgEAAQAAAAIEBAIECQoDAwMAAwYEAQACAgAAAAAAAAAAAgICAgQEBAQICAgJCQsMBwcGAwEBAwMBBQwMDAwMDAwMDAcGBQICAQEAAAAAAAEECAQECw0DAwAAAAUEAgAAAAAAAAAAAAACBAQEBAQBAQIECAgIBAkLDQ0MAwMDAQEBBQcMDAwMDAwMDAUFAQIBAQAAAAAEBAICBAQJDQ0DAwAAAgkJBAAAAAAAAAAAAAAABAQEBAQEAQEBCAgICAQJCwsKDAcKAQABAwMMDAwMDAwKDAUCAQAAAAAAAAAEBQEAAAQFBQ0GAwMBAgQJCAIAAAAAAAAAAAAAAgQEBAQJBQMBBAgICAQECwsLBQUHBQAAAQMFBQoMDAoKDAIBAAAAAAAAAAIJBQEAAAQECQ0MAwMDAQIICAQEAgAAAAAAAAACBAQEBAQJBQMBAQgICAgECQsFAQEGCgEAAQECAgUKCgwMDAEAAAEAAAAAAAgJCQIABAgJCw0NBQMDAwECBAQICAQAAAAAAAICBAQEBAQJAwAAAQQICAgICAsFAQMHDQUBAAECAgQJCgoMDAEAAQEAAAAABAkLCQUECAkLCwsNCQYGBgMBAQICBAIAAAABAgECCQUEBAQEAQAAAQQICAgECAkHBgYNDQkCAAABAgIEBQkKCgABAQEAAAAABAkLDQ0LCwsJCAkLCQ0KBwYFAgEBAQEBAQIEAgUKCQQEBAIBAQADAgQICAgEBAkNDQ0MBwUEAgEBAgICBAQECgICAgAAAAAABAQJCgwKCwsICAQECQsLBQYHBgMDAwMDAwUFBQwLCQQEBAEDAAADAggICAgEBAsNDQoHAwMEBAMBAwMDBQQECQICAQAAAAAAAgQEBAoKCwkJCAQECAgIBQUKCgUFBQUGBQ0FCQkJCAgIBQMAAAMDBAgICAQECQ0NDQoGAwMICQUBAQMDBQUEBAIBAAAABAQAAgQEBAUKCgULCQQIBAQECAgJCAgICAkJCQkCBAgICAQFAwMDAwMDCAgICAgECQsLDQcDAwIJDQ0DAQMDAwUJBAIBAAAABAQCAgUFBQUFCgULCQgIBAQECAgICAQICAgICAQECAgICQUDAwMDAwIFCAgICQkJCwsKDAYDAQEJCwoDAwMDBQoJCAICAQACBAUBAgIFCgwMCgcHCgkFBAQECAgIBAQICAgIBAIECAUFBQMDAwMFAwIFBAQICQsLDQwMBgMDAQIJCQoGAwIDBQwKBQICAQACBAoBBQUFBQYFAwMDAwUCAwICBQgIBAgICAgCAAEBAgMDAwMDBQUFAwUKBQQJCgsNDAwHAwMBAgkKCgwMAwICBQwKBQICAAAEBAkKCgUDAwEBAQMDAwMDAwEDAwMDAwIEBAIBAAEDAwMDAwUFCgUFBQkEAgQJDQ0NBwUFAQIEBQsNDAwHAQECBQwKCAICAAEECQkKCQUDAQABAwMDAwMDAQAAAwMDAwMDAQMDAwUDBQUFCQsKCgUFBAQCAgkNDQ0KAwECBAQEBQoNDA0DAQIFCgwKCAQCAgIECQsLCAkMAgIKBwYDAAMDAQAAAwMFCgoFAQMFCQkFCQkLCwwMCgQEAgIECQ0NCwsEAAAECAkEAgINDQ0DAQIFCgoFCAUEAgECBAsLCAsMCgcGAwMDAAMDAwMDAwcKCQkJAwUJCAsLCwkKDAwKAgAABAQJDQ0LCQgEBAQICQkFAgIMDAcBAQIFCgkEBAUEBAEBAgkLCw0NBwYDAwMDAwMGBQUFBQkJCwkJCQgICQoNDAcHBwwKAgAABAkMDAoJCAgICAgECQ0NDQ0NAwEBAQMFBQUCBAUFBAEBAQAJCQsNDQwKCgcHCgsLCwsLCwkLCwsLCQkHCgQHDgMDBgcKBQQECQ0PDAgICAkJCQkFCg0NDQ0HAQEBAwMDAQECBQoJBQICAQAABAkLCwsJCg0LCQkJCwsLCQkKDQwGAwMGDAwMBwYHAwcMDA0NDQsLCQQICQkMDQ0NDQ0NDQcDAAEBAwEBAQUKDAwKBQMCAQAAAAEECQsNDQ0NCwkKDQwNCgoGAwMDAwYMDQwMAgwPDAwNDQ0LCwkJCQgJDQwMDw8PDw0MBgMAAQEBAQEBAgUMDAwMDAYDAQAAAAAABAkLDQ0NDQwNDQ0MDAYGAwUNDQwNDQ8NBQ0PDQ0NCwkJCQkJCQsMDQ8PDQ0NDQcDAQAAAQEBAwMFCgwMDAoMDAcGAwEBAQAABAkJCQsLDQ0NDQ0NDA0PDgwNDQ0NDQ0NDQ8NCgsLCwkJCw0NDQ0NDQ0NDQ0NDQ0GAAABAQEDBQwHBwwMDAkJCgUFAwMBAQAAAAQKDA0NDQsLCw0NDQ8NDw8PDQ8PDQ0NDQ0NDQsLCw0NDQ0NDQ0NDQ0NDQ0NBwYDAAABAAMFBwwMDAwMDAkJCQQEBQUDAQAAAAECBQwMDAwHDQ0NDQ0NDQ0NDQ0NDQ0NDQ0NDQ0NDQ0NDAwMDQ0NDQ0NDA0HAwMAAAMBAwYHBwwMDA8PDAkKCgUFBQUDAQEAAAACBQoKDAcDDAwMDQ0NDQ0NDQ0NDQ0NDQ0NDQ0NDQ0PDQ0PDAwPDA8MDAwGAwEAAAEBAwYMDAwMDw8PDAoMCgkJCQUEAQEBAAABBQUMBwcBDAcMDwUFDQUFCgsJCQsJCQ0KCQsNCwsPDQwPDAcNAw4MDAYDAwAAAAEBAwcMDA0NDQ0MDQwNCgoMCQUFBQEBAAAAAQIGAwcBDAwFDwMFCgUECQkECQsBCQsIBAkJCQsPDAcPDAwOAwcDBgMDAwAAAAACAwUMDAwMDAwNDQ8NDQwMDAkJCQMDAQAAAAEFAwwFBw4DBwMFCwkICQQCCQsEBAkIAgkJCQsNDAwHAw4OBwwDAwMDAQAAAQECAgkJCw0NDQ0NCw4PDQ0MDAwMCgMDAQAAAAEBAwUHAw4DAgUDCgkECAkCCQsEBAkEAQUNDQwNDAwHAwwFDAwDAwMDAAABAQEBAgIJDAwMDAwLCQ4ODw0NDAwMCgMDAwEAAQQCBQMOAw4DBgwDBQUCBQkCCg0CBAkCAQoMDAwMDAcDAwwHDAMDAwMDAAABAwMDAgIECQoMDAwLCQ4ODg4ODg4MBwUFAwMAAQICBQEHAQcGBw4DBgcFBQYDCg0CBAoAAQoLCgwKBg8DAw4DBwUDAwMBAAABAwMJAwkECAoMDAwLCQ4ODAwODg4ODAcGAwMBAAECBQMFAQUFBQwDBgwFAwcDBQwCBAoDAwcKCg0KAw4OBgwDBwUFAwMBAAEDAwUMBwkEBAoMDAwMCw4MDAwODgwHDgcHBgMBAAIEBAEJBAQFAwcDBgwGAwcBAQwDBQoDBgcMDQ0HAw4ODgwDCgIFBQIBAQEDAwUMDAoFBAkKDAwODA4ODg4MDAUBBgcHBwYDAQIICQgJCQQJAgUDBg4GAwcDAwwDAwcDBwwMDQ0HAwwHDAcDCQQJBQIBAAMDAwwMDAoFBAkKDA4ODgwODg4ODgwGDA4HBwYDAQIIBAgECQQJAgUGBg4GAw4GAwwDBwwDBwwMDw8OBwcDDAIICQkJCQQBAQIFBQwMDAoCAgUKDg4ODg4ODg4ODgwMDAcDAwMDAwIJCAkICQgJCQsMDAwGAwwHAwwDDAwDBw8MDg4OAwMDCggJCwsJCQQCAQMFDAwMCgICBQoMDg4ODg4ODAwMDAwHBQcGBgYDAwMJCQkJCQkLDAwODA8HBg4MBwwDDA4HDg4MDg4DAwMDCQkLDQ0LCQUBAgMFDAwMBQIBBQoMBwcODA4MDAcHDAoMBw4MBwYDAwMFCQsJCwwNDA0OBgUMBw4OBwwHDA4ODg4OBgMDAwUJCQkNDQ0NCwIBAgUKDAwKBQIBBQwMDA4ODA8MDAcHCgwMDA4OBgMDAwMFDQ0LDQ0NDAwOBgcPDAwMDgwMDg4ODg4GAwYHCgkJCwsNDQ0NDAUBBQUJCgoFBAQECgwMDAwMCw0KCgoKDAwMDg4GAwMDAwMFDQ0NCg0NDg4ODg4ODg4ODg4ODg4GDg4DBQoKCQkJDQwNDQ0NDAUDBQkKBQUECQkKDAwMDA0NDQoKBQUKDAcKDg4GAwMDAwMDDQ0NBQwMDg4ODg4OBgMGBgYGDgYDBwoFCQkJCwsLDQ0PDQ0NBwMDBQUFBQUJCQoMDA0NDQ0NDAoJBQUKDA4ODgcGAwMDAwMDDA0MCg8OBwYGAwMDAwMDAwMDAwYHCgoJCQsLDQ0PDQ0LDQ0NCgMFBQUFBAQJCgwNDQ0NDQ0MDAoFAgUKDAwODgcGBgMDAwMDDA0NDQ8ODgYGAwMGBgYGBgYGBgwLCQkJCw0NDQ0PDQ0KDQ0NBQMFBQQFBQoKDQwMDQ0NDQcGBwoKBQIKDAwODg4OBwYDAwMDBg0NDQ8OBw4GBgYODg4MDQ0MDQsJCwsLDQ0LCQsPDQ0LDQ0HBQUFBQUJCgwMDQ0NDQ0NBwMDAwwKBQIFDAwODg4ODgcHBgYGAwYMDw8ODAwMBwwODg8MDQ0NDQsLDQ0NDQ8NCgsPDQwNDQUFBQUFCgoKDAoMDQ0NDQ0MBgMDAwwKCQUDCgwMDA4ODg4MDAwHAwMGDA4PDwwMDQ0NDw8PDwwPDQ8PDQ0PDw8PDw8PDwwFCgUFCQoKDAwKDQoNDQ0NDQ0MBgMDAwwKCQUFCQUMDAwHBwwMDAwHBgMGDAwGDA4PDw8PDw8PDw0PDw8PDw8PDw8PDwwMBgUFCgoKCgwMDAwNDw0NDQ0NDQwMBwMDAwwNCwkFBAQKDAwMDAwMDAwMBwcMDAwGDg4ODg4ODg4PDg8ODg8PDw8PDw4OBwcMBQUJCgoMDQ0NDQ8PDw0NDQ0NDAwMBwYDAw0NDQoJBQQJCgoMDAwMDAwMDAwMDAwMDg4ODg4ODg4ODg4ODg4OBwcHBwcHBwcMCgoKDAwNDQ0NDw8PDQ0NDQ0MDAwHBgMDAwsLDQ0KCQkEBAkKCgwMDAwMDAwMDAwMDg4ODg4ODg4ODg4ODg4OBwcHBwwMDAwMDAwMDQ0PDQ0PDw8PDQ0PDQ0MCgcFBQMDBQ==';

export const TEMPLATES = [
  { id: 'umbravox', label: 'UMBRAVOX', blurb: 'violet heptagram',
    base: { name: 'MY UMBRAVOX', header: 'UMBRAVOX', dial: 'compact', centrepiece: 'heptagram', alert: 'ward' } },
  { id: 'baph0mau5', label: 'CVLT OF BAPH0MAU5', blurb: 'the seal\'s colours (the seal itself stays built in)',
    base: { name: 'MY BAPH0MAU5', header: 'CVLT OF BAPH0MAU5', dial: 'large', centrepiece: 'heptagram', alert: 'supernova' } },
  { id: 'cosmic', label: 'COSMIC MUFFIN', blurb: 'neon galaxy muffin',
    base: { name: 'MY COSMIC MUFFIN', header: 'COSMIC MVFFIN', dial: 'large', centrepiece: 'emblem', alert: 'supernova' } },
];

export function b64ToBytes(b64) {
  const bin = atob(b64), out = new Uint8Array(bin.length);
  for (let i = 0; i < bin.length; i++) out[i] = bin.charCodeAt(i);
  return out;
}

// A fresh {theme, emblem}; callers may edit it freely.
export function makeTemplate(id) {
  const t = TEMPLATES.find((x) => x.id === id);
  if (!t) throw new Error('unknown template ' + id);
  const theme = { format: 1, author: '', ...t.base, sweeps: ALL_SWEEPS.slice(), sweep: 'radar', colors: { ...PALETTES[id] } };
  return { theme, emblem: id === 'cosmic' ? emblemFromBin(b64ToBytes(COSMIC_EMBLEM_B64)) : null };
}

export function blankTheme() {
  return { format: 1, name: 'NEW THEME', author: '', header: '', dial: 'compact', centrepiece: 'heptagram',
    sweeps: ALL_SWEEPS.slice(), sweep: 'radar', alert: 'ward', colors: { ...PALETTES.blank } };
}

// ---- state -> the theme object pack.js and checks.js take --------------------
export function themeFromState(state) {
  const t = state.theme;
  const sweeps = limits.sweepNames.filter((s) => (t.sweeps || []).includes(s));
  const out = {
    ...t,
    name: sanitizeText(t.name).value,
    author: sanitizeText(t.author).value,
    header: sanitizeText(t.header).value,
    sweeps,
    colors: { ...t.colors },
  };
  out.sweep = startSweep({ sweeps, sweep: t.sweep });
  if (out.sweep === null) delete out.sweep;
  return out;
}

// ---- emblem.bin -> the emblem object ------------------------------------------
export function emblemFromBin(b) {
  if (!b || b.length < 8 + 4) return null;
  if (b[0] !== 0x55 || b[1] !== 0x56 || b[2] !== 0x45 || b[3] !== 1) return null;
  const w = b[4], h = b[5], ncol = b[6];
  if (w < 1 || h < 1 || w > limits.MAX_EMBLEM_DIM || h > limits.MAX_EMBLEM_DIM) return null;
  if (ncol < 2 || ncol > limits.MAX_EMBLEM_COLORS || b[7] !== 0) return null;
  if (b.length !== 8 + 2 * ncol + w * h) return null;
  const pal565 = [];
  for (let i = 0; i < ncol; i++) pal565.push(b[8 + 2 * i] | (b[9 + 2 * i] << 8));
  const px = Array.from(b.subarray(8 + 2 * ncol));
  if (px.some((i) => i >= ncol)) return null;
  return { w, h, ncol, pal565, px };
}

// ---- opening an existing theme ---------------------------------------------------
const printable = (s) => s.replace(/[^\x20-\x7e]/g, '');

// theme.json text -> {ok, theme, warnings} | {ok:false, error}. Never throws.
export function parseThemeJson(text) {
  let j;
  try { j = JSON.parse(text); } catch (e) { return { ok: false, error: 'theme.json is not valid JSON (' + e.message + ').' }; }
  if (!j || typeof j !== 'object' || Array.isArray(j)) return { ok: false, error: 'theme.json must be a JSON object.' };
  if (j.format !== 1) return { ok: false, error: 'theme.json has format ' + JSON.stringify(j.format) + '; this designer reads format 1.' };
  if (typeof j.name !== 'string') return { ok: false, error: 'theme.json has no "name" text.' };

  const warnings = [];
  const str = (v, label) => {
    if (v === undefined) return '';
    if (typeof v !== 'string') { warnings.push(label + ' is not text; left empty.'); return ''; }
    const c = printable(sanitizeText(v).value);
    if (c !== v) warnings.push(label + ' had a quote, backslash or non-ASCII character; removed.');
    return c;
  };
  const pick = (v, list, def, label) => {
    if (v === undefined) return def;
    if (list.includes(v)) return v;
    warnings.push(label + ' ' + JSON.stringify(v) + ' is not one of ' + list.join(', ') + '; using ' + def + '.');
    return def;
  };
  const theme = {
    format: 1,
    name: str(j.name, 'name'),
    author: str(j.author, 'author'),
    header: str(j.header, 'header'),
    dial: pick(j.dial, limits.dialNames, 'compact', 'dial'),
    centrepiece: pick(j.centrepiece, limits.centrepieceNames, 'heptagram', 'centrepiece'),
    alert: pick(j.alert, limits.alertNames, 'ward', 'alert'),
  };
  let sweeps = limits.sweepNames.slice();
  if (j.sweeps === undefined) warnings.push('no sweeps listed; the device requires this key; all sweeps were offered.');
  else {
    if (!Array.isArray(j.sweeps)) warnings.push('sweeps is not a list; offering all of them.');
    else {
      const bad = j.sweeps.filter((s) => !limits.sweepNames.includes(s));
      if (bad.length) warnings.push('Unknown sweeps ignored: ' + bad.map((s) => JSON.stringify(s)).join(', ') + '.');
      sweeps = limits.sweepNames.filter((s) => j.sweeps.includes(s));
    }
  }
  theme.sweeps = sweeps;
  theme.sweep = startSweep({ sweeps, sweep: j.sweep }) || undefined;
  if (theme.sweep === undefined) delete theme.sweep;

  const cin = j.colors && typeof j.colors === 'object' && !Array.isArray(j.colors) ? j.colors : {};
  theme.colors = {};
  const missing = [];
  for (const r of ROLES) {
    const v = cin[r];
    if (isHex(v)) theme.colors[r] = v.toUpperCase();
    else { theme.colors[r] = PALETTES.blank[r]; missing.push(r); }
  }
  if (missing.length) warnings.push('Missing or invalid colour' + (missing.length > 1 ? 's' : '') + ' (' + missing.join(', ') + ') set to neutral grey.');
  return { ok: true, theme, warnings };
}

// Which of the files the user chose may be read at all. Takes descriptors
// {name, webkitRelativePath, size} (or {name, data}) -- no contents -- so a
// card root picked by mistake costs nothing. Keeps only theme.json and
// emblem.bin, checks their size BEFORE any read, refuses more than one
// theme.json and an emblem.bin from a different folder.
export function selectPackFiles(list) {
  const pathOf = (f) => String(f.webkitRelativePath || f.name);
  const base = (f) => pathOf(f).split('/').pop();
  const dir = (f) => pathOf(f).split('/').slice(0, -1).join('/');
  const size = (f) => (typeof f.size === 'number' ? f.size : f.data ? f.data.length : 0);
  const themes = list.filter((f) => base(f) === 'theme.json');
  const emblems = list.filter((f) => base(f) === 'emblem.bin');
  if (!themes.length) return { ok: false, error: 'No theme.json found. Choose a theme.json, or the theme folder that holds one.' };
  if (themes.length > 1) return { ok: false, error: 'Found more than one theme.json (' + themes.length + '). Choose one theme folder, not a folder of themes.' };
  const t = themes[0];
  if (size(t) > limits.MAX_JSON) return { ok: false, error: 'theme.json is ' + size(t) + ' bytes; a theme.json over ' + limits.MAX_JSON + ' bytes is not a theme and was not read.' };
  const picked = [t];
  for (const e of emblems) {
    if (dir(e) !== dir(t)) return { ok: false, error: 'emblem.bin is not in the same folder as theme.json. Choose one theme folder.' };
    if (size(e) > limits.MAX_EMBLEM) return { ok: false, error: 'emblem.bin is ' + size(e) + ' bytes; over the ' + limits.MAX_EMBLEM + ' byte limit, so it was not read.' };
    picked.push(e);
  }
  return { ok: true, picked };
}

// Files the user gave us ({name, data}) -> {ok, theme, emblem, warnings} | {ok:false, error}.
export function openPackFiles(files) {
  const sel = selectPackFiles(files);
  if (!sel.ok) return sel;
  const base = (f) => String(f.name).split('/').pop();
  const tj = sel.picked.find((f) => base(f) === 'theme.json');
  const r = parseThemeJson(new TextDecoder().decode(tj.data));
  if (!r.ok) return r;
  let emblem = null;
  if (r.theme.centrepiece === 'emblem') {
    const eb = sel.picked.find((f) => base(f) === 'emblem.bin');
    if (!eb) return { ok: false, error: 'This theme uses an emblem but no emblem.bin came with it. Choose the whole theme folder, or select theme.json and emblem.bin together.' };
    emblem = emblemFromBin(eb.data);
    if (!emblem) return { ok: false, error: 'emblem.bin is not a valid emblem (wrong header, size or palette).' };
  }
  return { ok: true, theme: r.theme, emblem, warnings: r.warnings };
}
