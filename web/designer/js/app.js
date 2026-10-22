// DOM glue for the designer. The rules live in model.js, plan.js, limitations.js,
// install.js and preview.js; this file only wires them to the page.
// User-supplied strings (theme names, folder names read from the card) only ever
// go in through textContent.
import limits from './limits.json' with { type: 'json' };
import { ROLES, isHex, fromAccent } from './palette.js';
import { runChecks } from './checks.js';
import { buildPack, folderName } from './pack.js';
import { makeZip } from './zip.js';
import { imageToEmblem } from './emblem.js';
import {
  TEMPLATES, makeTemplate, blankTheme, sanitizeText, sanitizeName, themeFromState, openPackFiles, startSweep,
} from './model.js';
import { limitsFor, placeLimitations, renderLimitList, DESIGN_PLACES } from './limitations.js';
import { planInstall, retargetFiles } from './plan.js';
import { installToCard, fsAccessSupported, readCardThemes, inspectCard } from './install.js';
import { loadWasm, W, H } from './preview.js';

const $ = (id) => document.getElementById(id);
const state = { theme: null, emblem: null, tab: 'scan', view: 'start', card: null, lastPack: null };
let preview = null;       // null until the wasm loads (or if it cannot)

function el(tag, props = {}, ...kids) {
  const e = document.createElement(tag);
  for (const [k, v] of Object.entries(props)) {
    if (k === 'class') e.className = v; else if (k === 'text') e.textContent = v; else e.setAttribute(k, v);
  }
  for (const k of kids) e.append(k);
  return e;
}
const clear = (n) => { while (n.firstChild) n.removeChild(n.firstChild); };

// ---------------------------------------------------------------- views
function show(view) {
  state.view = view;
  for (const s of document.querySelectorAll('.view')) s.hidden = s.dataset.view !== view;
  for (const b of document.querySelectorAll('.step')) {
    if (b.dataset.view === view) b.setAttribute('aria-current', 'page'); else b.removeAttribute('aria-current');
    if (b.dataset.view !== 'start') b.disabled = !state.theme;
  }
  if (view === 'install') enterInstall();
}
for (const b of document.querySelectorAll('.step')) b.addEventListener('click', () => show(b.dataset.view));

// ---------------------------------------------------------------- canvas painting
function paint(canvas, rgba) {
  canvas.getContext('2d').putImageData(new ImageData(new Uint8ClampedArray(rgba.buffer, rgba.byteOffset, rgba.length), W, H), 0, 0);
}
function paintEmblem(canvas, e) {
  const ctx = canvas.getContext('2d');
  canvas.width = e.w; canvas.height = e.h;
  const img = ctx.createImageData(e.w, e.h);
  const pal = e.pal565.map((c) => { const r5 = c >> 11, g6 = (c >> 5) & 63, b5 = c & 31; return [(r5 << 3) | (r5 >> 2), (g6 << 2) | (g6 >> 4), (b5 << 3) | (b5 >> 2)]; });
  e.px.forEach((i, p) => { img.data.set([...pal[i], 255], p * 4); });
  ctx.putImageData(img, 0, 0);
}

// ---------------------------------------------------------------- limitations (text from limits.json only)
function fillLimits(view, places) {
  const placed = placeLimitations(limits, view, places);
  for (const host of document.querySelectorAll('[data-limits]')) {
    if (host.closest('.view').dataset.view === view) host.innerHTML = renderLimitList(placed[host.dataset.limits] || []);   // trusted limits.json text, escaped
  }
}

// ---------------------------------------------------------------- start
function buildStart() {
  const host = $('templates');
  TEMPLATES.forEach((t, i) => {
    const c = el('canvas', { width: W, height: H });
    const b = el('button', { type: 'button', class: 'tpl' }, c, el('div', { class: 'nm', text: t.label }), el('div', { class: 'bl', text: t.blurb }));
    b.addEventListener('click', () => { const m = makeTemplate(t.id); begin(m.theme, m.emblem); });
    host.append(b);
    if (preview) paint(c, preview.renderBuiltin(i, 'scan', 24)); else c.hidden = true;
  });
}
$('btn-blank').addEventListener('click', () => begin(blankTheme(), null));

