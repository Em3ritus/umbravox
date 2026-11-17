// The live preview: the device's own ui/ code compiled to WebAssembly
// (web/designer/wasm/bridge.cpp). This module only marshals bytes in and out;
// it does not touch the DOM, so Node can test it against the simulator.
import { buildPack } from './pack.js';
import { sweepId, startSweep } from './model.js';

// The boards the preview can draw. cyd: the 320x240 screen at half scale (the legacy canvas path). waveshare: the 3.5"
// 480x320 engine UI at full size (ui/preview_ws.h through uv_render_ws).
export const BOARDS = {
  cyd: { id: 'cyd', name: 'CYD 2.8" (320x240)', w: 160, h: 120, screens: ['scan', 'mesh', 'alert', 'menu'] },
  waveshare: { id: 'waveshare', name: 'Waveshare 3.5" (480x320)', w: 480, h: 320, screens: ['scan', 'mesh', 'alert', 'menu', 'aspect'] },
};
export const W = BOARDS.cyd.w, H = BOARDS.cyd.h;     // the original exports: the CYD
export const SCREENS = { scan: 0, mesh: 1, alert: 2, menu: 3, aspect: 4 };
export const ALERT_CONFS = { low: 0, med: 1, high: 2 };

// M is the instantiated emscripten module (the default export's factory, awaited).
export function createPreview(M) {
  let draft = null;          // {id, sweep} of the last pack the device code accepted

  const withBytes = (bytes, fn) => {
    const len = bytes ? bytes.length : 0;
    const ptr = len ? M._malloc(len) : 0;
    if (len) M.HEAPU8.set(bytes, ptr);
    try { return fn(ptr, len); } finally { if (len) M._free(ptr); }
  };

  const hasWs = typeof M._uv_render_ws === 'function';

  // opts: {board: 'cyd' (default) | 'waveshare', alertConf: 'low' | 'med' | 'high' (Waveshare only; default high)}.
  function paint(theme, screen, frame, sweep, opts = {}) {
    const board = opts.board ?? 'cyd';
    const b = BOARDS[board];
    if (!b || (board === 'waveshare' && !hasWs)) throw new Error('unknown board ' + board);
    if (!b.screens.includes(screen)) throw new Error(`the ${board} preview has no ${screen} screen`);
    const conf = ALERT_CONFS[opts.alertConf ?? 'high'];
    if (conf === undefined) throw new Error('unknown alertConf ' + opts.alertConf);
    const bytes = b.w * b.h * 4;
    const ptr = M._malloc(bytes);
    try {
      // Alert style is left to the theme: a pack plays the one rite its theme.json names.
      if (board === 'waveshare') M._uv_render_ws(theme, SCREENS[screen], frame, sweep, -1, conf, ptr);
      else M._uv_render(theme, SCREENS[screen], frame, sweep, -1, ptr);
      return M.HEAPU8.slice(ptr, ptr + bytes);
    } finally { M._free(ptr); }
  }

  return {
    // Try a draft. {ok:true, id} on success; {ok:false, error} otherwise, and the
    // previous good draft stays on screen.
    // clip: an optional radar clip (js/radarvideo.js buildClip), loaded with the pack exactly as the board reads radar.vid.
    // layout: optional layout.json text (js/layout.js toJson); refused by the device rules -> {ok:false, error}.
    loadDraft(theme, emblem, clip = null, layout = null) {
      let pack;
      try { pack = buildPack(theme, emblem, clip); } catch (e) { return { ok: false, error: e.message }; }
      const named = (n) => pack.files.find((f) => f.path.endsWith('/' + n));
      const json = named('theme.json').data;
      const emb = named('emblem.bin') ? named('emblem.bin').data : null;
      const vid = named('radar.vid') ? named('radar.vid').data : null;
      const lay = layout ? new TextEncoder().encode(layout) : null;
      const id = withBytes(json, (jp, jl) => withBytes(emb, (ep, el) => withBytes(vid, (cp, cl) => withBytes(lay, (lp, ll) => M._uv_load_pack(jp, jl, ep, el, cp, cl, lp, ll)))));
      if (id < 0) return { ok: false, error: M.UTF8ToString(M._uv_error()) };
      // Only ever a sweep the pack offers.
      const name = startSweep(theme);
      draft = { id, sweep: name === null ? -1 : sweepId(name) };
      return { ok: true, id };
    },
    hasDraft: () => draft !== null,
    // The boards this build of the wasm can draw ('waveshare' needs uv_render_ws).
    boards: () => (hasWs ? ['cyd', 'waveshare'] : ['cyd']),
    // One frame of the current draft as RGBA: 160x120 for the CYD, 480x320 for the Waveshare.
    renderDraft(screen, frame, opts) {
      if (!draft) throw new Error('no draft loaded');
      return paint(draft.id, screen, frame, draft.sweep, opts);
    },
    // The layout rules, from the device's own code (ui/themelayout.*): never retyped in JS.
    pieceNames() {
      const n = M._uv_piece_count();
      return Array.from({ length: n }, (_, i) => M.UTF8ToString(M._uv_piece_name(i)));
    },
    defaultRect(board, piece) {
      const b = board === 'waveshare' ? 0 : board === 'cyd' ? 1 : -1;
      const i = this.pieceNames().indexOf(piece);
      if (b < 0 || i < 0) throw new Error(`unknown board or piece: ${board} ${piece}`);
      const p = M._malloc(16);
      try {
        M._uv_layout_default(b, i, p);
        const v = new Int32Array(M.HEAPU8.buffer.slice(p, p + 16));
        return { x: v[0], y: v[1], w: v[2], h: v[3] };
      } finally { M._free(p); }
    },
    checkLayout(text) {
      const bytes = new TextEncoder().encode(text);
      const ok = withBytes(bytes, (p, l) => M._uv_layout_check(p, l));
      return ok ? { ok: true } : { ok: false, why: M.UTF8ToString(M._uv_error()) };
    },
    // One frame of a built-in (0 UMBRAVOX, 1 BAPH0MAU5, 2 COSMIC MUFFIN).
    renderBuiltin(id, screen, frame, opts) { return paint(id, screen, frame, -1, opts); },
  };
}

// Browser side: instantiate the module from its URL.
export async function loadWasm(url) {
  const factory = (await import(url)).default;
  return createPreview(await factory());
}
