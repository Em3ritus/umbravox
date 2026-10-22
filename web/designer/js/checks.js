// Everything the designer can tell the user about a theme before it is built,
// using the same rules as the firmware (ui/themepack.cpp, ui/contrast.h).
import limits from './limits.json' with { type: 'json' };
import { ROLES, isHex, toRgb565, contrastX10, contrastRatio, fmtRatio } from './palette.js';
import { themeJson } from './pack.js';

const ok = (text) => ({ level: 'ok', text });
const warn = (text) => ({ level: 'warn', text });
const error = (text) => ({ level: 'error', text });

function checkString(label, s, max, minLen, out) {
  if (/["\\]/.test(s)) out.push(error(label + ' cannot contain a double quote or a backslash.'));
  if (s.length > max) out.push(error(label + ' is ' + s.length + ' characters; the limit is ' + max + '.'));
  else if (s.length < minLen) out.push(error(label + ' must be at least ' + minLen + ' character' + (minLen > 1 ? 's' : '') + '.'));
}

export function runChecks(theme, emblem) {
  const out = [];
  const t = theme || {};
  const c = t.colors || {};

  // colours
  const bad = ROLES.filter((r) => !isHex(c[r]));
  if (bad.length) out.push(error('Colours missing or not #RRGGBB: ' + bad.join(', ') + '.'));
  else out.push(ok('All ' + ROLES.length + ' colours are set.'));
  const have = (...rs) => rs.every((r) => isHex(c[r]));
  const ratio = (a, b) => contrastRatio(c[a], c[b]);

  if (have('text', 'panel')) {
    const r = ratio('text', 'panel');
    if (r < 3) out.push(error('Text on panel is ' + fmtRatio(r) + '; it is unreadable. Use at least 4.5:1.'));
    else if (r < 4.5) out.push(warn('Text on panel is ' + fmtRatio(r) + '; below the recommended 4.5:1.'));
    else out.push(ok('Text on panel is ' + fmtRatio(r) + '.'));
  }
  if (have('dim', 'panel')) {
    const r = ratio('dim', 'panel');
    if (r < 3) out.push(warn('Dim text on panel is ' + fmtRatio(r) + '; labels may be hard to read (aim for 3.0:1 or more).'));
    else out.push(ok('Dim text on panel is ' + fmtRatio(r) + '.'));
  }

  // the device's rule: 3:1 against void AND panel, measured on the RGB565 colours
  if (have('void', 'panel')) {
    const need = limits.MIN_EDGE_CONTRAST_X10;
    const edge = (role, what, remedy) => {
      if (!isHex(c[role])) return;
      const v = toRgb565(c[role]);
      const rv = contrastX10(v, toRgb565(c.void)), rp = contrastX10(v, toRgb565(c.panel));
      if (rv < need || rp < need) {
        out.push(warn(role + ' (' + what + ') is only ' + fmtRatio(Math.min(rv, rp) / 10) + ' against ' +
          (rv <= rp ? 'void' : 'panel') + '; it needs ' + fmtRatio(need / 10) + '. The device will substitute ' + remedy + '.'));
      } else out.push(ok(role + ' (' + what + ') reads against void and panel.'));
    };
    edge('amber', 'status and medium-confidence alert edge', 'a safe colour (#F59E0B)');
    edge('red', 'status and high-confidence alert edge', 'a safe colour (#EF4444)');
    edge('witch', 'low-confidence alert edge', 'a safe colour (#D6C2FF) for the alert edge');
  }

  // centrepiece
  if (t.centrepiece === 'emblem') {
    if (!emblem) out.push(error('The emblem centrepiece needs a picture.'));
    else {
      const D = limits.MAX_EMBLEM_DIM, C = limits.MAX_EMBLEM_COLORS;
      if (emblem.w > D || emblem.h > D || emblem.w < 1 || emblem.h < 1)
        out.push(error('The emblem is ' + emblem.w + 'x' + emblem.h + '; the limit is ' + D + 'x' + D + '.'));
      else out.push(ok('Emblem is ' + emblem.w + 'x' + emblem.h + '.'));
      if (emblem.ncol > C || emblem.ncol < 2) out.push(error('The emblem has ' + emblem.ncol + ' colours; it needs 2 to ' + C + ' colours (at most 16 colours).'));
      else out.push(ok('Emblem uses ' + emblem.ncol + ' colours.'));
    }
  }

  // header
  const hd = typeof t.header === 'string' ? t.header : '';
  if (hd) {
    checkString('Header', hd, limits.MAX_HEADER, 1, out);
    if (/[^\x20-\x7e]/.test(hd) || /[{|}~`]/.test(hd)) out.push(error('Header has a character the device font lacks (use ASCII 32..95: capitals, digits, space and punctuation).'));
    if (/[a-z]/.test(hd)) out.push(warn('Header has lowercase letters; the device shows them as capitals.'));
  }

  // name
  const nm = typeof t.name === 'string' ? t.name : '';
  checkString('Name', nm, limits.MAX_NAME, 1, out);
  if (nm && !/^[A-Z0-9 .-]*$/.test(nm)) out.push(error('Name may only use ' + limits.nameChars + ' (capital letters).'));
  if (limits.reservedNames.includes(nm)) out.push(error('Name "' + nm + '" is reserved: it is a built-in theme\'s name, so the device would skip this pack.'));

  // author
  const au = typeof t.author === 'string' ? t.author : '';
  checkString('Author', au, limits.MAX_AUTHOR, 0, out);
  if (/[^\x20-\x7e]/.test(au)) out.push(error('Author must be printable ASCII.'));

  // sweeps and the other presets
  const sweeps = Array.isArray(t.sweeps) ? t.sweeps : [];
  if (!sweeps.length) out.push(error('Offer at least one sweep.'));
  for (const s of sweeps) if (!limits.sweepNames.includes(s)) out.push(error('Unknown sweep "' + s + '".'));
  if (sweeps.length && t.sweep && !sweeps.includes(t.sweep)) out.push(error('The starting sweep "' + t.sweep + '" must be one of the sweeps offered.'));
  if (t.dial && !limits.dialNames.includes(t.dial)) out.push(error('Dial must be compact or large.'));
  if (t.alert && !limits.alertNames.includes(t.alert)) out.push(error('Unknown alert "' + t.alert + '".'));
  if (t.centrepiece && !limits.centrepieceNames.includes(t.centrepiece)) out.push(error('Centrepiece must be heptagram or emblem.'));

  // sizes
  let json = null;
  try { json = new TextEncoder().encode(themeJson(t)).length; } catch (e) { json = null; }
  const emb = t.centrepiece === 'emblem' && emblem ? 8 + 2 * emblem.ncol + emblem.w * emblem.h : 0;
  if (json !== null) {
    if (json > limits.MAX_JSON) out.push(error('theme.json is ' + json + ' bytes; the limit is ' + limits.MAX_JSON + '.'));
    const total = json + emb;
    if (total > limits.MAX_PACK) out.push(error('The pack is ' + total + ' bytes; the limit is ' + limits.MAX_PACK + ' (64 KB).'));
    else out.push(ok('Pack size ' + total + ' bytes of ' + limits.MAX_PACK + ' (64 KB).'));
  }
  if (emb > limits.MAX_EMBLEM) out.push(error('emblem.bin is ' + emb + ' bytes; the limit is ' + limits.MAX_EMBLEM + '.'));

  out.push(ok('The device loads at most ' + limits.MAX_PACKS + ' packs (the first ' + limits.MAX_PACKS + ' by folder name).'));
  return out;
}
