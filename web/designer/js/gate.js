// Is this theme allowed to reach the Install view? Pure: no DOM.
import { runChecks } from './checks.js';
import { buildPack } from './pack.js';

// {ok:true} or {ok:false, reason}. Any error-level check blocks (warnings do not),
// and so does buildPack refusing the theme.
export function installGate(theme, emblem) {
  const errs = runChecks(theme, emblem).filter((c) => c.level === 'error');
  if (errs.length) return { ok: false, reason: 'Fix this first: ' + errs.map((e) => e.text).join(' ') };
  try { buildPack(theme, emblem); } catch (e) { return { ok: false, reason: 'The pack cannot be built: ' + e.message }; }
  return { ok: true };
}
