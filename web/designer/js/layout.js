// The Layout step's model: a theme's layout.json for both engine boards, edited piece by piece.
// DOM-free, so node:test covers it. The rules (piece names, defaults, the verdict) are the device's own, passed in as
// `rules` from js/preview.js (pieceNames, defaultRect, checkLayout): nothing here decides what the device accepts.
//
// L = { boards: { waveshare: {name: {rect, hidden, style?}}, cyd: {...} }, master: 'waveshare'|'cyd'|null,
//       adjusted: { waveshare: [names], cyd: [names] }, style: { waveshare: {...}, cyd: {...} } }
// A piece's style is { font, scale, ink, fill, edge, frame, slice, bg }; the board style (the other screens) is
// { title, body, numbers, bg, panel, panelSlice, highlight: {fill, edge}, icons: [] }. Decor pieces are 'decor.0'..
// 'decor.7' in boards[board], each { rect, hidden: false, pic }. Fonts and pictures are SOURCE keys (a built-in font id,
// an uploaded file's key, or 'file:f3' / 'file:a5' from an opened pack); js/layoutmedia.js turns them into the
// pack's f0..f7.fnt / a0..a15.art and their names in layout.json.
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
  style: { waveshare: { ...(L.style?.waveshare || {}) }, cyd: { ...(L.style?.cyd || {}) } },
  cards: { waveshare: { ...(L.cards?.waveshare || {}) }, cyd: { ...(L.cards?.cyd || {}) } },
});

export const MAX_DECOR = 8;
export const isDecor = (name) => /^decor\.[0-7]$/.test(name);
export const decorNames = (L, board) => Object.keys(L.boards[board]).filter(isDecor).sort();

export function newLayout() {
  return { boards: { waveshare: {}, cyd: {} }, master: null, adjusted: { waveshare: [], cyd: [] }, style: { waveshare: {}, cyd: {} },
    cards: { waveshare: {}, cyd: {} } };
}

// ---- plan 5b: alert cards (overlays, one per level and board) and piece effects ----
// L.cards[board][level] = { rect: {x,y,w,h}, font?, scale?, pic? } (font and pic are source keys, as for pieces).
// No card for a level = the device's built-in card. Cards are not pieces: no hidden state, no tray, no z-order.
export const LEVELS = ['low', 'med', 'high'];
const CARD_MIN = { waveshare: { w: 240, h: 120 }, cyd: { w: 160, h: 96 } };   // ui/themelayout.cpp checkLook
// Merge `patch` (rect, font, scale, pic; null or '' removes a key) into a card; patch null removes the card.
export function setCard(L, board, level, patch) {
  if (!SCREEN[board]) throw new Error('unknown board ' + board);
  if (!LEVELS.includes(level)) throw new Error('unknown alert level ' + level);
  const n = clone(L), cards = n.cards[board];
  if (patch === null) delete cards[level];
  else {
    const c = { ...(cards[level] || {}) };
    for (const [k, v] of Object.entries(patch)) { if (v === null || v === undefined || v === '') delete c[k]; else c[k] = k === 'rect' ? { x: Math.round(v.x), y: Math.round(v.y), w: Math.round(v.w), h: Math.round(v.h) } : v; }
    if (!c.rect) throw new Error(`alert.card.${level} needs a rect`);
    cards[level] = c;
  }
  if (!n.master) n.master = board;
  return n;
}
// The card a level shows: the theme's, or the built-in one (rules.cardDefault, from the device code).
export function cardOf(L, board, level, rules) {
  const c = L.cards?.[board]?.[level];
  return c ? { ...c, builtin: false } : { rect: rules.cardDefault(board, level), builtin: true };
}
// Pieces on a board that carry an alert effect (a hidden piece draws nothing, so it does not count).
export const fxCount = (L, board) => Object.values(L.boards[board]).filter((p) => p && !p.hidden && p.style?.effect).length;

