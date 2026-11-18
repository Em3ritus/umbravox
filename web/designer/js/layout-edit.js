// Pointer maths for the Layout step: page coordinates to device pixels, which piece is under the pointer, corner
// handles, drag and resize with snapping and clamping. DOM-free (node:test covers it); app.js only feeds it events.

// Drawing order, bottom to top: the radar group under the count tiles under the header and footer (a piece higher in
// this list is picked first). Within the radar group the badge, channel and spectrum draw over the radar.
export const Z_ORDER = ['radar', 'spectrum', 'channel', 'badge', 'count.wifi', 'count.5g', 'count.ble',
  'menu', 'wordmark', 'status.gps', 'status.nodes', 'status.power', 'bridge', 'sd',
  'nav.scan', 'nav.domain', 'nav.codex', 'nav.poi', 'stop'];

const SNAP = 2;
const snap = (d) => Math.trunc(d / SNAP) * SNAP;
const clamp = (v, lo, hi) => Math.max(lo, Math.min(hi, v));

// A pointer at (clientX, clientY) over a canvas drawn at canvasRect (getBoundingClientRect) showing a W x H screen.
export function toDevice(clientX, clientY, canvasRect, W, H) {
  const x = Math.round(((clientX - canvasRect.left) * W) / canvasRect.width);
  const y = Math.round(((clientY - canvasRect.top) * H) / canvasRect.height);
  return { x: clamp(x, 0, W - 1), y: clamp(y, 0, H - 1) };
}

// rects: {name: {x,y,w,h} | null (hidden)}. The topmost visible piece containing (x, y), or null.
export function pickPiece(rects, x, y) {
  for (let i = Z_ORDER.length - 1; i >= 0; i--) {
    const r = rects[Z_ORDER[i]];
    if (r && r.w > 0 && x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h) return Z_ORDER[i];
  }
  // Decor draws under every piece (decor.0 lowest), so it is picked last, topmost decor first.
  for (let i = 7; i >= 0; i--) {
    const r = rects['decor.' + i];
    if (r && r.w > 0 && x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h) return 'decor.' + i;
  }
  return null;
}

// The corner handle within `slop` px of (x, y): 'nw' | 'ne' | 'sw' | 'se' | null.
export function handleAt(r, x, y, slop = 8) {
  const near = (cx, cy) => Math.abs(x - cx) <= slop && Math.abs(y - cy) <= slop;
  if (near(r.x, r.y)) return 'nw';
  if (near(r.x + r.w, r.y)) return 'ne';
  if (near(r.x, r.y + r.h)) return 'sw';
  if (near(r.x + r.w, r.y + r.h)) return 'se';
  return null;
}

// The rect after dragging from `start` to `now` (device px). mode 'move' or a corner. Moves and edges snap to 2 px;
// the result stays on the bounds {w, h}; a resize never goes under 2 px (one snap step). opts.square keeps w == h
// (the radar), resizing away from the fixed corner.
export function dragTo(rect, start, now, mode, bounds, opts = {}) {
  const dx = snap(now.x - start.x), dy = snap(now.y - start.y);
  let { x, y, w, h } = rect;
  if (mode === 'move') {
    x = clamp(x + dx, 0, bounds.w - w);
    y = clamp(y + dy, 0, bounds.h - h);
    return { x, y, w, h };
  }
  const right = x + w, bottom = y + h;   // the opposite edges stay put for the corner being dragged
  if (mode === 'nw' || mode === 'sw') { const nx = clamp(x + dx, 0, right - SNAP); w = right - nx; x = nx; }
  else w = clamp(w + dx, SNAP, bounds.w - x);
  if (mode === 'nw' || mode === 'ne') { const ny = clamp(y + dy, 0, bottom - SNAP); h = bottom - ny; y = ny; }
  else h = clamp(h + dy, SNAP, bounds.h - y);
  if (opts.square) {
    const left = mode === 'nw' || mode === 'sw', top = mode === 'nw' || mode === 'ne';
    const room = Math.min(left ? right : bounds.w - rect.x, top ? bottom : bounds.h - rect.y);
    const s = clamp(Math.max(w, h), SNAP, room);
    w = s; h = s;
    x = left ? right - s : rect.x;
    y = top ? bottom - s : rect.y;
  }
  return { x, y, w, h };
}

// Arrow keys: 1 px, Shift: 8 px. Any other key: null.
export function nudge(r, key, shift) {
  const d = shift ? 8 : 1;
  const m = { ArrowLeft: [-d, 0], ArrowRight: [d, 0], ArrowUp: [0, -d], ArrowDown: [0, d] }[key];
  return m ? { x: r.x + m[0], y: r.y + m[1], w: r.w, h: r.h } : null;
}
