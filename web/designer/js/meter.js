// The speed meter (plan 5c). Pure: no DOM.
//
// The counts come from the device's own code in wasm (preview.measure -> uv_meter -> ui/meter.h): pixels drawn and
// pushed per frame, replayed the way the board's engine draws. The fit turns them into milliseconds; it was measured
// on the boards (tools/meterprobe.py, tools/meterfit.py) and ships as js/meter-fit.json together with the limits.
// Nothing here knows a number of its own: a board the fit lacks is "uncalibrated" and never blocks.

export const METER_BOARDS = ['waveshare', 'cyd-engine'];
export const METER_MODES = ['scan', 'low', 'med', 'high'];
export const METER_FRAMES = 30;
const SKIP_SCAN = 2;          // the entry repaint is not a layout's steady cost; an alert's first frame is real
const AMBER = 80;
const NAME = { waveshare: 'Waveshare', 'cyd-engine': 'CYD' };

const num = (x) => typeof x === 'number' && Number.isFinite(x);

// {ok:true, fit} or {ok:false, why}. A broken fit is refused here, never shown as 0 ms.
export function loadFit(j) {
  if (!j || typeof j !== 'object' || j.version !== 1 || !j.boards || typeof j.boards !== 'object') return { ok: false, why: 'meter-fit.json: not a version 1 fit' };
  for (const [b, f] of Object.entries(j.boards)) {
    const L = f && f.limits;
    if (!f || !num(f.a) || !num(f.b) || !num(f.c) || f.a < 0 || f.b < 0) return { ok: false, why: `meter-fit.json: ${b}: a, b and c must be numbers, a and b not negative` };
    if (!num(f.frameMs) || f.frameMs < 1 || !L || !num(L.scanMs) || !num(L.alertMs) || L.scanMs <= 0 || L.alertMs <= 0) return { ok: false, why: `meter-fit.json: ${b}: frameMs and limits must be positive numbers` };
  }
  return { ok: true, fit: j };
}

// measure(board, mode, frames, frameMs) -> [{drawn, pushed}] for each board and mode, at the fit's frame period.
export function measureAll(measure, fit) {
  const out = {};
  for (const b of METER_BOARDS) {
    const frameMs = (fit && fit.boards && fit.boards[b] && fit.boards[b].frameMs) || 20;
    out[b] = {};
    for (const m of METER_MODES) out[b][m] = measure(b, m, METER_FRAMES, frameMs);
  }
  return out;
}

const round1 = (x) => Math.round(x * 10) / 10;

// {rows: [{board, kind, level, ms, limit, pct, state}], over, reason}. One scan row and one alert row (the worst
// level) per measured board. state: ok | amber (from 80%) | over (past 100%) | uncalibrated.
export function meterRows(fit, measured) {
  const rows = [];
  for (const b of METER_BOARDS) {
    const m = measured && measured[b];
    if (!m) continue;
    const f = fit && fit.boards && fit.boards[b];
    const cost = (fr) => (f ? f.a * fr.drawn + f.b * fr.pushed + f.c : 0);
    const worst = (list, from) => (list || []).slice(from).reduce((w, fr) => Math.max(w, cost(fr)), 0);
    const scanMs = worst(m.scan, SKIP_SCAN);
    let level = 'low', alertMs = -1;
    for (const l of ['low', 'med', 'high']) { const w = worst(m[l], 0); if (w > alertMs) { alertMs = w; level = l; } }
    for (const [kind, ms, lv] of [['scan', scanMs, null], ['alert', alertMs, level]]) {
      if (!f) { rows.push({ board: b, kind, level: lv, ms: null, limit: null, pct: null, state: 'uncalibrated' }); continue; }
      const limit = kind === 'scan' ? f.limits.scanMs : f.limits.alertMs;
      const pct = (ms / limit) * 100;
      rows.push({ board: b, kind, level: lv, ms: round1(ms), limit, pct, state: pct > 100 ? 'over' : pct >= AMBER ? 'amber' : 'ok' });
    }
  }
  const bad = rows.find((r) => r.state === 'over');
  const what = (r) => `${NAME[r.board]} ${r.kind === 'scan' ? 'scan' : `alert (${r.level.toUpperCase()})`}`;
  return { rows, over: !!bad, reason: bad ? `Speed: ${what(bad)} is ${bad.ms} ms, over its ${bad.limit} ms limit.` : null };
}

export const meterLabel = (r) => `${NAME[r.board]} ${r.kind === 'scan' ? 'scan' : `alert ${r.level.toUpperCase()}`}`;
