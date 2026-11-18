// DOM glue for the designer. The rules live in model.js, plan.js, limitations.js,
// install.js and preview.js; this file only wires them to the page.
// User-supplied strings (theme names, folder names read from the card) only ever
// go in through textContent.
import limits from './limits.json' with { type: 'json' };
import { ROLES, isHex, fromAccent } from './palette.js';
import { runChecks, lookChecks } from './checks.js';
import { buildPack, folderName } from './pack.js';
import { makeZip } from './zip.js';
import { imageToEmblem } from './emblem.js';
import {
  TEMPLATES, makeTemplate, blankTheme, sanitizeText, sanitizeName, themeFromState, openPackFiles, selectPackFiles, startSweep,
} from './model.js';
import { limitsFor, placeLimitations, renderLimitList, DESIGN_PLACES } from './limitations.js';
import { installGate } from './gate.js';
import { planInstall, retargetFiles } from './plan.js';
import { installToCard, fsAccessSupported, readCardThemes, inspectCard } from './install.js';
import { installOverSerial, wrapWebSerial, usbSupported, BAUD } from './usbinstall.js';
import { loadWasm, W, H, BOARDS } from './preview.js';
import { grabFrames, grabGifFrames, isGifFile, buildClip, quantizeColour, greyFrames, MAX_COLOUR_FRAMES } from './radarvideo.js';
import { emptySounds, hasSounds, cutSound, decodeToMono16k, playPcm, soundSeconds, LEVELS } from './alertsound.js';
import { usbFilters, boardWords, BOARD_INFO } from './boards.js';
import { shareUrl, renameWarning, THEMES_SITE } from './share.js';
import { themeParam, fetchGalleryTheme } from './gallery.js';
import { newLayout, setPiece, showPiece, isEmpty, fromJson, rectOf, draftOther, withDrafts, SCREEN, setStyle, setBoardStyle, addDecor, decorNames, isDecor, setCard, cardOf } from './layout.js';
import { alertFrame, nextMode, editableRects, effectAllowed } from './layout-alert.js';
import { packMedia } from './layoutmedia.js';
import { encodeFont, rasteriseFont } from './fontfmt.js';
import { grabGifArt } from './artfmt.js';
import { toDevice, pickPiece, handleAt, dragTo, nudge } from './layout-edit.js';
import { PRESETS, presetJson } from './layout-presets.js';

const $ = (id) => document.getElementById(id);
const state = { theme: null, emblem: null, clip: null, sounds: emptySounds(), tab: 'scan', view: 'start', card: null, lastPack: null, board: 'cyd', conf: 'high', basedOn: '', basedOnName: '',
  layout: newLayout(), lboard: 'waveshare', lsel: null, presetId: 'standard', lvisited: new Set(),
  media: { fonts: {}, pictures: {} }, mediaVersion: 0, lscreen: 'scan', lmode: 'scan', lplay: null };
let preview = null;       // null until the wasm loads (or if it cannot)
// The board the preview can really draw: an older wasm may lack the Waveshare, and the install picker does not need it.
function previewBoard() { return preview && preview.boards().includes(state.board) ? state.board : 'cyd'; }

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
function installBlock() {
  if (!state.theme) return 'Choose a template first.';
  const g = installGate(currentTheme(), state.emblem, state.clip, state.sounds, layoutText(), preview ? (t) => preview.checkLayout(t) : null);
  return g.ok ? null : g.reason;
}
function show(view) {
  if (view === 'install') {
    const why = installBlock();
    if (why) {
      const m = state.view === 'start' ? $('open-msg') : $('action-msg');
      m.className = 'msg err'; m.textContent = 'The Install step is closed. ' + why;
      if (state.view === 'install') view = 'design'; else return;
    }
  }
  state.view = view;
  for (const s of document.querySelectorAll('.view')) s.hidden = s.dataset.view !== view;
  for (const b of document.querySelectorAll('.step')) {
    if (b.dataset.view === view) b.setAttribute('aria-current', 'page'); else b.removeAttribute('aria-current');
    if (b.dataset.view === 'design') b.disabled = !state.theme;
    if (b.dataset.view === 'layout') b.disabled = !state.theme || !preview || !preview.hasEngine();
    if (b.dataset.view === 'install') { const why = installBlock(); b.disabled = !!why; b.title = why || ''; }
  }
  if (view === 'install') enterInstall();
  if (view === 'layout') enterLayout();
}
for (const b of document.querySelectorAll('.step')) b.addEventListener('click', () => show(b.dataset.view));

