// The speed meter (plan 5c). Pure: no DOM.
//
// The counts come from the device's own code in wasm (preview.measure -> uv_meter -> ui/meter.h): pixels drawn and
// pushed per frame, replayed the way the board's engine draws. The fits turn them into milliseconds: one model for
// scan frames and one for alert frames per board (an alert pixel costs 2-2.5x a scan pixel), measured on the boards
// (tools/meterprobe.py, tools/meterfit.py) and shipped as js/meter-fit.json with the limits. Each row is judged on
// its p95 frame (nearest rank): the single worst frame is ~3x the median on both boards and mostly noise.
// The scan limit is the built-in theme's PREDICTED scan p95 + 15%, so the fit's bias (6-13% under the board on
// every calibration pack) cancels out. The alert row is rough (34% under to 40% over the board): it warns, never blocks.
// Nothing here knows a number of its own: a board the fit lacks is "uncalibrated" and never blocks.

export const METER_BOARDS = ['waveshare', 'cyd-engine'];
export const METER_MODES = ['scan', 'low', 'med', 'high'];
export const METER_FRAMES = 30;
const SKIP_SCAN = 2;          // the entry repaint is not a layout's steady cost; an alert's first frame is real
                              // (ui/meter.h raises it over a drawn scan screen)
const AMBER = 80;
export const SCAN_MARGIN = 1.15;   // scan limit = the built-in theme's predicted scan p95 + 15% (tools/meter-limits.mjs)
const NAME = { waveshare: 'Waveshare', 'cyd-engine': 'CYD' };

const num = (x) => typeof x === 'number' && Number.isFinite(x);

// {ok:true, fit} or {ok:false, why}. A broken fit is refused here, never shown as 0 ms.
const model = (m) => !!m && num(m.a) && num(m.b) && num(m.c) && m.a >= 0 && m.b >= 0;

export function loadFit(j) {
  if (!j || typeof j !== 'object' || j.version !== 2 || !j.boards || typeof j.boards !== 'object') return { ok: false, why: 'meter-fit.json: not a version 2 fit' };
  for (const [b, f] of Object.entries(j.boards)) {
    const L = f && f.limits;
    if (!f || !model(f.scan) || !model(f.alert)) return { ok: false, why: `meter-fit.json: ${b}: scan and alert need a, b and c as numbers, a and b not negative` };
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

// Nearest-rank 95th percentile (the same rule as tools/meterfit.py's limits); 0 for no frames.
export function p95(xs) {
  if (!xs.length) return 0;
  const s = [...xs].sort((x, y) => x - y);
  return s[Math.max(0, Math.ceil(0.95 * s.length) - 1)];
}

// {rows: [{board, kind, level, ms, limit, pct, state, rough}], over, reason}. One scan row and one alert row (the
// level with the highest p95) per measured board; ms is that p95 frame. state: ok | amber (from 80%) | over (past
// 100%, scan only: an alert row past its limit stays amber) | uncalibrated. rough: the alert model is too coarse to block.
export function meterRows(fit, measured) {
  const rows = [];
  for (const b of METER_BOARDS) {
    const m = measured && measured[b];
    if (!m) continue;
    const f = fit && fit.boards && fit.boards[b];
    const cost = (k, fr) => k.a * fr.drawn + k.b * fr.pushed + k.c;
    const judge = (k, list, from) => (f ? p95((list || []).slice(from).map((fr) => cost(k, fr))) : 0);
    const scanMs = judge(f && f.scan, m.scan, SKIP_SCAN);
    let level = 'low', alertMs = -1;
    for (const l of ['low', 'med', 'high']) { const w = judge(f && f.alert, m[l], 0); if (w > alertMs) { alertMs = w; level = l; } }
    for (const [kind, ms, lv] of [['scan', scanMs, null], ['alert', alertMs, level]]) {
      const rough = kind === 'alert';
      if (!f) { rows.push({ board: b, kind, level: lv, ms: null, limit: null, pct: null, state: 'uncalibrated', rough }); continue; }
      const limit = kind === 'scan' ? f.limits.scanMs : f.limits.alertMs;
      const pct = (ms / limit) * 100;
      const state = pct > 100 && !rough ? 'over' : pct >= AMBER ? 'amber' : 'ok';
      rows.push({ board: b, kind, level: lv, ms: round1(ms), limit, pct, state, rough });
    }
  }
  const bad = rows.find((r) => r.state === 'over');
  const what = (r) => `${NAME[r.board]} ${r.kind === 'scan' ? 'scan' : `alert (${r.level.toUpperCase()})`}`;
  return { rows, over: !!bad, reason: bad ? `Speed: ${what(bad)} is ${bad.ms} ms, over its ${bad.limit} ms limit (p95 frame).` : null };
}

// The scan limit for a board: the built-in theme's (theme 0, no pack) scan frames, as measure() returns them at the
// fit's frame period, through the board's scan model; p95 from frame SKIP_SCAN, + 15%. null for a board the fit lacks.
export function builtinScanLimit(fit, board, frames) {
  const f = fit && fit.boards && fit.boards[board];
  if (!f) return null;
  const k = f.scan;
  return round1(p95(frames.slice(SKIP_SCAN).map((fr) => k.a * fr.drawn + k.b * fr.pushed + k.c)) * SCAN_MARGIN);
}

export const meterLabel = (r) => `${NAME[r.board]} ${r.kind === 'scan' ? 'scan' : `alert ${r.level.toUpperCase()}`}`;

// The panel's line for one row.
export function meterText(r) {
  if (r.state === 'uncalibrated') return `${meterLabel(r)}: not calibrated`;
  return `${meterLabel(r)}: ~${r.ms} ms of ${r.limit} ms${r.rough ? ' (rough: warns, never blocks)' : ''}`;
}
