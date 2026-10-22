// Write a pack to the SD card through the File System Access API, then read
// every file back and compare bytes. A write that "succeeded" is not trusted.

export const fsAccessSupported = (g = globalThis) => typeof g.showDirectoryPicker === 'function';

const same = (a, b) => {
  if (a.length !== b.length) return false;
  for (let i = 0; i < a.length; i++) if (a[i] !== b[i]) return false;
  return true;
};

async function exists(dir, name) {
  try { await dir.getFileHandle(name); return true; } catch (e) { return false; }
}

// files: [{path: '<folder>/<file>', data: Uint8Array}] as buildPack returns.
// Never throws for card problems: the result says what happened.
export async function installToCard(root, files) {
  const res = { ok: true, unsupported: false, verified: false, wrote: [], removed: [], mismatched: [], error: null };
  if (!root) { res.ok = false; res.unsupported = true; return res; }
  try {
    const themes = await root.getDirectoryHandle('themes', { create: true });
    const dirs = new Map();
    for (const f of files) {
      const [folder, ...rest] = f.path.split('/');
      const name = rest.join('/');
      let d = dirs.get(folder);
      if (!d) { d = await themes.getDirectoryHandle(folder, { create: true }); dirs.set(folder, d); }
      const fh = await d.getFileHandle(name, { create: true });
      const w = await fh.createWritable();
      await w.write(f.data);
      await w.close();
      res.wrote.push('themes/' + f.path);
    }
    // An overwrite must not leave the old pack's emblem behind when this one has none.
    for (const [folder, d] of dirs) {
      if (!files.some((f) => f.path === folder + '/emblem.bin') && await exists(d, 'emblem.bin')) {
        await d.removeEntry('emblem.bin');
        res.removed.push('themes/' + folder + '/emblem.bin');
      }
    }
    // Read it all back.
    for (const f of files) {
      const [folder, ...rest] = f.path.split('/');
      let ok = false;
      try {
        const fh = await dirs.get(folder).getFileHandle(rest.join('/'));
        const back = new Uint8Array(await (await fh.getFile()).arrayBuffer());
        ok = same(back, f.data);
      } catch (e) { ok = false; }
      if (!ok) res.mismatched.push('themes/' + f.path);
    }
    res.verified = res.mismatched.length === 0;
  } catch (e) {
    res.ok = false; res.verified = false; res.error = (e && e.message) || String(e);
  }
  return res;
}

// What is already under /themes: [{folder, name|null}]. Names come from the
// card, so they are data: callers must put them in the page with textContent.
export async function readCardThemes(root) {
  let themes;
  try { themes = await root.getDirectoryHandle('themes'); } catch (e) { return []; }
  const out = [];
  for await (const entry of themes.values()) {
    if (entry.kind !== 'directory') continue;
    let name = null;
    try {
      const fh = await entry.getFileHandle('theme.json');
      const j = JSON.parse(await (await fh.getFile()).text());
      if (j && typeof j.name === 'string') name = j.name;
    } catch (e) { name = null; }
    out.push({ folder: entry.name, name });
  }
  return out.sort((a, b) => (a.folder < b.folder ? -1 : 1));
}

// Does this folder look like an UMBRAVOX card? (diag.log or a WiGLE CSV at the top.)
export async function inspectCard(root) {
  const found = [];
  for await (const entry of root.values()) {
    if (entry.kind !== 'file') continue;
    if (entry.name === 'diag.log' || /\.csv$/i.test(entry.name)) found.push('/' + entry.name);
  }
  return { looksLikeCard: found.length > 0, found };
}