// Merge `patch` into a piece's style (a key set to null, undefined or '' is removed). A piece the author never placed
// is placed at `rect` (its default) so the style has a rect to go with.
export function setStyle(L, board, name, patch, rect) {
  const n = clone(L), b = n.boards[board];
  if (!b) throw new Error('unknown board ' + board);
  const cur = b[name] || { rect: rect ? { ...rect } : null, hidden: false };
  const style = { ...(cur.style || {}) };
  for (const [k, v] of Object.entries(patch)) {
    // 'none' and 'medium' are the device's defaults: not written
    if (v === null || v === undefined || v === '' || (k === 'effect' && v === 'none') || (k === 'speed' && v === 'medium')) delete style[k]; else style[k] = v;
  }
  b[name] = { ...cur, style };
  if (!b[name].rect && !b[name].hidden) throw new Error(`${name} needs a rect to carry a style`);
  if (!n.master) n.master = board;
  return n;
}
// The board's style block for the other screens, merged the same way.
export function setBoardStyle(L, board, patch) {
  const n = clone(L);
  const st = { ...n.style[board] };
  for (const [k, v] of Object.entries(patch)) { if (v === null || v === undefined || v === '') delete st[k]; else st[k] = v; }
  n.style[board] = st;
  if (!n.master) n.master = board;
  return n;
}
// A picture with no function, under every piece: the first free decor.N. Returns { L, name }.
export function addDecor(L, board, pic, rect) {
  const used = new Set(decorNames(L, board));
  let i = 0; while (i < MAX_DECOR && used.has('decor.' + i)) i++;
  if (i >= MAX_DECOR) throw new Error(`a board holds at most ${MAX_DECOR} pictures`);
  const n = clone(L), name = 'decor.' + i;
  n.boards[board][name] = { rect: { ...rect }, hidden: false, pic };
  if (!n.master) n.master = board;
  return { L: n, name };
}