function begin(theme, emblem) {
  state.theme = theme; state.emblem = emblem; state.card = null;
  $('open-msg').textContent = '';
  syncForm(); changed(); show('design');
}

// open an existing theme: files, folder or drop
async function readFiles(list) {
  const out = [];
  for (const f of list) out.push({ name: f.webkitRelativePath || f.name, data: new Uint8Array(await f.arrayBuffer()) });
  return out;
}
async function entriesToFiles(items) {
  const files = [];
  const walk = async (entry, depth) => {
    if (entry.isFile) files.push(await new Promise((res, rej) => entry.file(res, rej)));
    else if (entry.isDirectory && depth < 2) {
      const r = entry.createReader();
      for (;;) {
        const batch = await new Promise((res, rej) => r.readEntries(res, rej));
        if (!batch.length) break;
        for (const e of batch) await walk(e, depth + 1);
      }
    }
  };
  for (const it of items) { const e = it.webkitGetAsEntry && it.webkitGetAsEntry(); if (e) await walk(e, 0); }
  return files;
}
async function openThese(fileList) {
  const msg = $('open-msg'); msg.className = 'msg';
  try {
    const r = openPackFiles(await readFiles(fileList));
    if (!r.ok) { msg.className = 'msg err'; msg.textContent = r.error; return; }
    begin(r.theme, r.emblem);
    if (r.warnings.length) { const m = $('action-msg'); m.className = 'msg warn'; m.textContent = 'Opened with changes: ' + r.warnings.join(' '); }
  } catch (e) { msg.className = 'msg err'; msg.textContent = 'Could not read those files: ' + e.message; }
}
$('btn-open-files').addEventListener('click', () => $('in-files').click());
$('btn-open-folder').addEventListener('click', () => $('in-folder').click());
$('in-files').addEventListener('change', (e) => openThese([...e.target.files]));
$('in-folder').addEventListener('change', (e) => openThese([...e.target.files]));
const drop = $('drop');
drop.addEventListener('click', () => $('in-files').click());
drop.addEventListener('keydown', (e) => { if (e.key === 'Enter' || e.key === ' ') $('in-files').click(); });
drop.addEventListener('dragover', (e) => { e.preventDefault(); drop.classList.add('over'); });
drop.addEventListener('dragleave', () => drop.classList.remove('over'));
drop.addEventListener('drop', async (e) => {
  e.preventDefault(); drop.classList.remove('over');
  const items = [...e.dataTransfer.items];
  const files = items.some((i) => i.webkitGetAsEntry) ? await entriesToFiles(items) : [...e.dataTransfer.files];
  openThese(files.length ? files : [...e.dataTransfer.files]);
});

// ---------------------------------------------------------------- design: form
const GROUPS = [
  ['Background', [['void', 'void'], ['panel', 'panel'], ['grid', 'grid']]],
  ['Lines', [['edge', 'edge'], ['edgeLit', 'edge lit']]],
  ['Text', [['dim', 'dim'], ['text', 'text'], ['bright', 'bright']]],
  ['Chrome', [['witch', 'accent'], ['witchHi', 'accent hot'], ['bone', 'ceremony']]],
  ['Centrepiece alight', [['live', 'alight'], ['liveHi', 'alight hot']]],
  ['The search', [['sweep', 'sweep'], ['sweepHi', 'sweep edge']]],
  ['Data', [['magenta', '2.4 GHz'], ['ice', '5 GHz'], ['green', 'BLE']]],
  ['Status', [['amber', 'warning'], ['red', 'danger']]],
];
const colorInputs = {};

