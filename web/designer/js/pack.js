// Theme + emblem -> the files of a pack, exactly as the device parser reads them.
import limits from './limits.json' with { type: 'json' };
import { ROLES, isHex, describe } from './palette.js';
import { emblemToBin } from './emblem.js';
import { clipFile, resizeClip, CLIP_SIZE } from './radarvideo.js';
import { soundFile, hasSounds } from './alertsound.js';

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
  if (t.heptagram === 'off') lines.push('  "heptagram": "off"');
  if (t.contacts === 'off') lines.push('  "contacts": "off"');
  lines.push('  "sweeps": [' + sweeps.map((s) => q(s, 'sweep')).join(', ') + ']');
  if (sweep) lines.push('  "sweep": ' + q(sweep, 'sweep'));
  lines.push('  "alert": ' + q(t.alert || 'ward', 'alert'));
  const cols = ROLES.map((r) => {
    const v = t.colors && t.colors[r];
    if (!isHex(v)) throw new Error("colour '" + r + "' is not #RRGGBB: " + describe(v));
    return '    "' + r + '": "' + v.toUpperCase() + '"';
  });
  lines.push('  "colors": {\n' + cols.join(',\n') + '\n  }');
  return '{\n' + lines.join(',\n') + '\n}\n';
}

// The clip at the Waveshare radar's placed radius (the layout's radar box is 2r+5 square), so the board fills the disc
// without scaling; the CYD draws no clip. No layout, or the built-in radar size: the clip as it is.
function clipForLayout(clip, layout) {
  if (!layout) return clip;
  let r = null;
  try { const box = JSON.parse(layout).waveshare?.radar; if (Array.isArray(box)) r = Math.floor((box[2] - 5) / 2); } catch (e) { return clip; }
  return r && r < CLIP_SIZE && r >= 24 ? resizeClip(clip, r) : clip;
}

// A folder name the device accepts, derived from the theme name.
export function folderName(name) {
  const f = String(name || '').toLowerCase().replace(/[^a-z0-9]+/g, '-').replace(/^-+|-+$/g, '').slice(0, 31).replace(/-+$/, '');
  return f || 'pack';
}

// clip: an optional radar clip (js/radarvideo.js buildClip), written as radar.vid. sounds: optional alert sounds
// ({low, med, high}, js/alertsound.js), written as alert.snd. layout: optional layout.json text (js/layout.js toJson).
// media: the layout's font and picture files, [{ name: 'f0.fnt' | 'a3.art', data }] (js/layoutmedia.js packMedia).
export function buildPack(theme, emblem, clip = null, sounds = null, layout = null, media = []) {
  const dir = folderName(theme.name);
  const files = [{ path: dir + '/theme.json', data: new TextEncoder().encode(themeJson(theme)) }];
  if (theme.centrepiece === 'emblem') {
    if (!emblem) throw new Error('the emblem centrepiece needs a picture');
    files.push({ path: dir + '/emblem.bin', data: emblemToBin(emblem) });
  }
  if (clip) files.push({ path: dir + '/radar.vid', data: clipFile(clipForLayout(clip, layout)) });
  if (hasSounds(sounds)) files.push({ path: dir + '/alert.snd', data: soundFile(sounds) });
  for (const m of media || []) {
    if (!/^(f[0-7]\.fnt|a([0-9]|1[0-5])\.art)$/.test(m.name)) throw new Error('bad media file name: ' + m.name);
    files.push({ path: dir + '/' + m.name, data: m.data });
  }
  if (layout) files.push({ path: dir + '/layout.json', data: new TextEncoder().encode(layout) });
  return { files, bytes: files.reduce((n, f) => n + f.data.length, 0) };
}
