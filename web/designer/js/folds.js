// The Layout step's folded sections (Fonts and pictures, Other screens, Hidden pieces): the one line each shows next
// to its heading, so a closed section still says what is in it. DOM-free, so node:test covers it.
import { decorNames } from './layout.js';

const SCREEN_NAME = { scan: 'Scan', mesh: 'Mesh', menu: 'Menu', aspect: 'Aspect' };
const plural = (n, one) => `${n} ${one}${n === 1 ? '' : 's'}`;

// opts: { fonts: uploaded font count, names: the device's piece names, screen: the previewed screen }.
export function foldSummaries(L, board, { fonts = 0, names = [], screen = 'scan' } = {}) {
  const pics = decorNames(L, board).length;
  const media = [fonts ? plural(fonts, 'font') : '', pics ? plural(pics, 'picture') : ''].filter(Boolean).join(' · ') || 'no uploads';

  const bs = (L.style && L.style[board]) || {};
  const set = [['title', 'titles'], ['body', 'text'], ['numbers', 'numbers']].filter(([k]) => bs[k]).map(([, w]) => w);
  if (bs.highlight && (bs.highlight.fill || bs.highlight.edge)) set.push('highlight');
  if (bs.bg) set.push('background');
  if (bs.panel) set.push('panel');
  if (Array.isArray(bs.icons) && bs.icons.some(Boolean)) set.push('icons');
  const screens = `${set.length ? set.join(', ') : 'built-in look'} · preview ${SCREEN_NAME[screen] || screen}`;

  const hiddenN = names.filter((n) => L.boards[board][n] && L.boards[board][n].hidden).length;
  return { media, screens, hidden: hiddenN ? `${hiddenN} hidden` : 'none' };
}