function buildPalette() {
  const host = $('palette');
  for (const [title, roles] of GROUPS) {
    host.append(el('div', { class: 'pal-group', text: title }));
    for (const [role, label] of roles) {
      const pick = el('input', { type: 'color', 'aria-label': label + ' colour' });
      const txt = el('input', { type: 'text', maxlength: '7', spellcheck: 'false', 'aria-label': label + ' hex' });
      pick.addEventListener('input', () => setColor(role, pick.value.toUpperCase()));
      txt.addEventListener('input', () => {
        const v = txt.value.startsWith('#') ? txt.value : '#' + txt.value;
        if (isHex(v)) { txt.classList.remove('bad'); setColor(role, v.toUpperCase(), txt); } else txt.classList.add('bad');
      });
      colorInputs[role] = { pick, txt };
      host.append(el('div', { class: 'pal' }, pick, el('span', { text: label }), txt));
    }
  }
  const sw = $('sweeps');
  for (const s of limits.sweepNames) {
    const cb = el('input', { type: 'checkbox', 'data-sweep': s });
    cb.addEventListener('change', onSweeps);
    sw.append(el('label', {}, cb, el('span', { text: s[0].toUpperCase() + s.slice(1) })));
  }
  for (const a of limits.alertNames) $('f-alert').append(el('option', { value: a, text: a[0].toUpperCase() + a.slice(1) }));
}
function setColor(role, hex, from) {
  state.theme.colors[role] = hex;
  const c = colorInputs[role];
  if (from !== c.txt) { c.txt.value = hex; c.txt.classList.remove('bad'); }
  c.pick.value = hex.toLowerCase();
  changed();
}
function syncForm() {
  const t = state.theme;
  $('f-name').value = t.name; $('f-author').value = t.author || ''; $('f-header').value = t.header || '';
  for (const r of ROLES) { colorInputs[r].pick.value = t.colors[r].toLowerCase(); colorInputs[r].txt.value = t.colors[r]; colorInputs[r].txt.classList.remove('bad'); }
  for (const b of $('seg-centre').children) b.setAttribute('aria-pressed', String(b.dataset.v === t.centrepiece));
  for (const b of $('seg-dial').children) b.setAttribute('aria-pressed', String(b.dataset.v === t.dial));
  for (const cb of $('sweeps').querySelectorAll('input')) cb.checked = t.sweeps.includes(cb.dataset.sweep);
  $('f-alert').value = t.alert;
  syncSweepSelect(); syncEmblemBox();
}
function syncSweepSelect() {
  const t = state.theme, sel = $('f-sweep');
  clear(sel);
  for (const s of limits.sweepNames) if (t.sweeps.includes(s)) sel.append(el('option', { value: s, text: s[0].toUpperCase() + s.slice(1) }));
  const cur = startSweep(t);
  if (cur) { t.sweep = cur; sel.value = cur; } else delete t.sweep;
}
function syncEmblemBox() {
  const t = state.theme, box = $('emblem-box');
  box.hidden = t.centrepiece !== 'emblem';
  const info = $('emblem-info');
  if (state.emblem) {
    paintEmblem($('emblem-canvas'), state.emblem);
    info.textContent = state.emblem.w + 'x' + state.emblem.h + '   ' + state.emblem.ncol + ' colours';
    $('btn-image').textContent = 'Replace image';
  } else {
    const c = $('emblem-canvas'); c.getContext('2d').clearRect(0, 0, c.width, c.height);
    info.textContent = 'No picture yet.'; $('btn-image').textContent = 'Choose image';
  }
}

function textField(id, key, fn) {
  const inp = $(id);
  inp.addEventListener('input', () => {
    const r = fn(inp.value);
    if (r.value !== inp.value) inp.value = r.value;
    state.theme[key] = r.value;
    const note = $('id-note');
    note.textContent = r.stripped ? 'A double quote or backslash was removed: the device reads no escapes, so a pack with either would be skipped.' : '';
    changed();
  });
}
textField('f-name', 'name', sanitizeName);
textField('f-author', 'author', sanitizeText);
textField('f-header', 'header', sanitizeName);

