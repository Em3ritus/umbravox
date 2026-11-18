// Starting layouts for the Start page: very different starting points, each a layout for both engine boards that the
// device accepts (test: every preset passes checkLayout on both boards). Rects are board pixels: Waveshare 480x320,
// CYD engine build 320x240 (built-in places in ui/layout_engine.h).
const tiles = (xs, ys, ws, hs) => ({
  'count.wifi': [xs[0], ys[0], ws[0], hs[0]], 'count.5g': [xs[1], ys[1], ws[1], hs[1]], 'count.ble': [xs[2], ys[2], ws[2], hs[2]],
});

export const PRESETS = [
  { id: 'standard', name: 'Standard', description: 'The built-in layout (no layout file).', layout: null },
  {
    id: 'radar-left', name: 'Radar left', description: 'The radar on the left, the counts in a column on the right.',
    layout: {
      waveshare: { radar: [16, 56, 209, 209], channel: [240, 52, 100, 24], spectrum: [240, 194, 66, 74],
        ...tiles([364, 364, 364], [52, 124, 196], [100, 100, 100], [68, 68, 68]) },
      cyd: { radar: [8, 52, 115, 115], channel: [130, 52, 60, 16], spectrum: [8, 170, 140, 26],
        ...tiles([224, 224, 224], [52, 97, 142], [88, 88, 88], [45, 45, 46]) },
    },
  },
  {
    id: 'big-radar', name: 'Big radar', description: 'The radar alone in the middle; the counts are hidden.',
    layout: {
      waveshare: { radar: [136, 56, 209, 209], 'count.wifi': 'hidden', 'count.5g': 'hidden', 'count.ble': 'hidden' },
      cyd: { radar: [102, 52, 115, 115], 'count.wifi': 'hidden', 'count.5g': 'hidden', 'count.ble': 'hidden' },
    },
  },
  {
    id: 'numbers', name: 'Numbers', description: 'No radar: three big counts across the screen.',
    layout: {
      waveshare: { radar: 'hidden', spectrum: 'hidden', channel: 'hidden', badge: 'hidden',
        ...tiles([16, 168, 320], [52, 52, 52], [144, 144, 144], [208, 208, 208]) },
      cyd: { radar: 'hidden', spectrum: 'hidden', channel: 'hidden', badge: 'hidden',
        ...tiles([8, 112, 216], [52, 52, 52], [96, 96, 96], [136, 136, 136]) },
    },
  },
  {
    id: 'bottom-up', name: 'Bottom-up', description: 'The header along the bottom and the buttons along the top.',
    layout: {
      waveshare: { menu: [0, 276, 44, 44], wordmark: [44, 276, 164, 44], 'status.gps': [208, 276, 56, 44], 'status.nodes': [264, 276, 48, 44],
        bridge: [312, 276, 64, 44], sd: [376, 276, 48, 44], 'status.power': [424, 276, 56, 44],
        'nav.scan': [0, 0, 96, 44], 'nav.domain': [96, 0, 96, 44], 'nav.codex': [192, 0, 96, 44], 'nav.poi': [288, 0, 96, 44], stop: [384, 0, 96, 44] },
      cyd: { menu: [0, 196, 44, 44], wordmark: [44, 196, 68, 44], 'status.gps': [112, 196, 44, 44], 'status.nodes': [156, 196, 44, 44],
        bridge: [200, 196, 44, 44], sd: [244, 196, 32, 44], 'status.power': [276, 196, 44, 44],
        'nav.scan': [0, 0, 60, 44], 'nav.domain': [60, 0, 80, 44], 'nav.codex': [140, 0, 68, 44], 'nav.poi': [208, 0, 52, 44], stop: [260, 0, 60, 44] },
    },
  },
];

// A preset as layout.json text (null for Standard).
export const presetJson = (p) => (p.layout ? JSON.stringify({ format: 1, ...p.layout }) : null);
