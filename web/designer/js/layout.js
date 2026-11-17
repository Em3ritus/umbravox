// The Layout step's model: a theme's layout.json for both engine boards, edited piece by piece.
// DOM-free, so node:test covers it. The rules (piece names, defaults, the verdict) are the device's own, passed in as
// `rules` from js/preview.js (pieceNames, defaultRect, checkLayout): nothing here decides what the device accepts.
//
// L = { boards: { waveshare: {name: {rect, hidden}}, cyd: {...} }, master: 'waveshare'|'cyd'|null,
//       adjusted: { waveshare: [names], cyd: [names] } }
// A piece absent from a board stays where the device puts it by default. `hidden` keeps the last rect, so showing a
// piece again puts it back where it was.

export const SCREEN = { waveshare: { w: 480, h: 320 }, cyd: { w: 320, h: 240 } };
export const BOARD_NAMES = ['waveshare', 'cyd'];
// Pieces with no 44 px tap target (ui/themelayout.cpp tappable()).
const NOT_TAPPABLE = new Set(['wordmark', 'sd', 'radar', 'spectrum', 'channel', 'badge']);
// Pieces drawn at fixed offsets: never smaller than their built-in size (ui/themelayout.cpp); badge >= 24, spectrum >= 42x22.
const BUILT_IN_FLOOR = new Set(['wordmark', 'sd', 'channel']);

// An edit makes a fresh layout: clone() drops `raw` (the opened file's text) and `dropped`.
const clone = (L) => ({
  boards: { waveshare: { ...L.boards.waveshare }, cyd: { ...L.boards.cyd } },
  master: L.master,
  adjusted: { waveshare: [...L.adjusted.waveshare], cyd: [...L.adjusted.cyd] },
});

export function newLayout() {
  return { boards: { waveshare: {}, cyd: {} }, master: null, adjusted: { waveshare: [], cyd: [] } };
}

// rect {x,y,w,h} places the piece; 'hidden' hides it (keeping its last rect); null puts it back to the default.
// The radar is always square (side = the larger of w and h). The first board edited becomes the master; a piece
// edited on the other board is marked adjusted.
export function setPiece(L, board, name, rect) {
  const n = clone(L);
  const b = n.boards[board];
  if (!b) throw new Error('unknown board ' + board);
  if (rect === null) delete b[name];
  else if (rect === 'hidden') b[name] = { rect: b[name]?.rect ?? null, hidden: true };
  else {
    let { x, y, w, h } = rect;
    if (name === 'radar') { const s = Math.max(w, h); w = s; h = s; }
    b[name] = { rect: { x: Math.round(x), y: Math.round(y), w: Math.round(w), h: Math.round(h) }, hidden: false };
  }
  if (!n.master) n.master = board;
  else if (board !== n.master && !n.adjusted[board].includes(name)) n.adjusted[board].push(name);
  return n;
}

// Show a hidden piece again: back to its last rect, or to the default if it never had one.
export function showPiece(L, board, name) {
  const p = L.boards[board][name];
  if (!p || !p.hidden) return L;
  return p.rect ? setPiece(L, board, name, p.rect) : setPiece(L, board, name, null);
}

export const isEmpty = (L) => !L.raw && BOARD_NAMES.every((b) => Object.keys(L.boards[b]).length === 0);

// Where a piece is on a board: its placed rect, null when hidden, or the default.
export function rectOf(L, board, name, rules) {
  const p = L.boards[board][name];
  if (p && p.hidden) return null;
  if (p && p.rect) return p.rect;
  return rules.defaultRect(board, name);
}

// layout.json text: both boards always (the device never guesses a board), pieces in the device's order, only the
// ones that differ from the default. null when nothing is laid out (the pack then has no layout.json).
export function toJson(L, names) {
  if (L.raw) return L.raw;   // an opened file, untouched: the device rules judge exactly what the author opened
  if (isEmpty(L)) return null;
  const out = { format: 1 };
  for (const b of BOARD_NAMES) {
    const o = {};
    for (const n of names) {
      const p = L.boards[b][n];
      if (!p) continue;
      o[n] = p.hidden ? 'hidden' : [p.rect.x, p.rect.y, p.rect.w, p.rect.h];
    }
    out[b] = o;
  }
  return JSON.stringify(out);
}

// An opened layout.json. The text is kept as `raw` and is what toJson returns (and so what is checked and saved)
// until the first edit, so a file the device refuses is shown as refused, never quietly rewritten. Entries the editor
// cannot show are listed in `dropped` (names: the device's piece names, to report a misspelt one). Throws on a file
// that is not a JSON object.
const PLAIN = (v) => v !== null && typeof v === 'object' && !Array.isArray(v);
const BAD_KEYS = new Set(['__proto__', 'constructor', 'prototype']);
export function fromJson(text, names = null) {
  let j;
  try { j = JSON.parse(text); } catch (e) { throw new Error('layout.json is not valid JSON'); }
  if (!PLAIN(j)) throw new Error('layout.json is not a JSON object');
  const L = newLayout();
  L.raw = text; L.dropped = [];
  for (const b of BOARD_NAMES) {
    if (!(b in j)) continue;
    const o = j[b];
    if (!PLAIN(o)) { L.dropped.push(`${b} is not an object`); continue; }
    for (const n of Object.keys(o)) {
      const v = o[n];
      if (BAD_KEYS.has(n) || (names && !names.includes(n))) { L.dropped.push(`${b}: "${n}" is not a piece`); continue; }
      if (v === 'hidden') L.boards[b][n] = { rect: null, hidden: true };
      else if (Array.isArray(v) && v.length === 4 && v.every((x) => Number.isInteger(x) && Math.abs(x) < 32768))
        L.boards[b][n] = { rect: { x: v[0], y: v[1], w: v[2], h: v[3] }, hidden: false };
      else L.dropped.push(`${b}: ${n} is not [x, y, w, h] or "hidden"`);
    }
  }
  if (BOARD_NAMES.some((b) => Object.keys(L.boards[b]).length)) L.master = Object.keys(L.boards.waveshare).length ? 'waveshare' : 'cyd';
  return L;
}