$('btn-accent').addEventListener('click', () => $('in-accent').click());
$('in-accent').addEventListener('change', (e) => {
  if (!window.confirm('Replace all 20 colours with a palette made from ' + e.target.value.toUpperCase() + '?')) return;
  state.theme.colors = fromAccent(e.target.value.toUpperCase());
  syncForm(); changed();
});
for (const b of $('seg-centre').children) b.addEventListener('click', () => {
  state.theme.centrepiece = b.dataset.v; syncForm(); changed();
});
for (const b of $('seg-dial').children) b.addEventListener('click', () => { state.theme.dial = b.dataset.v; syncForm(); changed(); });
function onSweeps() {
  state.theme.sweeps = limits.sweepNames.filter((s) => $('sweeps').querySelector('[data-sweep="' + s + '"]').checked);
  syncSweepSelect(); changed();
}
$('f-sweep').addEventListener('change', (e) => { state.theme.sweep = e.target.value; changed(); });
$('f-alert').addEventListener('change', (e) => { state.theme.alert = e.target.value; changed(); });

$('btn-image').addEventListener('click', () => $('in-image').click());
$('in-image').addEventListener('change', async (e) => {
  const f = e.target.files[0]; e.target.value = '';
  const msg = $('emblem-msg'); msg.className = 'msg'; msg.textContent = '';
  if (!f) return;
  try {
    const bmp = await createImageBitmap(f);
    const k = Math.min(1, 1024 / Math.max(bmp.width, bmp.height));
    const w = Math.max(1, Math.round(bmp.width * k)), h = Math.max(1, Math.round(bmp.height * k));
    const cv = document.createElement('canvas'); cv.width = w; cv.height = h;
    const ctx = cv.getContext('2d'); ctx.drawImage(bmp, 0, 0, w, h);
    state.emblem = imageToEmblem(ctx.getImageData(0, 0, w, h).data, w, h);
    state.theme.centrepiece = 'emblem';
    syncForm(); changed();
  } catch (err) { msg.className = 'msg err'; msg.textContent = 'Could not read that image: ' + err.message; }
});

// ---------------------------------------------------------------- design: preview + checks
let draftTimer = 0, checksNow = [];
function changed() {
  clearTimeout(draftTimer);
  draftTimer = setTimeout(refresh, 80);
  refreshChecks();
}
function currentTheme() { return themeFromState(state); }
function refreshChecks() {
  const t = currentTheme();
  checksNow = runChecks(t, state.emblem);
  const ul = $('checks'); clear(ul);
  for (const c of checksNow) {
    ul.append(el('li', { class: c.level }, el('span', { class: 'g', text: c.level === 'ok' ? 'v' : c.level === 'warn' ? '!' : 'x' }), el('span', { text: c.text })));
  }
  const bad = checksNow.some((c) => c.level === 'error');
  for (const id of ['btn-download', 'btn-install']) { $(id).disabled = bad; $(id).title = bad ? 'Fix the errors in the checks first.' : ''; }
}
function refresh() {
  if (!preview || !state.theme) return;
  const r = preview.loadDraft(currentTheme(), state.emblem);
  const m = $('preview-msg');
  if (r.ok) { m.className = 'msg'; m.textContent = ''; }
  else { m.className = 'msg warn'; m.textContent = 'The preview shows the last version the device would accept. This one is rejected: ' + r.error; }
}
for (const b of $('tabs').children) b.addEventListener('click', () => { state.tab = b.dataset.v; syncTabs(); t0 = performance.now(); });
function syncTabs() { for (const b of $('tabs').children) b.setAttribute('aria-selected', String(b.dataset.v === state.tab)); }

