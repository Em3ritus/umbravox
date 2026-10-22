// Colour helpers shared by the designer: RGB565 packing, the colour the panel
// really shows, the device's contrast formula, and a one-colour palette.

export const ROLES = ['void', 'panel', 'grid', 'edge', 'edgeLit', 'dim', 'text', 'bright',
  'witch', 'witchHi', 'bone', 'sweep', 'sweepHi', 'live', 'liveHi', 'magenta', 'ice', 'green', 'amber', 'red'];

export const isHex = (s) => typeof s === 'string' && /^#[0-9A-Fa-f]{6}$/.test(s);

function rgb(hex) {
  return [parseInt(hex.slice(1, 3), 16), parseInt(hex.slice(3, 5), 16), parseInt(hex.slice(5, 7), 16)];
}
const toHex = (r, g, b) =>
  '#' + [r, g, b].map((v) => Math.max(0, Math.min(255, Math.round(v))).toString(16).padStart(2, '0')).join('').toUpperCase();

// Same packing as the device parser: R5 G6 B5 from the high bits.
export function toRgb565(hex) {
  const [r, g, b] = rgb(hex);
  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

// 8-bit channels the panel shows for an RGB565 value (high bits replicated).
export function expand565(c) {
  const r5 = c >> 11, g6 = (c >> 5) & 63, b5 = c & 31;
  return [(r5 << 3) | (r5 >> 2), (g6 << 2) | (g6 >> 4), (b5 << 3) | (b5 >> 2)];
}

export function clamp565(hex) {
  return toHex(...expand565(toRgb565(hex)));
}

// ---- contrast: WCAG relative luminance of the colour the panel shows ----
const lin = (v8) => {
  const c = v8 / 255;
  return c <= 0.03928 ? c / 12.92 : Math.pow((c + 0.055) / 1.055, 2.4);
};
function luminance565(c) {
  const [r, g, b] = expand565(c);
  return 0.2126 * lin(r) + 0.7152 * lin(g) + 0.0722 * lin(b);
}
function ratio565(a, b) {
  let la = luminance565(a), lb = luminance565(b);
  if (la < lb) [la, lb] = [lb, la];
  return (la + 0.05) / (lb + 0.05); // the +0.05 keeps the divisor above zero
}
// ui/contrast.h contrastX10: ratio x10, truncated.
export const contrastX10 = (a565, b565) => Math.floor(ratio565(a565, b565) * 10 + 0.0001);
// The ratio of two #RRGGBB colours as the panel will show them (3 decimals).
export const contrastRatio = (a, b) => Math.round(ratio565(toRgb565(a), toRgb565(b)) * 1000) / 1000;
// Truncated to one decimal, like the device, for messages: 4.46 shows as 4.4.
export const fmtRatio = (r) => (Math.floor(r * 10 + 1e-9) / 10).toFixed(1) + ':1';

// ---- HSL ----
function toHsl(hex) {
  const [r, g, b] = rgb(hex).map((v) => v / 255);
  const mx = Math.max(r, g, b), mn = Math.min(r, g, b), l = (mx + mn) / 2, d = mx - mn;
  if (d === 0) return [0, 0, l];
  const s = d / (1 - Math.abs(2 * l - 1));
  let h = mx === r ? ((g - b) / d) % 6 : mx === g ? (b - r) / d + 2 : (r - g) / d + 4;
  h = (h * 60 + 360) % 360;
  return [h, s, l];
}
function fromHsl(h, s, l) {
  h = ((h % 360) + 360) % 360; s = Math.max(0, Math.min(1, s)); l = Math.max(0, Math.min(1, l));
  const c = (1 - Math.abs(2 * l - 1)) * s, x = c * (1 - Math.abs(((h / 60) % 2) - 1)), m = l - c / 2;
  const [r, g, b] = h < 60 ? [c, x, 0] : h < 120 ? [x, c, 0] : h < 180 ? [0, c, x]
    : h < 240 ? [0, x, c] : h < 300 ? [x, 0, c] : [c, 0, x];
  return toHex((r + m) * 255, (g + m) * 255, (b + m) * 255);
}

// Raise lightness until `hex` reads at `min`:1 against every background.
function lift(h, s, l, bgs, min) {
  let hex = fromHsl(h, s, l);
  while (l < 1 && bgs.some((bg) => contrastRatio(hex, bg) < min)) {
    l = Math.min(1, l + 0.02);
    hex = fromHsl(h, s, l);
  }
  return hex;
}

// One colour -> a complete dark-theme palette. Deterministic. Every role that
// carries text or an alert edge is lifted until it reads against void and panel.
export function fromAccent(accent) {
  const [h, s0] = toHsl(accent);
  const s = Math.max(0, Math.min(1, s0));
  const sec = Math.max(s, 0.6); // secondaries stay coloured even for a grey accent
  const voidC = fromHsl(h, Math.min(s, 0.6) * 0.8, 0.05);
  const panel = fromHsl(h, Math.min(s, 0.6) * 0.7, 0.10);
  const bgs = [voidC, panel];
  const witch = lift(h, s, Math.max(0.5, toHsl(accent)[2]), bgs, 4.5);
  const [, , wl] = toHsl(witch);
  return {
    void: voidC,
    panel,
    grid: fromHsl(h, Math.min(s, 0.6) * 0.7, 0.14),
    edge: fromHsl(h, Math.min(s, 0.6) * 0.6, 0.26),
    edgeLit: lift(h, s, Math.min(1, wl + 0.06), bgs, 4.5),
    dim: lift(h, Math.min(s, 0.35), 0.6, bgs, 4.5),
    text: lift(h, Math.min(s, 0.5) * 0.6, 0.9, bgs, 7),
    bright: '#FFFFFF',
    witch,
    witchHi: lift(h + 30, sec, 0.62, bgs, 4.5),
    bone: lift(45, 0.9, 0.65, bgs, 4.5),
    sweep: lift(h + 180, sec, 0.55, bgs, 4.5),
    sweepHi: lift(h + 180, sec, 0.8, bgs, 7),
    live: lift(h + 330, sec, 0.65, bgs, 4.5),
    liveHi: lift(h + 330, 0.9, 0.9, bgs, 7),
    magenta: lift(315, 0.9, 0.6, bgs, 4.5),
    ice: lift(180, 0.85, 0.6, bgs, 4.5),
    green: lift(150, 0.9, 0.62, bgs, 4.5),
    amber: '#F59E0B',
    red: '#EF4444',
  };
}
