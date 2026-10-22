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
//
// Order: (1) remove an emblem.bin the new pack does not have, (2) write
// emblem.bin, (3) write theme.json LAST. The device only trusts a folder
// through its theme.json, so a failure part-way never leaves a NEW theme.json
// beside an OLD emblem; at worst the old theme.json meets the new emblem
// or nothing changed. Each write is aborted (no .crswap left) if it fails.
// state: 'untouched' (nothing on the card changed), 'unknown' (the folder may
// be incomplete or mixed), 'complete' (written and read back identical).
export async function installToCard(root, files) {
  const res = { ok: true, unsupported: false, verified: false, state: 'untouched', wrote: [], removed: [], mismatched: [], error: null, message: '' };
  if (!root) { res.ok = false; res.unsupported = true; return res; }
  const folders = [...new Set(files.map((f) => f.path.split('/')[0]))];
  const ordered = [...files].sort((a, b) => (a.path.endsWith('/theme.json') ? 1 : 0) - (b.path.endsWith('/theme.json') ? 1 : 0));
  try {
    const themes = await root.getDirectoryHandle('themes', { create: true });
    const dirs = new Map();
    for (const folder of folders) dirs.set(folder, await themes.getDirectoryHandle(folder, { create: true }));
    for (const [folder, d] of dirs) {
      if (!files.some((f) => f.path === folder + '/emblem.bin') && await exists(d, 'emblem.bin')) {
        res.state = 'unknown';
        await d.removeEntry('emblem.bin');
        res.removed.push('themes/' + folder + '/emblem.bin');
      }
    }
    for (const f of ordered) {
      const [folder, ...rest] = f.path.split('/');
      const fh = await dirs.get(folder).getFileHandle(rest.join('/'), { create: true });
      const w = await fh.createWritable();
      try {
        await w.write(f.data);
        await w.close();
      } catch (e) {
        try { await w.abort(); } catch (e2) { /* the first error is the one to report */ }
        throw e;
      }
      res.state = 'unknown';
      res.wrote.push('themes/' + f.path);
    }
    for (const f of ordered) {
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
    res.state = res.verified ? 'complete' : 'unknown';
  } catch (e) {
    res.ok = false; res.verified = false; res.error = (e && e.message) || String(e);
  }
  if (res.state === 'unknown') {
    res.message = 'The folder ' + folders.map((f) => '/themes/' + f).join(', ') + ' on the card may be incomplete or mixed (part new, part old). ' +
      'Run the install again, or delete that folder from the card.';
  } else if (res.state === 'untouched' && !res.ok) {
    res.message = 'Nothing on the card was changed.';
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