let t0 = performance.now(), lastDraw = 0;
function tick(now) {
  requestAnimationFrame(tick);
  if (!preview || !preview.hasDraft() || state.view !== 'design' || document.hidden || now - lastDraw < 40) return;
  lastDraw = now;
  const ms = now - t0;
  const frame = state.tab === 'alert' ? Math.floor((ms % 2500) / 20) : Math.floor(ms / 20);
  paint($('preview'), preview.renderDraft(state.tab, frame));
}

// ---------------------------------------------------------------- build + download
function currentPack() { return buildPack(currentTheme(), state.emblem); }
function download() {
  try {
    const pack = currentPack();
    const folder = pack.files[0].path.split('/')[0];
    const blob = new Blob([makeZip(pack.files)], { type: 'application/zip' });
    const a = el('a', { href: URL.createObjectURL(blob), download: folder + '.zip' });
    document.body.append(a); a.click(); a.remove();
    setTimeout(() => URL.revokeObjectURL(a.href), 10000);
    const m = $('action-msg'); m.className = 'msg';
    m.textContent = 'Downloaded ' + folder + '.zip. Unzip it and copy the ' + folder + ' folder into the themes folder on the SD card.';
  } catch (e) { const m = $('action-msg'); m.className = 'msg err'; m.textContent = e.message; }
}
$('btn-download').addEventListener('click', download);
$('btn-zip2').addEventListener('click', download);
$('btn-share').addEventListener('click', () => {
  const m = $('action-msg'); m.className = 'msg'; m.textContent = 'Sharing arrives in a later release. For now, download the pack and send the zip to a friend.';
});
$('btn-install').addEventListener('click', () => show('install'));

// ---------------------------------------------------------------- install
function logLine(text, cls = '') { $('log').append(el('li', { class: cls, text })); }
function enterInstall() {
  let pack;
  try { pack = currentPack(); } catch (e) { show('design'); return; }
  state.lastPack = pack;
  if (preview && preview.hasDraft()) paint($('install-preview'), preview.renderDraft('scan', 24));
  const folder = pack.files[0].path.split('/')[0];
  $('install-info').textContent = '/themes/' + folder + '/   ' + pack.files.length + ' file' + (pack.files.length > 1 ? 's' : '') + '   ' + (pack.bytes / 1024).toFixed(1) + ' KB';
  clear($('log')); $('collide').hidden = true; $('btn-write').disabled = !state.card;
  clear($('done')); $('done').append(el('p', { class: 'muted', text: 'Not yet.' })); $('done-head').className = 'num';
  const ok = fsAccessSupported();
  $('btn-pick').disabled = !ok;
  const un = $('unsupported'); un.hidden = ok;
  if (!ok) {
    $('pick-note').textContent = '';
    un.textContent = 'This browser cannot write to a folder from a web page (it has no File System Access API). Download the zip instead, unzip it, and copy the ' + folder +
      ' folder into the themes folder on the SD card (make a folder called themes at the top of the card if there is none). Then eject the card and reboot the device.';
  }
  if (state.card) logLine('card chosen: ' + state.card.name);
  fillLimits('install', {});
}
$('btn-pick').addEventListener('click', async () => {
  try {
    const h = await window.showDirectoryPicker({ mode: 'readwrite', id: 'umbravox-card' });
    state.card = h; clear($('log'));
    const info = await inspectCard(h);
    if (info.looksLikeCard) logLine('found the card  (UMBRAVOX card: ' + info.found.slice(0, 3).join(', ') + ')');
    else logLine('this folder has no diag.log or log CSV at the top, so it may not be the card. Check before writing.', 'warn');
    $('btn-write').disabled = false;
  } catch (e) {
    if (e && e.name === 'AbortError') return;
    clear($('log')); logLine('could not open that folder: ' + ((e && e.message) || e), 'err');
  }
});

