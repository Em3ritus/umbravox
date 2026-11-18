// Is this theme allowed to reach the Install view? Pure: no DOM.
import { runChecks } from './checks.js';
import { buildPack } from './pack.js';

// {ok:true} or {ok:false, reason}. Any error-level check blocks (warnings do not),
// and so does buildPack refusing the theme. layout: the layout.json text the pack would carry; checkLayout: the
// device's own verdict (js/preview.js checkLayout) -- a layout the device would refuse never reaches a card.
// meter: js/meter.js meterRows() for this layout; over its limit on any board blocks (amber and uncalibrated do not).
export function installGate(theme, emblem, clip = null, sounds = null, layout = null, checkLayout = null, meter = null) {
  const errs = runChecks(theme, emblem, clip, sounds).filter((c) => c.level === 'error');
  if (errs.length) return { ok: false, reason: 'Fix this first: ' + errs.map((e) => e.text).join(' ') };
  if (layout && checkLayout) {
    const v = checkLayout(layout);
    if (!v.ok) return { ok: false, reason: 'Fix this first: Layout: ' + String(v.why).replace(/^layout\.json: /, '') };
  }
  if (meter && meter.over) return { ok: false, reason: 'Fix this first: ' + meter.reason };
  try { buildPack(theme, emblem, clip, sounds, layout); } catch (e) { return { ok: false, reason: 'The pack cannot be built: ' + e.message }; }
  return { ok: true };
}