// Before checking or saving: if one board is laid out and the other was never opened in the editor, draft it, so a
// saved pack always carries both boards (spec "Two boards"). A board the author opened is left as they left it.
export function withDrafts(L, rules, visited) {
  if (!L.master || L.raw) return L;
  const other = L.master === 'waveshare' ? 'cyd' : 'waveshare';
  if (visited.has(other) || Object.keys(L.boards[other]).length) return L;
  return draftOther(L, L.master, other, rules);
}

const overlaps = (a, b) => a && b && a.w > 0 && b.w > 0 && a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h;

// One coordinate, anchored by which third of the source screen the piece's centre is in: same (scaled) distance from
// the near edge, or the same (scaled) offset from the centre.
function anchor(pos, size, newSize, from, to, k) {
  const c = pos + size / 2;
  if (c < from / 3) return Math.round(pos * k);
  if (c > (2 * from) / 3) return to - Math.round((from - (pos + size)) * k) - newSize;
  return Math.round(to / 2 + (c - from / 2) * k - newSize / 2);
}

// Draft board `to` from board `from` (rules: {names, defaultRect}). Every piece placed or hidden on `from` is scaled
// by k = min(width ratio, height ratio) and anchored (above); tappable pieces never under 44, the device's size floors
// kept (wordmark, sd and channel at least built-in size; badge 24; spectrum 42x22); the radar stays square
// and within its default size on `to`; then clamped on screen; then a piece that overlaps one it did not overlap on
// `from` is nudged apart along the shorter axis (3 tries). Pieces untouched on `from` stay default on `to`.
export function draftOther(L, from, to, rules) {
  const F = SCREEN[from], T = SCREEN[to];
  const k = Math.min(T.w / F.w, T.h / F.h);
  const n = clone(L);
  n.boards[to] = {};
  n.adjusted[to] = [];
  const src = L.boards[from];
  for (const name of rules.names) {
    const p = src[name];
    if (!p) continue;
    if (p.hidden) { n.boards[to][name] = { rect: null, hidden: true }; continue; }
    const r = p.rect;
    // What the author changed is carried over, scaled; what they left at the built-in value takes the target board's
    // built-in value (so "the header moved to the bottom" drafts as exactly that, not as a squashed row).
    const sd = rules.defaultRect(from, name), td = rules.defaultRect(to, name);
    let w = r.w === sd.w ? td.w : Math.round(r.w * k), h = r.h === sd.h ? td.h : Math.round(r.h * k);
    if (name === 'radar') {
      const max = td.w;
      const s = r.w === sd.w ? max : Math.max(53, Math.min(max, Math.round(r.w * k)));
      w = s; h = s;
    } else if (!NOT_TAPPABLE.has(name)) { w = Math.max(44, w); h = Math.max(44, h); }
    else if (BUILT_IN_FLOOR.has(name)) { w = Math.max(td.w, w); h = Math.max(td.h, h); }
    else if (name === 'badge') { w = Math.max(24, w); h = Math.max(24, h); }
    else if (name === 'spectrum') { w = Math.max(42, w); h = Math.max(22, h); }
    let x = r.x === sd.x && r.w === sd.w ? td.x : anchor(r.x, r.w, w, F.w, T.w, k);
    let y = r.y === sd.y && r.h === sd.h ? td.y : anchor(r.y, r.h, h, F.h, T.h, k);
    x = Math.max(0, Math.min(T.w - w, x)); y = Math.max(0, Math.min(T.h - h, y));
    n.boards[to][name] = { rect: { x, y, w, h }, hidden: false };
  }
  // Nudge apart pieces that the scaling pushed together.
  for (let tries = 0; tries < 3; tries++) {
    let moved = false;
    for (const a of rules.names) {
      const pa = n.boards[to][a];
      if (!pa || pa.hidden) continue;
      for (const b of rules.names) {
        if (a === b) continue;
        const ra = n.boards[to][a].rect, rb = rectOf(n, to, b, rules);
        if (!overlaps(ra, rb)) continue;
        if (overlaps(rectOf(L, from, a, rules), rectOf(L, from, b, rules))) continue;   // they overlapped by design
        const dx = Math.min(ra.x + ra.w - rb.x, rb.x + rb.w - ra.x), dy = Math.min(ra.y + ra.h - rb.y, rb.y + rb.h - ra.y);
        const r = { ...ra };
        if (dx <= dy) r.x += (ra.x + ra.w / 2 < rb.x + rb.w / 2 ? -dx : dx);
        else r.y += (ra.y + ra.h / 2 < rb.y + rb.h / 2 ? -dy : dy);
        r.x = Math.max(0, Math.min(T.w - r.w, r.x)); r.y = Math.max(0, Math.min(T.h - r.h, r.y));
        n.boards[to][a] = { rect: r, hidden: false };
        moved = true;
      }
    }
    if (!moved) break;
  }
  if (!n.master) n.master = from;
  return n;
}
