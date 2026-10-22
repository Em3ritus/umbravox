// The live preview: the device's own ui/ code compiled to WebAssembly
// (web/designer/wasm/bridge.cpp). This module only marshals bytes in and out;
// it does not touch the DOM, so Node can test it against the simulator.
import { buildPack } from './pack.js';
import { sweepId, startSweep } from './model.js';

export const W = 160, H = 120;
export const SCREENS = { scan: 0, mesh: 1, alert: 2, menu: 3 };

// M is the instantiated emscripten module (the default export's factory, awaited).
export function createPreview(M) {
  const n = W * H * 4;
  let draft = null;          // {id, sweep} of the last pack the device code accepted

  const withBytes = (bytes, fn) => {
    const len = bytes ? bytes.length : 0;
    const ptr = len ? M._malloc(len) : 0;
    if (len) M.HEAPU8.set(bytes, ptr);
    try { return fn(ptr, len); } finally { if (len) M._free(ptr); }
  };

  function paint(theme, screen, frame, sweep) {
    const ptr = M._malloc(n);
    try {
      // Alert style is left to the theme: a pack plays the one rite its theme.json names.
      M._uv_render(theme, SCREENS[screen] ?? 0, frame, sweep, -1, ptr);
      return M.HEAPU8.slice(ptr, ptr + n);
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
    // One frame of the current draft as 160x120 RGBA.
    renderDraft(screen, frame) {
      if (!draft) throw new Error('no draft loaded');
      return paint(draft.id, screen, frame, draft.sweep);
    },
    // One frame of a built-in (0 UMBRAVOX, 1 BAPH0MAU5, 2 COSMIC MUFFIN).
    renderBuiltin(id, screen, frame) { return paint(id, screen, frame, -1); },
  };
}

// Browser side: instantiate the module from its URL.
export async function loadWasm(url) {
  const factory = (await import(url)).default;
  return createPreview(await factory());
}