// rect {x,y,w,h} places the piece; 'hidden' hides it (keeping its last rect); null puts it back to the default.
// The radar is always square (side = the larger of w and h). The first board edited becomes the master; a piece
// edited on the other board is marked adjusted.
export function setPiece(L, board, name, rect) {
  const n = clone(L);
  const b = n.boards[board];
  if (!b) throw new Error('unknown board ' + board);
  if (rect === null) delete b[name];
  else if (rect === 'hidden') { if (isDecor(name)) delete b[name]; else b[name] = { ...b[name], rect: b[name]?.rect ?? null, hidden: true }; }
  else {
    let { x, y, w, h } = rect;
    if (name === 'radar') { const s = Math.max(w, h); w = s; h = s; }
    b[name] = { ...b[name], rect: { x: Math.round(x), y: Math.round(y), w: Math.round(w), h: Math.round(h) }, hidden: false };   // keeps style / pic
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

export const isEmpty = (L) => !L.raw && BOARD_NAMES.every((b) => Object.keys(L.boards[b]).length === 0 && Object.keys(L.style?.[b] || {}).length === 0 &&
  Object.keys(L.cards?.[b] || {}).length === 0);

// Where a piece is on a board: its placed rect, null when hidden, or the default.
export function rectOf(L, board, name, rules) {
  const p = L.boards[board][name];
  if (p && p.hidden) return null;
  if (p && p.rect) return p.rect;
  return rules.defaultRect(board, name);
}

// layout.json text: both boards always (the device never guesses a board), pieces in the device's order, only the
// ones that differ from the default. null when nothing is laid out (the pack then has no layout.json).
// refs (from js/layoutmedia.js) names the pack files: refs.font(key) -> 'f0', refs.pic(key, board, w, h, opts) -> 'a0'.
// Without refs the source keys are written as they are (only meaningful for a layout with no fonts or pictures).
const SAME = { font: (k) => k, pic: (k) => k };
const rectArr = (r) => [r.x, r.y, r.w, r.h];
function pieceStyleOut(st, r, b, refs) {
  const o = {};
  if (st.font) o.font = refs.font(st.font);
  if (st.scale) o.scale = st.scale;
  for (const k of ['ink', 'fill', 'edge']) if (st[k]) o[k] = st[k];
  if (st.frame) { o.frame = refs.pic(st.frame, b, null, null, { frameFor: r }); o.slice = st.slice || [4, 4, 4, 4]; }
  if (st.bg) o.bg = refs.pic(st.bg, b, r.w, r.h, { transparent: false });
  if (st.effect) o.effect = st.effect;
  if (st.speed) o.speed = st.speed;
  return o;
}
function boardStyleOut(st, b, refs) {
  const o = {};
  for (const k of ['title', 'body', 'numbers']) if (st[k]) o[k] = refs.font(st[k]);
  if (st.bg) o.bg = refs.pic(st.bg, b, SCREEN[b].w, SCREEN[b].h, { transparent: false });
  if (st.panel) { o.panel = refs.pic(st.panel, b, null, null, { panel: true }); o.panelSlice = st.panelSlice || [4, 4, 4, 4]; }
  if (st.highlight && (st.highlight.fill || st.highlight.edge)) {
    o.highlight = {};
    if (st.highlight.fill) o.highlight.fill = st.highlight.fill;
    if (st.highlight.edge) o.highlight.edge = st.highlight.edge;
  }
  if (Array.isArray(st.icons) && st.icons.some(Boolean)) o.icons = st.icons.slice(0, 6).map((k) => (k ? refs.pic(k, b, null, null, { icon: true }) : null));
  return o;
}
export function toJson(L, names, refs = null) {
  if (L.raw) return L.raw;   // an opened file, untouched: the device rules judge exactly what the author opened
  if (isEmpty(L)) return null;
  const R = refs || SAME;
  const out = { format: 1 };
  for (const b of BOARD_NAMES) {
    const o = {};
    for (const n of names) {
      const p = L.boards[b][n];
      if (!p) continue;
      if (p.hidden) o[n] = 'hidden';
      else if (p.style && Object.keys(p.style).length) o[n] = { rect: rectArr(p.rect), ...pieceStyleOut(p.style, p.rect, b, R) };
      else o[n] = rectArr(p.rect);
    }
    const st = boardStyleOut(L.style?.[b] || {}, b, R);
    if (Object.keys(st).length) o.style = st;
    for (const d of decorNames(L, b)) {
      const p = L.boards[b][d];
      o[d] = { rect: rectArr(p.rect), art: R.pic(p.pic, b, p.rect.w, p.rect.h, { transparent: true, decor: true }) };
    }
    for (const lv of LEVELS) {
      const c = L.cards?.[b]?.[lv];
      if (!c) continue;
      const co = { rect: rectArr(c.rect) };
      if (c.font) co.font = R.font(c.font);
      if (c.scale) co.scale = c.scale;
      if (c.pic) co.clip = R.pic(c.pic, b, null, null, { cardFor: c.rect });
      o['alert.card.' + lv] = co;
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
      const isRect = (r) => Array.isArray(r) && r.length === 4 && r.every((x) => Number.isInteger(x) && Math.abs(x) < 32768);
      const toRect = (r) => ({ x: r[0], y: r[1], w: r[2], h: r[3] });
      const file = (stem) => (typeof stem === 'string' ? 'file:' + stem : undefined);
      if (n === 'style' && PLAIN(v)) {
        const st = {};
        for (const k of ['title', 'body', 'numbers', 'bg', 'panel']) if (typeof v[k] === 'string') st[k] = file(v[k]);
        if (Array.isArray(v.panelSlice)) st.panelSlice = v.panelSlice.slice(0, 4);
        if (PLAIN(v.highlight)) st.highlight = { ...(typeof v.highlight.fill === 'string' ? { fill: v.highlight.fill } : {}), ...(typeof v.highlight.edge === 'string' ? { edge: v.highlight.edge } : {}) };
        if (Array.isArray(v.icons)) st.icons = v.icons.slice(0, 6).map((k) => (typeof k === 'string' ? file(k) : null));
        L.style[b] = st;
        continue;
      }
      const lv = /^alert\.card\.(low|med|high)$/.exec(n);
      if (lv && PLAIN(v) && isRect(v.rect)) {
        const c = { rect: toRect(v.rect) };
        if (typeof v.font === 'string') c.font = file(v.font);
        if (Number.isInteger(v.scale)) c.scale = v.scale;
        if (typeof v.clip === 'string') c.pic = file(v.clip);
        L.cards[b][lv[1]] = c;
        continue;
      }
      if (isDecor(n) && PLAIN(v) && isRect(v.rect) && typeof v.art === 'string') { L.boards[b][n] = { rect: toRect(v.rect), hidden: false, pic: file(v.art) }; continue; }
      if (BAD_KEYS.has(n) || (names && !names.includes(n))) { L.dropped.push(`${b}: "${n}" is not a piece`); continue; }
      if (v === 'hidden') L.boards[b][n] = { rect: null, hidden: true };
      else if (isRect(v)) L.boards[b][n] = { rect: toRect(v), hidden: false };
      else if (PLAIN(v) && isRect(v.rect)) {
        const st = {};
        if (typeof v.font === 'string') st.font = file(v.font);
        if (Number.isInteger(v.scale)) st.scale = v.scale;
        for (const k of ['ink', 'fill', 'edge']) if (typeof v[k] === 'string') st[k] = v[k];
        if (typeof v.frame === 'string') { st.frame = file(v.frame); if (Array.isArray(v.slice)) st.slice = v.slice.slice(0, 4); }
        if (typeof v.bg === 'string') st.bg = file(v.bg);
        for (const k of ['effect', 'speed']) if (typeof v[k] === 'string') st[k] = v[k];
        L.boards[b][n] = { rect: toRect(v.rect), hidden: false, ...(Object.keys(st).length ? { style: st } : {}) };
      }
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
    if (p.hidden) { n.boards[to][name] = { rect: null, hidden: true, ...(p.style ? { style: p.style } : {}) }; continue; }
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
    n.boards[to][name] = { rect: { x, y, w, h }, hidden: false, ...(p.style ? { style: p.style } : {}) };
  }
  // Decor: scaled and anchored like a piece, no size floor; the picture is converted to the new size at pack time.
  for (const name of decorNames(L, from)) {
    const p = src[name], r = p.rect;
    const w = Math.max(1, Math.round(r.w * k)), h = Math.max(1, Math.round(r.h * k));
    const x = Math.max(0, Math.min(T.w - w, anchor(r.x, r.w, w, F.w, T.w, k)));
    const y = Math.max(0, Math.min(T.h - h, anchor(r.y, r.h, h, F.h, T.h, k)));
    n.boards[to][name] = { rect: { x, y, w, h }, hidden: false, pic: p.pic };
  }
  if (!Object.keys(n.style[to]).length) n.style[to] = { ...(L.style?.[from] || {}) };
  // Alert cards: scaled and anchored like a piece, then grown to the target board's minimum and kept on screen.
  n.cards[to] = {};
  for (const lv of LEVELS) {
    const c = L.cards?.[from]?.[lv];
    if (!c) continue;
    const r = c.rect, m = CARD_MIN[to];
    const w = Math.min(T.w, Math.max(m.w, Math.round(r.w * k))), h = Math.min(T.h, Math.max(m.h, Math.round(r.h * k)));
    const x = Math.max(0, Math.min(T.w - w, anchor(r.x, r.w, w, F.w, T.w, k)));
    const y = Math.max(0, Math.min(T.h - h, anchor(r.y, r.h, h, F.h, T.h, k)));
    n.cards[to][lv] = { ...c, rect: { x, y, w, h } };
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