// ---------------------------------------------------------------- canvas painting
function paint(canvas, rgba, board = 'cyd') {
  const { w, h } = BOARDS[board];
  if (canvas.width !== w || canvas.height !== h) { canvas.width = w; canvas.height = h; }
  canvas.getContext('2d').putImageData(new ImageData(new Uint8ClampedArray(rgba.buffer, rgba.byteOffset, rgba.length), w, h), 0, 0);
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

// from: the gallery entry a theme was opened from ({folder, name}); every other start clears it.
function begin(theme, emblem, clip = null, sounds = null, from = null) {
  state.basedOn = from ? from.folder : ''; state.basedOnName = from ? from.name : '';
  state.theme = theme; state.emblem = emblem; state.clip = clip; state.sounds = sounds || emptySounds(); state.card = null;
  state.media = { fonts: {}, pictures: {} }; state.mediaVersion++;
  const preset = PRESETS.find((p) => p.id === state.presetId);
  state.layout = preset && preset.layout ? fromJson(presetJson(preset)) : newLayout();
  state.layout.raw = null;   // a preset is a starting point, not an opened file
  state.lsel = null; state.lvisited = new Set();
  $('open-msg').textContent = '';
  for (const k of LEVELS) sndSrc[k] = null;
  vidSrc = null; vidFile = null;
  paintVideoBar(); paintSoundBar();
  syncForm(); changed(); show('design');
}

// open an existing theme: files, folder or drop
// Descriptors only; contents are read after selectPackFiles has chosen what may be read.
const describeFile = (f, path) => ({ name: path || f.webkitRelativePath || f.name, webkitRelativePath: path || f.webkitRelativePath || '', size: f.size, file: f });
async function entriesToFiles(items) {
  const files = [];
  const walk = async (entry, depth) => {
    if (entry.isFile) { const f = await new Promise((res, rej) => entry.file(res, rej)); files.push(describeFile(f, entry.fullPath.replace(/^\//, ''))); }
    else if (entry.isDirectory && depth < 3) {
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
async function openThese(descs) {
  const msg = $('open-msg'); msg.className = 'msg';
  try {
    const sel = selectPackFiles(descs);
    if (!sel.ok) { msg.className = 'msg err'; msg.textContent = sel.error; return; }
    const read = [];
    for (const d of sel.picked) read.push({ name: d.name, data: new Uint8Array(await d.file.arrayBuffer()) });
    const r = openPackFiles(read);
    if (!r.ok) { msg.className = 'msg err'; msg.textContent = r.error; return; }
    begin(r.theme, r.emblem, r.clip, r.sounds);
    state.media = r.media || { fonts: {}, pictures: {} }; state.mediaVersion++;
    adoptLayout(r.layout);
    if (r.warnings.length) { const m = $('action-msg'); m.className = 'msg warn'; m.textContent = 'Opened with changes: ' + r.warnings.join(' '); }
  } catch (e) { msg.className = 'msg err'; msg.textContent = 'Could not read those files: ' + e.message; }
}
$('btn-open-files').addEventListener('click', () => $('in-files').click());
$('btn-open-folder').addEventListener('click', () => $('in-folder').click());
$('in-files').addEventListener('change', (e) => openThese([...e.target.files].map((f) => describeFile(f))));
$('in-folder').addEventListener('change', (e) => openThese([...e.target.files].map((f) => describeFile(f))));
const drop = $('drop');
drop.addEventListener('click', () => $('in-files').click());
drop.addEventListener('keydown', (e) => { if (e.key === 'Enter' || e.key === ' ') $('in-files').click(); });
drop.addEventListener('dragover', (e) => { e.preventDefault(); drop.classList.add('over'); });
drop.addEventListener('dragleave', () => drop.classList.remove('over'));
drop.addEventListener('drop', async (e) => {
  e.preventDefault(); drop.classList.remove('over');
  const items = [...e.dataTransfer.items];
  const files = items.some((i) => i.webkitGetAsEntry) ? await entriesToFiles(items) : [];
  openThese(files.length ? files : [...e.dataTransfer.files].map((f) => describeFile(f)));
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
  $('f-heptagram').checked = t.heptagram !== 'off'; $('f-contacts').checked = t.contacts !== 'off';
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
$('f-heptagram').addEventListener('change', (e) => { state.theme.heptagram = e.target.checked ? 'on' : 'off'; changed(); });
$('f-contacts').addEventListener('change', (e) => { state.theme.contacts = e.target.checked ? 'on' : 'off'; changed(); });
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
  checksNow = runChecks(t, state.emblem, state.clip, state.sounds);
  const rename = renameWarning(t.name, state.basedOnName);
  if (rename) checksNow.push({ level: 'warn', text: rename });
  for (const e of layoutPacked().errors) checksNow.push({ level: 'error', text: 'Layout: ' + e });
  checksNow.push(...lookChecks(t, state.layout, state.media, preview ? rules() : null));
  const lt = layoutText();
  if (lt && preview) {
    const v = preview.checkLayout(lt);
    checksNow.push(v.ok ? { level: 'ok', text: 'Layout: the device accepts it on both boards.' } : { level: 'error', text: 'Layout: ' + v.why.replace(/^layout\.json: /, '') });
    // The Layout step says it too: the board picture there keeps the last layout the device accepted.
    const lm = $('layout-msg');
    if (!v.ok) { lm.className = 'msg err'; lm.textContent = 'The device would refuse this layout: ' + v.why.replace(/^layout\.json: /, '') + '. The picture shows the last layout it accepted.'; }
    else if (lm.className === 'msg err') { lm.className = 'msg'; lm.textContent = ''; }
  } else if ($('layout-msg').className === 'msg err') { $('layout-msg').className = 'msg'; $('layout-msg').textContent = ''; }
  const ul = $('checks'); clear(ul);
  for (const c of checksNow) {
    ul.append(el('li', { class: c.level }, el('span', { class: 'g', text: c.level === 'ok' ? 'v' : c.level === 'warn' ? '!' : 'x' }), el('span', { text: c.text })));
  }
  const bad = checksNow.some((c) => c.level === 'error');
  for (const id of ['btn-download', 'btn-install', 'btn-share']) { $(id).disabled = bad; $(id).title = bad ? 'Fix the errors in the checks first.' : ''; }
  const nav = document.querySelector('.step[data-view="install"]'), why = installBlock();
  nav.disabled = !!why; nav.title = why || '';
}
function refresh() {
  if (!preview || !state.theme) return;
  const r = preview.loadDraft(currentTheme(), state.emblem, state.clip, layoutText(), previewMedia());
  const m = $('preview-msg');
  if (r.ok) { m.className = 'msg'; m.textContent = ''; }
  else { m.className = 'msg warn'; m.textContent = 'The preview shows the last version the device would accept. This one is rejected: ' + r.error; }
}
for (const b of $('tabs').children) b.addEventListener('click', () => { state.tab = b.dataset.v; syncTabs(); t0 = performance.now(); });
// The board being previewed (remembered per browser; the CYD until the person picks another).
const BOARD_KEY = 'uvx.designer.board';
function rememberBoard() { try { localStorage.setItem(BOARD_KEY, state.board); } catch (e) { /* private window: not remembered */ } }
function recallBoard() { try { const b = localStorage.getItem(BOARD_KEY); if (b && preview && preview.boards().includes(b)) state.board = b; } catch (e) { /* not remembered */ } }
for (const b of document.querySelectorAll('[data-b]')) b.addEventListener('click', () => { state.board = b.dataset.b; rememberBoard(); syncTabs(); t0 = performance.now(); if (state.view === 'install') paintInstall(); });
for (const b of $('confs').children) b.addEventListener('click', () => { state.conf = b.dataset.c; syncTabs(); t0 = performance.now(); });
function syncTabs() {
  const screens = BOARDS[state.board].screens;
  if (!screens.includes(state.tab)) state.tab = 'scan';
  for (const b of $('tabs').children) {
    b.hidden = !screens.includes(b.dataset.v);
    b.setAttribute('aria-selected', String(b.dataset.v === state.tab));
  }
  for (const b of document.querySelectorAll('[data-b]')) b.setAttribute('aria-selected', String(b.dataset.b === state.board));
  for (const b of $('confs').children) b.setAttribute('aria-selected', String(b.dataset.c === state.conf));
  $('dial-note').hidden = BOARD_INFO[state.board].dialPreset;
  const words = boardWords(state.board);
  for (const n of document.querySelectorAll('[data-bt]')) n.textContent = words[n.dataset.bt];
  $('boardbar').hidden = !preview || preview.boards().length < 2;
  $('confs').hidden = !(state.board === 'waveshare' && state.tab === 'alert');   // only the engine UI has LOW/MED/HIGH takeovers
  $('vidbar').hidden = !(preview && previewBoard() === 'waveshare');   // the clip plays in the Waveshare's scan radar
  $('soundbar').hidden = !(preview && previewBoard() === 'waveshare'); // only the Waveshare has a speaker
}

// Alert sounds for the Waveshare's speaker: one optional sound per level. The browser decodes any audio file, and
// js/alertsound.js turns it into the board's format (16 kHz mono, at most 6 s). They become part of the theme (alert.snd).
const SND_NAMES = { low: 'LOW', med: 'MED', high: 'HIGH' };
let sndBuilt = false;
const sndSrc = { low: null, med: null, high: null };   // the decoded recording behind each sound ({f, start}), so Start at can re-cut it
const clock = (s) => Math.floor(s / 60) + ':' + (s % 60).toFixed(1).padStart(4, '0');
function buildSoundBar() {
  if (sndBuilt) return; sndBuilt = true;
  const bar = $('soundbar');
  for (const k of LEVELS) {
    const row = document.createElement('div'); row.className = 'vidrow';
    row.innerHTML = '<label class="small" for="snd-' + k + '">' + SND_NAMES[k] + ' alert</label>' +
      '<input type="file" id="snd-' + k + '" accept="audio/*">' +
      '<button type="button" class="btn" id="snd-' + k + '-play" hidden>Play</button>' +
      '<button type="button" class="btn" id="snd-' + k + '-clear" hidden>Remove</button>' +
      '<span class="small muted" id="snd-' + k + '-info"></span>' +
      '<label class="small" id="snd-' + k + '-startlab" for="snd-' + k + '-start" hidden>Start at <span id="snd-' + k + '-startval">0:00.0</span></label>' +
      '<input type="range" id="snd-' + k + '-start" min="0" max="0" value="0" step="1" hidden aria-label="Where in the recording the ' + SND_NAMES[k] + ' sound starts">';
    bar.insertBefore(row, $('snd-msg'));
    $('snd-' + k).addEventListener('change', (e) => loadSound(k, e.target.files[0] || null));
    $('snd-' + k + '-play').addEventListener('click', () => playPcm(state.sounds[k]));
    $('snd-' + k + '-clear').addEventListener('click', () => { state.sounds[k] = null; sndSrc[k] = null; $('snd-' + k).value = ''; paintSoundBar(); changed(); });
    $('snd-' + k + '-start').addEventListener('input', (e) => { $('snd-' + k + '-startval').textContent = clock(Number(e.target.value) / 10); });
    $('snd-' + k + '-start').addEventListener('change', (e) => {
      if (!sndSrc[k]) return;
      sndSrc[k].start = Number(e.target.value) / 10;
      const pcm = cutSound(sndSrc[k].f, sndSrc[k].start);
      if (pcm) { state.sounds[k] = pcm; $('snd-msg').textContent = ''; paintSoundBar(); changed(); }
    });
  }
}
function paintSoundBar() {
  buildSoundBar();
  for (const k of LEVELS) {
    const pcm = state.sounds[k];
    $('snd-' + k + '-play').hidden = !pcm; $('snd-' + k + '-clear').hidden = !pcm;
    $('snd-' + k + '-info').textContent = pcm ? soundSeconds(pcm).toFixed(1) + ' s' : 'built-in beep';
    const src = sndSrc[k], dur = src ? src.f.length / 16000 : 0;
    const pick = !!pcm && dur > 6.5;                                   // only a recording longer than a sound has a choice of where to start
    $('snd-' + k + '-start').hidden = !pick; $('snd-' + k + '-startlab').hidden = !pick;
    if (pick) { const r = $('snd-' + k + '-start'); r.max = String(Math.max(0, Math.floor((dur - 0.5) * 10))); r.value = String(Math.round(src.start * 10)); $('snd-' + k + '-startval').textContent = clock(src.start); }
  }
}
async function loadSound(k, file) {
  const msg = $('snd-msg');
  if (!file) return;
  try {
    msg.className = 'msg'; msg.textContent = 'Reading ' + file.name + '...';
    const f = await decodeToMono16k(file);
    const pcm = cutSound(f, 0);
    if (!pcm) throw new Error('there is nothing audible in it');
    state.sounds[k] = pcm; sndSrc[k] = { f, start: 0 };
    msg.textContent = f.length / 16000 > 6.5 ? 'Used the first 6 seconds of it; move Start at to use another part.' : '';
    changed();
  } catch (e) { msg.className = 'msg warn'; msg.textContent = 'Could not use this file: ' + ((e && e.message) || e); }
  finally { paintSoundBar(); }
}

// A video in the Waveshare radar. The browser decodes it; js/radarvideo.js turns it into the board's format, and it
// becomes part of the theme (radar.vid): previewed, checked, downloaded and installed with it.
let vidFile = null, vidBusy = false;
let vidSrc = null;                                   // the RGBA frames captured from the last file, so Keep colours can flip without decoding it again
function paintVideoBar() {
  const msg = $('vid-msg');
  $('vid-clear').hidden = !state.clip;
  const box = $('vid-colour');
  box.disabled = !vidSrc && !state.clip;                                                  // an opened pack has no source to convert again
  if (!vidSrc && state.clip) box.checked = !!state.clip.palette;
  if (state.clip) { msg.className = 'msg'; msg.textContent = `${state.clip.frames} frames, ${(state.clip.frames / state.clip.fps).toFixed(1)} s, looping, ${state.clip.palette ? 'in its own colours' : "in the theme's colours"}.`; }
  else if (!vidBusy) { msg.className = 'msg'; msg.textContent = ''; }
}
// RGBA frames -> the clip, in colour (one 16-colour palette for the whole video) or as greys.
function convertVideo() {
  if (!vidSrc) return;
  if ($('vid-colour').checked) { const q = quantizeColour(vidSrc.slice(0, MAX_COLOUR_FRAMES)); state.clip = buildClip(q.frames, q.palette); }
  else state.clip = buildClip(greyFrames(vidSrc));
}
async function loadVideo() {
  if (!vidFile || vidBusy || !preview) return;
  vidBusy = true;
  const msg = $('vid-msg'), url = URL.createObjectURL(vidFile);
  const onProgress = (d, n) => { msg.textContent = `Converting ${d} / ${n} frames...`; };
  const vpos = Number($('vid-pos').value) / 100;
  try {
    msg.className = 'msg'; msg.textContent = 'Reading the file...';
    if (isGifFile(vidFile)) vidSrc = await grabGifFrames(vidFile, { vpos, onProgress });
    else {
      const v = document.createElement('video');
      v.muted = true; v.playsInline = true; v.preload = 'auto'; v.src = url;
      await new Promise((res, rej) => { v.onloadeddata = res; v.onerror = () => rej(new Error('the browser cannot read this video')); });
      vidSrc = await grabFrames(v, { vpos, onProgress });
    }
    msg.textContent = 'Choosing colours...'; await new Promise((r) => setTimeout(r, 20));
    convertVideo();
    t0 = performance.now();
    changed();
  } catch (e) { msg.className = 'msg warn'; msg.textContent = 'Could not use this file: ' + ((e && e.message) || e); }
  finally { URL.revokeObjectURL(url); vidBusy = false; paintVideoBar(); }
}
$('vid-file').addEventListener('change', (e) => { vidFile = e.target.files[0] || null; loadVideo(); });
$('vid-pos').addEventListener('change', () => { if (vidFile && state.clip) loadVideo(); });
$('vid-colour').addEventListener('change', async () => {
  if (!vidSrc || vidBusy) return;
  vidBusy = true; const msg = $('vid-msg'); msg.className = 'msg'; msg.textContent = 'Choosing colours...';
  await new Promise((r) => setTimeout(r, 20));
  try { convertVideo(); t0 = performance.now(); changed(); } catch (e) { msg.className = 'msg warn'; msg.textContent = 'Could not convert: ' + e.message; }
  finally { vidBusy = false; paintVideoBar(); }
});
$('vid-clear').addEventListener('click', () => {
  state.clip = null; vidFile = null; vidSrc = null; $('vid-file').value = '';
  paintVideoBar(); changed();
});

let t0 = performance.now(), lastDraw = 0;
function tick(now) {
  requestAnimationFrame(tick);
  if (!preview || !preview.hasDraft() || (state.view !== 'design' && state.view !== 'layout') || document.hidden || now - lastDraw < 40) return;
  lastDraw = now;
  const ms = now - t0;
  if (state.view === 'layout') { paintLayout(ms); return; }
  const frame = state.tab === 'alert' ? Math.floor((ms % 2500) / 20) : Math.floor(ms / 20);
  paint($('preview'), preview.renderDraft(state.tab, frame, { board: previewBoard(), alertConf: state.conf }), previewBoard());
}

// ---------------------------------------------------------------- build + download
function currentPack() { return buildPack(currentTheme(), state.emblem, state.clip, state.sounds, layoutText(), layoutPacked().files); }
// Save the pack as <folder>.zip through a temporary link. -> the folder name.
function saveZip(pack) {
  const folder = pack.files[0].path.split('/')[0];
  const blob = new Blob([makeZip(pack.files)], { type: 'application/zip' });
  const a = el('a', { href: URL.createObjectURL(blob), download: folder + '.zip' });
  document.body.append(a); a.click(); a.remove();
  setTimeout(() => URL.revokeObjectURL(a.href), 10000);
  return folder;
}
function download() {
  const m = $('action-msg');
  if (checksNow.some((c) => c.level === 'error')) { m.className = 'msg err'; m.textContent = 'Fix the errors in the checks first.'; return; }
  try {
    const folder = saveZip(currentPack());
    m.className = 'msg';
    m.textContent = 'Downloaded ' + folder + '.zip. Unzip it and copy the ' + folder + ' folder into the themes folder on the SD card.';
  } catch (e) { m.className = 'msg err'; m.textContent = e.message; }
}
$('btn-download').addEventListener('click', download);
$('btn-zip2').addEventListener('click', download);
// Share: save the same zip as Download pack, then open the themes repository's submission form with the fields
// filled. The page cannot attach the zip to the form; the person drags it in.
$('btn-share').addEventListener('click', () => {
  if (checksNow.some((c) => c.level === 'error')) return;
  const m = $('action-msg');
  if (layoutText()) {   // the community repository accepts layout.json from its next update (plan 6)
    m.className = 'msg warn';
    m.textContent = 'Sharing a theme with a layout is not open yet: the community themes repository accepts layouts from its next update. Download the pack and install it yourself for now, or set the layout back to Standard to share.';
    return;
  }
  try {
    const folder = saveZip(currentPack());
    window.open(shareUrl(currentTheme(), state.basedOn), '_blank', 'noopener');
    m.className = 'msg';
    m.textContent = 'Saved ' + folder + '.zip. The submission form should have opened in a new tab: drag the zip into it, tick the two boxes and submit. If no tab opened, allow pop-ups for this page and press Share again.';
  } catch (e) { m.className = 'msg err'; m.textContent = e.message; }
});
$('btn-install').addEventListener('click', () => show('install'));

// ---------------------------------------------------------------- install
function logLine(text, cls = '') { $('log').append(el('li', { class: cls, text })); }
function paintInstall() {
  if (preview && preview.hasDraft()) paint($('install-preview'), preview.renderDraft('scan', 24, { board: previewBoard() }), previewBoard());
}
function enterInstall() {
  let pack;
  try { pack = currentPack(); } catch (e) {
    const m = $('action-msg'); m.className = 'msg err'; m.textContent = 'Cannot install: ' + e.message;
    state.view = 'design'; show('design'); return;
  }
  state.lastPack = pack;
  paintInstall();
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
  fillLimits('install', USB_PLACES);
  const un2 = $('usb-clip-note');
  un2.hidden = false;
  un2.textContent = (state.clip || hasSounds(state.sounds))
    ? 'This theme carries ' + [state.clip && 'a video (about ' + Math.round((state.clip.data.length + 12) / 1024) + ' KB)', hasSounds(state.sounds) && 'alert sounds'].filter(Boolean).join(' and ') + '. Over USB a video takes a few minutes and sounds under a minute; a card reader is instant.'
    : 'If you took a video or sounds OUT of a theme you installed before, the USB route cannot delete the old radar.vid or alert.snd from the card (it only ever writes): the board would keep playing it. Delete it with tools/sdput.py, or use the card-reader route, which removes it.';
  enterUsb();
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
// `unread` says why the folder's theme name is not known (name === null).
function askCollision(plan, box = $('collide'), unread = 'its theme.json could not be read') {
  clear(box); box.hidden = false;
  return new Promise((resolve) => {
    const done = (v) => { box.hidden = true; resolve(v); };
    const btn = (label, v, cls = 'btn small') => { const b = el('button', { type: 'button', class: cls, text: label }); b.addEventListener('click', () => done(v)); return b; };
    const row = el('div', { class: 'row' });
    if (plan.collision) {
      const what = plan.collision.name === null ? unread : 'its theme name is "' + plan.collision.name + '"';
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
    if (!res.ok) {
      log('stopped: ' + res.error, 'err');
      log(res.message, res.state === 'unknown' ? 'err' : 'warn');
      $('btn-write').disabled = false; return;
    }
    if (res.verified) log('read it back and checked it');
    else {
      for (const m of res.mismatched) log('/' + m + ' does not read back the same as what was written', 'err');
      log(res.message, 'err');
      log('The card may be failing or write-protected. Try again, or use the zip.', 'err');
      $('btn-write').disabled = false;
      return;
    }
    const done = $('done'); clear(done); $('done-head').className = 'num ok';
    done.append(el('p', { text: 'Eject the card, put it in ' + boardWords(state.board).name + ' and reboot.' }),
      el('p', { text: 'Menu -> ASPECT -> THEME, then step to  ' + theme.name + '.' }),
      el('p', { class: 'tiny muted', text: 'A theme that fails to load is skipped and the reason goes in /diag.log.' }));
    $('btn-write').disabled = false;
  } catch (e) {
    log('failed: ' + ((e && e.message) || e), 'err'); $('btn-write').disabled = false;
  }
});

// ---------------------------------------------------------------- install over USB (Web Serial)
// DESIGN RULE (security review): the page sends only SDLS /themes, SDPUT, D, END
// and ABORT (usbinstall.js refuses anything else before writing it). The device's
// delete, remove-folder and restart commands act without any confirmation on the
// device, so no button, link or code path here ever sends them.
// Device output is untrusted text: it reaches the page through el(..., {text}) only.
const USB_PLACES = { usbbrowser: 'usb', usbreset: 'usb', usbreboot: 'usb', usbexclusive: 'usb' };
const usb = { port: null, link: null, busy: false, ac: null };
function usbLog(text, cls = '') { $('usb-log').append(el('li', { class: cls, text })); }
function usbButtons() {
  $('btn-usb-connect').disabled = !!usb.link || usb.busy;
  $('btn-usb-disconnect').hidden = !usb.link;
  $('btn-usb-disconnect').disabled = usb.busy;
  $('btn-usb-send').disabled = !usb.link || usb.busy || !state.lastPack;
  $('btn-usb-cancel').hidden = !usb.busy;
}
function enterUsb() {
  const ok = usbSupported();
  $('usb-body').hidden = !ok;
  const un = $('usb-unsupported'); un.hidden = ok;
  if (!ok) un.textContent = 'Installing over USB needs desktop Chrome or Edge (Web Serial, version 89 or later). This browser has no Web Serial: Firefox and Safari do not support it, nor do phones. Use the SD card route above, or the zip.';
  if (!usb.busy) {
    clear($('usb-done')); $('usb-done').append(el('p', { class: 'muted', text: 'Not yet.' })); $('usb-done-head').className = 'num';
    $('usb-collide').hidden = true; $('usb-progress').hidden = true;
  }
  usbButtons();
}
async function usbDisconnect(why) {
  const link = usb.link;
  usb.link = null; usb.port = null;
  if (link) { try { await link.close(); } catch (e) { /* closed already */ } }
  if (why) usbLog(why, 'warn');
  usbButtons();
}
$('btn-usb-connect').addEventListener('click', async () => {
  if (!usbSupported() || usb.link) return;
  let port;
  try {
    port = await navigator.serial.requestPort(usbFilters(state.board, $('usb-all').checked) ? { filters: usbFilters(state.board, false) } : {});
  } catch (e) {
    if (e && e.name === 'NotFoundError') return;          // the chooser was closed
    usbLog('could not choose a port: ' + ((e && e.message) || e), 'err'); return;
  }
  try {
    await port.open({ baudRate: BAUD, dataBits: 8, stopBits: 1, parity: 'none', flowControl: 'none' });
  } catch (e) {
    usbLog('could not open the port' + (e && e.name === 'NetworkError' ? ': another program (a serial monitor?) has it open. Close it and try again.' : ': ' + ((e && e.message) || e)), 'err');
    return;
  }
  // Hold DTR and RTS low at once: on the CYD's CH340 they drive the auto-reset
  // circuit. Opening the port may already have pulsed them (the board resets).
  try { await port.setSignals({ dataTerminalReady: false, requestToSend: false }); }
  catch (e) { usbLog('could not lower DTR/RTS (' + ((e && e.message) || e) + '); the board may reset', 'warn'); }
  usb.port = port; usb.link = wrapWebSerial(port);
  clear($('usb-log'));
  usbLog('connected at 115200 baud. If the board restarted, it is booting; the page waits for it before sending.');
  usbButtons();
});
$('btn-usb-disconnect').addEventListener('click', () => { if (!usb.busy) usbDisconnect('disconnected; the port is free for other programs'); });
if (usbSupported()) {
  navigator.serial.addEventListener('disconnect', (e) => {
    if (usb.port && e.target === usb.port) { if (usb.ac) usb.ac.abort(); usbDisconnect(boardWords(state.board).name + ' was unplugged'); }
  });
}
$('btn-usb-cancel').addEventListener('click', () => { if (usb.ac) usb.ac.abort(); $('usb-collide').hidden = true; });
$('btn-usb-send').addEventListener('click', async () => {
  if (!usb.link || usb.busy || !state.lastPack) return;
  usb.busy = true; usb.ac = new AbortController(); usbButtons();
  const theme = currentTheme(), bar = $('usb-progress');
  bar.hidden = false; bar.value = 0;
  clear($('usb-done')); $('usb-done').append(el('p', { class: 'muted', text: 'Not yet.' })); $('usb-done-head').className = 'num';
  const said = new Set();
  try {
    const res = await installOverSerial(usb.link, state.lastPack.files, {
      themeName: theme.name,
      signal: usb.ac.signal,
      decide: (plan) => askCollision(plan, $('usb-collide'), 'over USB the page reads folder names only, so it cannot tell which theme is in it'),
      onProgress: (p) => {
        bar.max = p.total || 1; bar.value = p.sent;
        const say = (k, t) => { if (!said.has(k)) { said.add(k); usbLog(t); } };
        if (p.phase === 'connect') say('connect', 'waiting for the board to be ready...');
        if (p.phase === 'list') say('list', 'reading the folder list of /themes on the card');
        if (p.phase === 'upload' && p.file) say(p.file, 'sending ' + p.file);
      },
    });
    for (const w of res.wrote) usbLog(w + ': the device read it back from the card, size and CRC-32 match');
    if (!res.ok) {
      usbLog(res.cancelled ? 'cancelled' : 'stopped: ' + res.error, res.cancelled ? 'warn' : 'err');
      if (res.message) usbLog(res.message, res.folderState === 'untouched' ? 'warn' : 'err');
      return;
    }
    const done = $('usb-done'); clear(done); $('usb-done-head').className = 'num ok';
    done.append(el('p', { text: 'The pack is on the card in /themes/' + res.folder + '/. It loads at the next boot.' }),
      el('p', { text: boardWords(state.board).restart }),
      el('p', { text: 'Then Menu -> ASPECT -> THEME, and step to  ' + theme.name + '.' }),
      el('p', { class: 'tiny muted', text: 'A theme that fails to load is skipped and the reason goes in /diag.log.' }));
  } catch (e) {
    usbLog('failed: ' + ((e && e.message) || e), 'err');
  } finally {
    usb.busy = false; usb.ac = null; usbButtons();
  }
});

// ---------------------------------------------------------------- boot
// "?theme=<folder>": open that gallery theme, through the same checks as a theme opened from files.
async function openFromGallery() {
  const folder = themeParam(location.search);
  if (!folder) return;
  const msg = $('open-msg'); msg.className = 'msg'; msg.textContent = 'Opening the gallery theme...';
  const got = await fetchGalleryTheme(folder);
  if (!got.ok) { msg.className = 'msg err'; msg.textContent = got.error; return; }
  const r = openPackFiles(got.files);
  if (!r.ok) { msg.className = 'msg err'; msg.textContent = 'The gallery theme could not be opened: ' + r.error; return; }
  begin(r.theme, r.emblem, r.clip, r.sounds, { folder, name: String(got.entry.name || r.theme.name) });
  state.media = r.media || { fonts: {}, pictures: {} }; state.mediaVersion++;
  adoptLayout(r.layout);
  const m = $('action-msg'); m.className = r.warnings.length ? 'msg warn' : 'msg';
  m.textContent = 'Opened ' + r.theme.name + ' from the gallery.' + (r.warnings.length ? ' Opened with changes: ' + r.warnings.join(' ') : '');
}

// ---------------------------------------------------------------- the Layout step
// The model is js/layout.js, the pointer maths js/layout-edit.js, the rules the device's own (preview.pieceNames,
// defaultRect, checkLayout). This only wires them to the page.
let layoutRules = null;
function rules() {
  if (!layoutRules) layoutRules = { names: preview.pieceNames(), defaultRect: (b, n) => preview.defaultRect(b, n),
    cardDefault: (b, l) => preview.cardDefault(b, l), fxCap: (b) => preview.fxCap(b) };
  return layoutRules;
}
// The layout.json the pack carries: with a board the author never opened drafted from the other (withDrafts).
// The layout.json the pack carries, with its fonts and pictures as pack files (js/layoutmedia.js), worked out once per
// edit: a board the author never opened is drafted from the other (withDrafts).
let packedMemo = null;
function layoutPacked() {
  const none = { json: null, files: [], errors: [] };
  if (!preview) return none;
  const key = [state.layout, state.lvisited.size, state.mediaVersion];
  if (packedMemo && packedMemo.key.every((v, i) => v === key[i])) return packedMemo.out;
  const L = withDrafts(state.layout, rules(), state.lvisited);
  let out = none;
  if (!isEmpty(L)) {
    if (L.raw) {   // an opened file, untouched: it goes as it is, with the fonts and pictures that came with it
      const files = [];
      for (const [k, v] of Object.entries(state.media.fonts)) if (k.startsWith('file:')) files.push({ name: k.slice(5) + '.fnt', data: v });
      for (const [k, v] of Object.entries(state.media.rawArt || {})) files.push({ name: k + '.art', data: v });
      out = { json: L.raw, files, errors: [] };
    } else out = packMedia(L, rules().names, state.media);
  }
  packedMemo = { key, out };
  return out;
}
function layoutText() { return layoutPacked().json; }
function previewMedia() {
  const m = { fonts: {}, arts: {} };
  for (const f of layoutPacked().files) (f.name.endsWith('.fnt') ? m.fonts : m.arts)[f.name.slice(0, -4)] = f.data;
  return m;
}
const engineBoard = (b) => (b === 'waveshare' ? 'waveshare' : 'cyd-engine');
// Plan 5b: the alert mode. An alert level shows that alert over the scan; only its card can be selected.
const isCard = (n) => typeof n === 'string' && n.startsWith('alert.card.');
const cardLevel = (n) => n.slice('alert.card.'.length);
function applyMode(m) {
  state.lmode = m.mode; state.lplay = m.playing ? performance.now() : null;
  if (state.lsel && (isCard(state.lsel) !== (m.mode !== 'scan'))) state.lsel = null;
  if (m.mode !== 'scan') state.lsel = 'alert.card.' + m.mode;
  for (const t of document.querySelectorAll('[data-lm]')) t.setAttribute('aria-selected', String(t.dataset.lm === m.mode));
  $('layout-play').hidden = m.mode === 'scan';
  $('layout-play').textContent = state.lplay === null ? '\u25B6 Play' : '\u25A0 Stop';
}
for (const t of document.querySelectorAll('[data-lm]')) t.addEventListener('click', () => { applyMode(nextMode(state.lmode, 'mode:' + t.dataset.lm)); syncLayout(); });
$('layout-play').addEventListener('click', () => {
  state.lplay = state.lplay === null ? performance.now() : null;
  $('layout-play').textContent = state.lplay === null ? '\u25B6 Play' : '\u25A0 Stop';
});
function editRects(board) {
  const cr = state.lmode === 'scan' ? null : cardOf(state.layout, board, state.lmode, rules()).rect;
  return editableRects(state.lmode, boardRects(board), cr);
}
// Where the selected thing is now (a piece or this level's card), and moving it.
function selRect() {
  const n = state.lsel;
  if (!n) return null;
  return isCard(n) ? cardOf(state.layout, state.lboard, cardLevel(n), rules()).rect : rectOf(state.layout, state.lboard, n, rules());
}
function placeSel(r) {
  const n = state.lsel;
  state.layout = isCard(n) ? setCard(state.layout, state.lboard, cardLevel(n), { rect: r }) : setPiece(state.layout, state.lboard, n, r);
}
function boardRects(board) {
  const out = {};
  for (const n of rules().names) out[n] = rectOf(state.layout, board, n, rules());
  for (const n of decorNames(state.layout, board)) out[n] = state.layout.boards[board][n].rect;
  return out;
}
function layoutChanged() { changed(); syncLayout(); }
// An opened pack's layout.json: kept even when the device would refuse it, so the author can fix it in the Layout step
// (the checks show why, and Download, Install and Share stay closed until it passes).
// Opened and gallery packs never take the Start page's preset: they have their own layout.json, or none.
function adoptLayout(text) {
  state.layout = newLayout(); state.lsel = null; state.lvisited = new Set();
  if (text) {
    const m = $('action-msg');
    try {
      state.layout = fromJson(text, preview ? rules().names : null);
      if (state.layout.dropped.length) { m.className = 'msg warn'; m.textContent = 'layout.json: ' + state.layout.dropped.join('; ') + '. The Layout step shows the rest; any edit saves a fresh file without these.'; }
    } catch (e) { m.className = 'msg warn'; m.textContent = e.message + ', so the layout was left out.'; }
  }
  layoutChanged();
}
function enterLayout() { selectBoard(state.lboard); }
function selectBoard(b) {
  // The first visit to a board the author has not touched, while the other is laid out: draft it from that one.
  const L = state.layout;
  if (L.master && L.master !== b && Object.keys(L.boards[b]).length === 0) {
    state.layout = draftOther(L, L.master, b, rules());
    $('layout-msg').className = 'msg'; $('layout-msg').textContent = 'Drafted from the other board. Check it, and drag anything that needs it.';
    changed();
  } else { $('layout-msg').textContent = ''; }
  state.lboard = b; state.lsel = null; state.lvisited.add(b);
  applyMode(nextMode(state.lmode, 'board'));
  for (const t of document.querySelectorAll('[data-lb]')) t.setAttribute('aria-selected', String(t.dataset.lb === b));
  $('layout-cyd-note').hidden = b !== 'cyd';
  syncLayout();
}
for (const t of document.querySelectorAll('[data-lb]')) t.addEventListener('click', () => selectBoard(t.dataset.lb));
function syncLayout() {
  if (!preview) return;
  const sel = state.lsel, r = selRect();
  $('layout-piece').textContent = sel ? sel : 'No piece selected';
  $('layout-modes').hidden = state.lscreen !== 'scan';
  for (const k of ['x', 'y', 'w', 'h']) { const i = $('layout-' + k); i.disabled = !r; i.value = r ? r[k] : ''; }
  $('layout-hide').disabled = !r || isCard(sel); $('layout-reset').disabled = !sel || isDecor(sel) || isCard(sel);
  $('layout-hide').textContent = sel && isDecor(sel) ? 'Remove' : 'Hide';
  syncLook();
  const tray = $('layout-tray'); clear(tray);
  for (const n of rules().names) {
    const p = state.layout.boards[state.lboard][n];
    if (!p || !p.hidden) continue;
    const b = el('button', { type: 'button', text: n });
    b.addEventListener('click', () => { state.layout = showPiece(state.layout, state.lboard, n); state.lsel = n; layoutChanged(); });
    tray.append(b);
  }
}
// ---------------------------------------------------------------- the look: fonts, colours and pictures (plan 4)
let builtinFonts = [];   // fonts/index.json (tools/bdf2fnt.mjs): [{id, name, px, licence, file}]
fetch('fonts/index.json').then((r) => (r.ok ? r.json() : [])).then((j) => { builtinFonts = Array.isArray(j) ? j : []; syncLook(); }).catch(() => {});
let uploads = 0;
const lookMsg = (text, warn = false) => { const m = $('lf-msg'); m.className = warn ? 'msg warn' : 'tiny muted'; m.textContent = text; };
function fillSelect(sel, opts, value) {
  const v = value ?? '';
  if (sel.options.length !== opts.length || [...sel.options].some((o, i) => o.value !== opts[i][0])) {
    clear(sel);
    for (const [val, text] of opts) sel.append(el('option', { value: val, text }));
  }
  sel.value = opts.some((o) => o[0] === v) ? v : '';
}
function fontOptions() {
  const o = [['', 'The built-in 5x7']];
  for (const f of builtinFonts) o.push([f.id, `${f.name} (${f.px} px)`]);
  for (const k of Object.keys(state.media.fonts)) if (!builtinFonts.some((f) => f.id === k)) o.push([k, k.replace(/^upload:|^file:/, '')]);
  return o;
}
const roleOptions = () => [['', "The theme's own"], ...ROLES.map((r) => [r, r])];
function syncLook() {
  if (!preview) return;
  const sel = state.lsel, p = sel ? state.layout.boards[state.lboard][sel] : null;
  const showLook = !!(sel && !isDecor(sel) && !isCard(sel) && !(p && p.hidden));
  syncCard();
  $('layout-look').hidden = !showLook;
  const st = (p && p.style) || {};
  fillSelect($('ls-font'), fontOptions(), st.font);
  $('ls-scale').value = st.scale ? String(st.scale) : '';
  for (const k of ['ink', 'fill', 'edge']) {
    fillSelect($('ls-' + k), roleOptions(), isHex(st[k]) ? '' : st[k]);
    if (isHex(st[k])) $('ls-' + k + '-hex').value = st[k].toLowerCase();
  }
  if (st.slice) $('ls-slice').value = st.slice[0];
  $('ls-effect').value = st.effect || 'none'; $('ls-speed').value = st.speed || 'medium';
  const cap = rules().fxCap(state.lboard), allowed = !sel || effectAllowed(state.layout, state.lboard, sel, cap);
  $('ls-effect').disabled = !allowed;
  $('ls-fx-note').textContent = allowed ? 'Plays only while an alert shows; pick an Alert tab above and press Play to see it.'
    : `At most ${cap} pieces can have an effect on this board (each repaints every alert frame).`;
  const bs = state.layout.style[state.lboard] || {};
  fillSelect($('lb-title'), fontOptions(), bs.title); fillSelect($('lb-body'), fontOptions(), bs.body); fillSelect($('lb-numbers'), fontOptions(), bs.numbers);
  fillSelect($('lb-hifill'), roleOptions(), bs.highlight?.fill); fillSelect($('lb-hiedge'), roleOptions(), bs.highlight?.edge);
  if (bs.panelSlice) $('lb-slice').value = bs.panelSlice[0];
  $('lb-screen').value = state.lscreen;
}
// The selected level's card: font, scale, picture, back to built-in, and its warnings.
function syncCard() {
  const sel = state.lsel, on = isCard(sel);
  $('layout-card').hidden = !on;
  if (!on) return;
  const lv = cardLevel(sel), c = cardOf(state.layout, state.lboard, lv, rules());
  fillSelect($('lc-font'), fontOptions(), c.font);
  $('lc-scale').value = c.scale ? String(c.scale) : '';
  $('lc-builtin').disabled = c.builtin;
  const label = state.lboard === 'waveshare' ? 'Waveshare' : 'CYD';
  const mine = lookChecks({ colors: {} }, state.layout, state.media, rules())
    .filter((x) => x.text.includes(`${lv} alert card`) && x.text.includes(label + ' board'));
  $('lc-warn').textContent = mine.map((x) => x.text).join(' ');
}
function cardSel(patch) {
  const lv = cardLevel(state.lsel), c = cardOf(state.layout, state.lboard, lv, rules());
  state.layout = setCard(state.layout, state.lboard, lv, { rect: c.rect, ...patch }); layoutChanged();
}
$('lc-font').addEventListener('change', async (e) => { try { await useFont(e.target.value); cardSel({ font: e.target.value || null }); } catch (x) { lookMsg(x.message, true); } });
$('lc-scale').addEventListener('change', (e) => cardSel({ scale: e.target.value ? Number(e.target.value) : null }));
$('lc-builtin').addEventListener('click', () => { if (!isCard(state.lsel)) return; state.layout = setCard(state.layout, state.lboard, cardLevel(state.lsel), null); layoutChanged(); });
$('ls-effect').addEventListener('change', (e) => styleSel({ effect: e.target.value }));
$('ls-speed').addEventListener('change', (e) => styleSel({ speed: e.target.value }));
// A built-in font's .fnt is fetched the first time it is chosen.
async function useFont(key) {
  if (!key || state.media.fonts[key]) return;
  const f = builtinFonts.find((x) => x.id === key);
  if (!f) return;
  const r = await fetch('fonts/' + f.file);
  if (!r.ok) throw new Error('could not load the font ' + f.name);
  state.media.fonts[key] = new Uint8Array(await r.arrayBuffer()); state.mediaVersion++;
}
// A PNG or GIF at its own size (at most 480x320), as RGBA frames with their delays.
async function readPicture(file) {
  const bmp = await createImageBitmap(file);
  const k = Math.min(1, 480 / bmp.width, 320 / bmp.height);
  const w = Math.max(1, Math.round(bmp.width * k)), h = Math.max(1, Math.round(bmp.height * k));
  if (isGifFile(file)) { bmp.close(); const g = await grabGifArt(file, { w, h }); return { w, h, frames: g.frames, delays: g.delays }; }
  const cv = new OffscreenCanvas(w, h), ctx = cv.getContext('2d');
  ctx.drawImage(bmp, 0, 0, w, h); bmp.close();
  return { w, h, frames: [Uint8ClampedArray.from(ctx.getImageData(0, 0, w, h).data)], delays: [0] };
}
async function addPicture(file) {
  const key = `pic:${++uploads}:${file.name}`;
  state.media.pictures[key] = await readPicture(file); state.mediaVersion++;
  return key;
}
function styleSel(patch) {
  const n = state.lsel;
  if (!n || isDecor(n)) return;
  state.layout = setStyle(state.layout, state.lboard, n, patch, rectOf(state.layout, state.lboard, n, rules())); layoutChanged();
}
const boardStyle = (patch) => { state.layout = setBoardStyle(state.layout, state.lboard, patch); layoutChanged(); };
const filePick = (id, fn) => $(id).addEventListener('change', async (e) => {
  const files = [...e.target.files]; e.target.value = '';
  if (!files.length) return;
  try { await fn(files); } catch (x) { lookMsg(x.message || String(x), true); }
});
$('ls-font').addEventListener('change', async (e) => { try { await useFont(e.target.value); styleSel({ font: e.target.value }); } catch (x) { lookMsg(x.message, true); } });
$('ls-scale').addEventListener('change', (e) => styleSel({ scale: e.target.value ? Number(e.target.value) : null }));
for (const k of ['ink', 'fill', 'edge']) {
  $('ls-' + k).addEventListener('change', (e) => styleSel({ [k]: e.target.value || null }));
  $('ls-' + k + '-hex').addEventListener('change', (e) => styleSel({ [k]: e.target.value.toUpperCase() }));
}
const border = (id) => { const b = Math.max(0, Math.min(60, Math.round(Number($(id).value) || 0))); return [b, b, b, b]; };
filePick('ls-frame', async ([f]) => styleSel({ frame: await addPicture(f), slice: border('ls-slice') }));
$('ls-slice').addEventListener('change', () => { const st = state.lsel && state.layout.boards[state.lboard][state.lsel]?.style; if (st && st.frame) styleSel({ slice: border('ls-slice') }); });
filePick('ls-bg', async ([f]) => styleSel({ bg: await addPicture(f) }));
filePick('lc-pic', async ([f]) => { if (isCard(state.lsel)) cardSel({ pic: await addPicture(f) }); });
$('ls-clear').addEventListener('click', () => styleSel({ font: null, scale: null, ink: null, fill: null, edge: null, frame: null, slice: null, bg: null, effect: null, speed: null }));
filePick('lf-file', async ([f]) => {
  const px = Math.max(6, Math.min(32, Math.round(Number($('lf-px').value) || 12)));
  const face = new FontFace('uv-upload-' + (++uploads), await f.arrayBuffer());
  const key = `upload:${f.name}@${px}`;
  state.media.fonts[key] = encodeFont(await rasteriseFont(face, px)); state.mediaVersion++;
  syncLook(); lookMsg(`${f.name} at ${px} px is in the font lists. Only share fonts you have the right to share.`);
});
filePick('ld-file', async ([f]) => {
  const key = await addPicture(f), src = state.media.pictures[key], S = SCREEN[state.lboard];
  const k = Math.min(1, 120 / src.w, 120 / src.h), w = Math.max(1, Math.round(src.w * k)), h = Math.max(1, Math.round(src.h * k));
  const r = addDecor(state.layout, state.lboard, key, { x: Math.round((S.w - w) / 2), y: Math.round((S.h - h) / 2), w, h });
  state.layout = r.L; state.lsel = r.name; layoutChanged();
});
for (const [id, k] of [['lb-title', 'title'], ['lb-body', 'body'], ['lb-numbers', 'numbers']])
  $(id).addEventListener('change', async (e) => { try { await useFont(e.target.value); boardStyle({ [k]: e.target.value || null }); } catch (x) { lookMsg(x.message, true); } });
for (const [id, k] of [['lb-hifill', 'fill'], ['lb-hiedge', 'edge']]) $(id).addEventListener('change', (e) => {
  const h = { ...(state.layout.style[state.lboard].highlight || {}) };
  if (e.target.value) h[k] = e.target.value; else delete h[k];
  boardStyle({ highlight: Object.keys(h).length ? h : null });
});
filePick('lb-bg', async ([f]) => boardStyle({ bg: await addPicture(f) }));
filePick('lb-panel', async ([f]) => boardStyle({ panel: await addPicture(f), panelSlice: border('lb-slice') }));
$('lb-slice').addEventListener('change', () => { if (state.layout.style[state.lboard].panel) boardStyle({ panelSlice: border('lb-slice') }); });
filePick('lb-icons', async (files) => { const keys = []; for (const f of files.slice(0, 6)) keys.push(await addPicture(f)); boardStyle({ icons: keys }); });
$('lb-screen').addEventListener('change', (e) => { state.lscreen = e.target.value; applyMode(nextMode(state.lmode, 'screen')); syncLayout(); });
$('lb-clear').addEventListener('click', () => boardStyle({ title: null, body: null, numbers: null, bg: null, panel: null, panelSlice: null, highlight: null, icons: null }));

for (const k of ['x', 'y', 'w', 'h']) $('layout-' + k).addEventListener('change', () => {
  if (!state.lsel) return;
  const r = { ...selRect() };
  r[k] = Math.round(Number($('layout-' + k).value) || 0);
  placeSel(r); layoutChanged();
});
$('layout-hide').addEventListener('click', () => { if (!state.lsel) return; state.layout = setPiece(state.layout, state.lboard, state.lsel, 'hidden'); state.lsel = null; layoutChanged(); });
$('layout-reset').addEventListener('click', () => { if (!state.lsel) return; state.layout = setPiece(state.layout, state.lboard, state.lsel, null); layoutChanged(); });

let drag = null;   // {name, mode, start, rect}
const canvasL = $('layout-canvas');
function devicePoint(e) { const S = SCREEN[state.lboard]; return toDevice(e.clientX, e.clientY, canvasL.getBoundingClientRect(), S.w, S.h); }
canvasL.addEventListener('pointerdown', (e) => {
  if (!preview) return;
  const p = devicePoint(e), rects = editRects(state.lboard);
  const selRect = state.lsel ? rects[state.lsel] : null;
  const corner = selRect ? handleAt(selRect, p.x, p.y, 8) : null;
  const name = corner ? state.lsel : pickPiece(rects, p.x, p.y);
  if (name || state.lmode === 'scan') state.lsel = name;   // an alert level keeps its card selected
  drag = name ? { name, mode: corner || 'move', start: p, rect: rects[name] } : null;
  if (drag) canvasL.setPointerCapture(e.pointerId);
  canvasL.focus(); syncLayout();
});
canvasL.addEventListener('pointermove', (e) => {
  if (!drag) return;
  const r = dragTo(drag.rect, drag.start, devicePoint(e), drag.mode, SCREEN[state.lboard], { square: drag.name === 'radar' });
  placeSel(r);
  changed(); syncLayout();
});
canvasL.addEventListener('pointerup', (e) => { if (drag) { drag = null; canvasL.releasePointerCapture(e.pointerId); layoutChanged(); } });
canvasL.addEventListener('keydown', (e) => {
  if (!state.lsel) return;
  const r0 = selRect();
  const r = r0 && nudge(r0, e.key, e.shiftKey);
  if (!r) return;
  e.preventDefault();
  placeSel(r); layoutChanged();
});
// The board's own frame (the device code through wasm), then outlines, the selection and its corner handles.
function paintLayout(ms) {
  const b = state.lboard, pb = engineBoard(b), alert = state.lscreen === 'scan' && state.lmode !== 'scan';
  // Play's start is a performance.now() time, so an alert's clock is that one; Scan keeps the page's clock (ms).
  if (alert) paint(canvasL, preview.renderDraft('alert', alertFrame(state.lmode, state.lplay, performance.now()), { board: pb, alertConf: state.lmode }), pb);
  else paint(canvasL, preview.renderDraft(state.lscreen, alertFrame('scan', null, ms), { board: pb }), pb);
  if (state.lscreen !== 'scan') return;   // the other screens: their style shows, the pieces are edited on Scan
  const ctx = canvasL.getContext('2d'), rects = editRects(b);
  if (alert && state.lplay === null) {    // the scan behind the card is dimmed: only the card is edited here
    const c = rects['alert.card.' + state.lmode], S = SCREEN[b];
    ctx.fillStyle = 'rgba(0,0,0,.45)';
    ctx.fillRect(0, 0, S.w, c.y); ctx.fillRect(0, c.y + c.h, S.w, S.h - c.y - c.h);
    ctx.fillRect(0, c.y, c.x, c.h); ctx.fillRect(c.x + c.w, c.y, S.w - c.x - c.w, c.h);
  }
  ctx.lineWidth = 1;
  for (const [n, r] of Object.entries(rects)) {
    if (!r || r.w <= 0) continue;
    ctx.strokeStyle = n === state.lsel ? '#ff4fcf' : 'rgba(180,150,255,.35)';
    ctx.strokeRect(r.x + 0.5, r.y + 0.5, r.w - 1, r.h - 1);
    if (state.layout.adjusted[b].includes(n)) { ctx.fillStyle = '#ffd04a'; ctx.fillRect(r.x + r.w - 5, r.y + 1, 4, 4); }
  }
  const s = state.lsel && rects[state.lsel];
  if (s) { ctx.fillStyle = '#ff4fcf'; for (const [x, y] of [[s.x, s.y], [s.x + s.w, s.y], [s.x, s.y + s.h], [s.x + s.w, s.y + s.h]]) ctx.fillRect(x - 3, y - 3, 6, 6); }
}
function buildPresets() {
  const host = $('layout-presets'); clear(host);
  for (const p of PRESETS) {
    const b = el('button', { type: 'button', role: 'radio', title: p.description, text: p.name });
    b.setAttribute('aria-checked', String(p.id === state.presetId));
    b.addEventListener('click', () => {
      state.presetId = p.id;
      for (const c of host.children) c.setAttribute('aria-checked', String(c === b));
      if (state.theme) { state.layout = p.layout ? fromJson(presetJson(p)) : newLayout(); state.lsel = null; layoutChanged(); }
    });
    host.append(b);
  }
}

async function boot() {
  buildPalette();
  syncTabs();
  fillLimits('design', DESIGN_PLACES);
  try { preview = await loadWasm(new URL('../wasm/uv_preview.js', import.meta.url).href); }
  catch (e) { preview = null; $('open-msg').textContent = ''; console.warn('preview unavailable', e); }
  recallBoard(); syncTabs();
  buildStart();
  buildPresets();
  $('gallery-link').href = THEMES_SITE;
  await openFromGallery();
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
