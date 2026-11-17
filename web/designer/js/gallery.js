// Opening a theme from the gallery (the themes site). Nothing here trusts the address bar or the index further than:
// a folder name that passes the device's folder rule, that the index lists exactly, whose files are among the four
// pack files, each within its size limit. Pure apart from the fetch function it is given.
import limits from './limits.json' with { type: 'json' };
import { THEMES_SITE, FOLDER_RE } from './share.js';

const FILE_LIMITS = { 'theme.json': limits.MAX_JSON, 'emblem.bin': limits.MAX_EMBLEM, 'radar.vid': limits.MAX_CLIP, 'alert.snd': limits.MAX_SOUND };
const MAX_INDEX = 1 << 20;
const FAIL = 'The gallery theme could not be loaded';

// "?theme=<folder>" from location.search -> the folder name, or null. Only that key is read.
export function themeParam(search) {
  const v = new URLSearchParams(String(search || '')).get('theme');
  return v && FOLDER_RE.test(v) ? v : null;
}

async function get(fetchFn, url, max, what) {
  const r = await fetchFn(url);
  if (!r || !r.ok) throw new Error(FAIL + ' (' + what + ': ' + (r ? r.status : 'no answer') + ').');
  const declared = Number(r.headers && r.headers.get && r.headers.get('content-length'));
  if (declared > max) throw new Error(what + ' is ' + declared + ' bytes; over the ' + max + ' byte limit, so it was not read.');
  const data = new Uint8Array(await r.arrayBuffer());
  if (data.length > max) throw new Error(what + ' is ' + data.length + ' bytes; over the ' + max + ' byte limit, so it was not used.');
  return data;
}

// -> {ok:true, files:[{name:'<folder>/<file>', data}], entry} | {ok:false, error}
export async function fetchGalleryTheme(folder, fetchFn = (u) => fetch(u), site = THEMES_SITE) {
  if (!FOLDER_RE.test(String(folder))) return { ok: false, error: 'That theme is not in the gallery.' };
  try {
    let index;
    try { index = JSON.parse(new TextDecoder().decode(await get(fetchFn, site + 'index.json', MAX_INDEX, 'the gallery list'))); }
    catch (e) { return { ok: false, error: /could not be loaded|limit/.test(e.message) ? e.message : FAIL + ' (the gallery list is not readable).' }; }
    const entry = index && Array.isArray(index.themes) ? index.themes.find((t) => t && t.folder === folder) : null;
    if (!entry) return { ok: false, error: 'That theme is not in the gallery.' };
    const names = entry.files;
    const known = Array.isArray(names) && names.length > 0 && names.every((n) => Object.hasOwn(FILE_LIMITS, n)) &&
      new Set(names).size === names.length && names.includes('theme.json');
    if (!known) return { ok: false, error: FAIL + ' (its file list is not a theme).' };
    const files = [];
    for (const n of names) files.push({ name: folder + '/' + n, data: await get(fetchFn, site + 'themes/' + folder + '/' + n, FILE_LIMITS[n], n) });
    return { ok: true, files, entry };
  } catch (e) {
    return { ok: false, error: /could not be loaded|limit/.test(e.message) ? e.message : FAIL + ' (' + e.message + ').' };
  }
}
