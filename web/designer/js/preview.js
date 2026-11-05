// The live preview: the device's own ui/ code compiled to WebAssembly
// (web/designer/wasm/bridge.cpp). This module only marshals bytes in and out;
// it does not touch the DOM, so Node can test it against the simulator.
import { buildPack } from './pack.js';
import { sweepId, startSweep } from './model.js';
import { validateClip } from './radarvideo.js';

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
  let hasClip = false;

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
    loadDraft(theme, emblem) {
      let pack;
      try { pack = buildPack(theme, emblem); } catch (e) { return { ok: false, error: e.message }; }
      const json = pack.files[0].data;
      const emb = pack.files.length > 1 ? pack.files[1].data : null;
      const id = withBytes(json, (jp, jl) => withBytes(emb, (ep, el) => M._uv_load_pack(jp, jl, ep, el)));
      if (id < 0) return { ok: false, error: M.UTF8ToString(M._uv_error()) };
      // Only ever a sweep the pack offers.
      const name = startSweep(theme);
      draft = { id, sweep: name === null ? -1 : sweepId(name) };
      return { ok: true, id };
    },
    hasDraft: () => draft !== null,
    // A radar clip (js/radarvideo.js buildClip) drawn in the Waveshare radar, preview only: the CYD never shows it.
    // {ok:true}, or {ok:false, error} with the previous clip still playing.
    setRadarVideo(clip) {
      const why = validateClip(clip);
      if (why) return { ok: false, error: why };
      if (typeof M._uv_set_video !== 'function') return { ok: false, error: 'this build of the preview has no video support' };
      const rc = withBytes(clip.data, (p) => M._uv_set_video(p, clip.frames, clip.w, clip.h, clip.fps));
      if (rc !== 0) return { ok: false, error: 'the preview refused the clip (' + rc + ')' };
      hasClip = true;
      return { ok: true };
    },
    clearRadarVideo() { if (typeof M._uv_clear_video === 'function') M._uv_clear_video(); hasClip = false; },
    hasRadarVideo: () => hasClip,
    // The boards this build of the wasm can draw ('waveshare' needs uv_render_ws).
    boards: () => (hasWs ? ['cyd', 'waveshare'] : ['cyd']),
    // One frame of the current draft as RGBA: 160x120 for the CYD, 480x320 for the Waveshare.
    renderDraft(screen, frame, opts) {
      if (!draft) throw new Error('no draft loaded');
      return paint(draft.id, screen, frame, draft.sweep, opts);
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