// Ask what to do about an existing folder. Resolves 'overwrite' | 'alternative' | 'proceed' | 'cancel'.
function askCollision(plan) {
  const box = $('collide'); clear(box); box.hidden = false;
  return new Promise((resolve) => {
    const done = (v) => { box.hidden = true; resolve(v); };
    const btn = (label, v, cls = 'btn small') => { const b = el('button', { type: 'button', class: cls, text: label }); b.addEventListener('click', () => done(v)); return b; };
    const row = el('div', { class: 'row' });
    if (plan.collision) {
      const what = plan.collision.name === null ? 'its theme.json could not be read' : 'its theme name is "' + plan.collision.name + '"';
      box.append(el('p', { text: '/themes/' + plan.collision.folder + ' is already on the card; ' + what + '.' }));
      row.append(btn('Overwrite', 'overwrite'));
      if (plan.sameName) box.append(el('p', { class: 'small', text: 'It has the same theme name as this one, so installing under another folder would give the device two themes with one name and it would skip one. Overwrite, or rename this theme first.' }));
      else row.append(btn('Install as ' + plan.alternative, 'alternative'));
    } else {
      box.append(el('p', { text: 'The folder /themes/' + plan.nameClash.folder + ' already holds a theme named "' + plan.nameClash.name + '". The device skips a pack whose name is taken, so one of the two would not appear.' }));
      row.append(btn('Install anyway', 'proceed'));
    }
    row.append(btn('Cancel', 'cancel'));
    box.append(row);
  });
}

$('btn-write').addEventListener('click', async () => {
  if (!state.card || !state.lastPack) return;
  $('btn-write').disabled = true;
  const log = (t, c) => logLine(t, c);
  try {
    let files = state.lastPack.files;
    const folder = files[0].path.split('/')[0];
    const theme = currentTheme();
    const plan = planInstall(folder, theme.name, await readCardThemes(state.card));
    if (plan.collision || plan.nameClash) {
      const choice = await askCollision(plan);
      if (choice === 'cancel') { log('cancelled; nothing was written', 'warn'); $('btn-write').disabled = false; return; }
      if (choice === 'alternative') files = retargetFiles(files, plan.alternative);
    }
    const res = await installToCard(state.card, files);
    for (const w of res.wrote) log('wrote /' + w);
    for (const r of res.removed) log('removed the old /' + r);
    if (!res.ok) { log('stopped: ' + res.error, 'err'); $('btn-write').disabled = false; return; }
    if (res.verified) log('read it back and checked it');
    else {
      for (const m of res.mismatched) log('/' + m + ' does not read back the same as what was written', 'err');
      log('The card may be failing or write-protected. Try again, or use the zip.', 'err');
      $('btn-write').disabled = false;
      return;
    }
    const done = $('done'); clear(done); $('done-head').className = 'num ok';
    done.append(el('p', { text: 'Eject the card, put it in the CYD and reboot.' }),
      el('p', { text: 'Menu -> ASPECT -> THEME, then step to  ' + theme.name + '.' }),
      el('p', { class: 'tiny muted', text: 'A theme that fails to load is skipped and the reason goes in /diag.log.' }));
    $('btn-write').disabled = false;
  } catch (e) {
    log('failed: ' + ((e && e.message) || e), 'err'); $('btn-write').disabled = false;
  }
});

// ---------------------------------------------------------------- boot
async function boot() {
  buildPalette();
  syncTabs();
  fillLimits('design', DESIGN_PLACES);
  try { preview = await loadWasm(new URL('../wasm/uv_preview.js', import.meta.url).href); }
  catch (e) { preview = null; $('open-msg').textContent = ''; console.warn('preview unavailable', e); }
  buildStart();
  if (preview && state.theme) refresh();
  if (!preview) {
    const m = $('preview-msg'); m.className = 'msg warn';
    m.textContent = 'The live preview could not load (the WebAssembly build is missing or blocked). You can still design, check and install a theme.';
    $('templates').after(el('p', { class: 'msg warn', text: 'The live preview could not load, so the template pictures are not shown.' }));
  }
  window.__designerBooted = true;
  requestAnimationFrame(tick);
}
boot();
