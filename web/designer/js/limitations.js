// Which limitations the page shows where. Pure: the text comes from limits.json
// and nothing here knows the DOM, so the tests can assert on strings.

export const escapeHtml = (s) => String(s).replace(/[&<>"']/g, (c) =>
  ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]));

// Every entry whose `where` includes `view`, in limits.json order.
export function limitsFor(limits, view) {
  return Object.values(limits.limitations).filter((l) => l.where.includes(view));
}

// Which Design panel each limitation sits beside. An entry limits.json gains
// later with no place here lands in `rest` instead of vanishing.
export const DESIGN_PLACES = {
  characters: 'identity', header: 'identity',
  rgb565: 'palette',
  centrepiece: 'centre',
  presets: 'motion', builtins: 'motion', alerts: 'motion',
};

export function placeLimitations(limits, view, places) {
  const out = { rest: [] };
  for (const l of limitsFor(limits, view)) (out[places[l.id] || 'rest'] ||= []).push(l);
  return out;
}

export function renderLimitList(entries) {
  if (!entries.length) return '';
  return '<ul class="limits">' +
    entries.map((l) => '<li data-limit="' + escapeHtml(l.id) + '">' + escapeHtml(l.text) + '</li>').join('') +
    '</ul>';
}
