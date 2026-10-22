// Theme + emblem -> the files of a pack, exactly as the device parser reads them.
import limits from './limits.json' with { type: 'json' };
import { ROLES, isHex } from './palette.js';
import { emblemToBin } from './emblem.js';

// Strings are never escaped: the device rejects escapes, so refuse instead.
function plain(s, what) {
  if (typeof s !== 'string' || /["\\]/.test(s) || /[^\x20-\x7e]/.test(s)) {
    throw new Error(what + ' cannot contain a double quote, a backslash or non-ASCII characters');
  }
  return s;
}
const q = (s, what) => '"' + plain(s, what) + '"';

export function themeJson(t) {
  const sweeps = t.sweeps && t.sweeps.length ? t.sweeps : [];
  const sweep = t.sweep || sweeps[0];
  const lines = ['  "format": 1', '  "name": ' + q(t.name ?? '', 'name')];
  if (t.author) lines.push('  "author": ' + q(t.author, 'author'));
  if (t.header) lines.push('  "header": ' + q(t.header, 'header'));
  lines.push('  "dial": ' + q(t.dial || 'compact', 'dial'));
  lines.push('  "centrepiece": ' + q(t.centrepiece || 'heptagram', 'centrepiece'));
  lines.push('  "sweeps": [' + sweeps.map((s) => q(s, 'sweep')).join(', ') + ']');
  if (sweep) lines.push('  "sweep": ' + q(sweep, 'sweep'));
  lines.push('  "alert": ' + q(t.alert || 'ward', 'alert'));
  const cols = ROLES.map((r) => {
    const v = t.colors && t.colors[r];
    if (!isHex(v)) throw new Error('colour ' + r + ' must be #RRGGBB');
    return '    "' + r + '": "' + v.toUpperCase() + '"';
  });
  lines.push('  "colors": {\n' + cols.join(',\n') + '\n  }');
  return '{\n' + lines.join(',\n') + '\n}\n';
}

// A folder name the device accepts, derived from the theme name.
export function folderName(name) {
  const f = String(name || '').toLowerCase().replace(/[^a-z0-9]+/g, '-').replace(/^-+|-+$/g, '').slice(0, 31).replace(/-+$/, '');
  return f || 'pack';
}

export function buildPack(theme, emblem) {
  const dir = folderName(theme.name);
  const files = [{ path: dir + '/theme.json', data: new TextEncoder().encode(themeJson(theme)) }];
  if (theme.centrepiece === 'emblem') {
    if (!emblem) throw new Error('the emblem centrepiece needs a picture');
    files.push({ path: dir + '/emblem.bin', data: emblemToBin(emblem) });
  }
  return { files, bytes: files.reduce((n, f) => n + f.data.length, 0) };
}
