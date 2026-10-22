// Install planning: folder collisions and names. No DOM, no file access.
const lc = (s) => String(s).toLowerCase();

// The first free folder name: base, base-2, base-3, ... never past 31 chars.
// FAT32 ignores case, so "Midnight" and "midnight" are the same folder.
export function suffixedFolder(base, existing) {
  const taken = new Set(existing.map(lc));
  if (!taken.has(lc(base))) return base;
  for (let n = 2; ; n++) {
    const suf = '-' + n;
    const head = base.slice(0, 31 - suf.length).replace(/-+$/, '');
    const cand = (head || 'pack') + suf;
    if (!taken.has(lc(cand))) return cand;
  }
}

// The same files, moved to another folder. Inputs are not modified.
export function retargetFiles(files, folder) {
  return files.map((f) => ({ path: folder + '/' + f.path.split('/').slice(1).join('/'), data: f.data }));
}

// card: [{folder, name|null}] as read from /themes on the card.
export function planInstall(folder, themeName, card) {
  const hit = card.find((t) => lc(t.folder) === lc(folder)) || null;
  const others = card.filter((t) => lc(t.folder) !== lc(folder));
  const clash = others.find((t) => t.name !== null && t.name === themeName) || null;
  return {
    folder,
    themeName,
    collision: hit ? { folder: hit.folder, name: hit.name } : null,
    sameName: !!(hit && hit.name === themeName),
    alternative: hit ? suffixedFolder(folder, card.map((t) => t.folder)) : null,
    nameClash: clash ? { folder: clash.folder, name: clash.name } : null,
  };
}
