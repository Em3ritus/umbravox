// layoutmedia.js: a layout's fonts and pictures as pack files. The model (js/layout.js) refers to fonts and pictures
// by source key; this gives each distinct font an f0..f7.fnt and each picture-at-a-size an a0..a15.art, converts the
// pictures at exactly the size they are placed (js/artfmt.js), and writes layout.json naming those files. The live
// preview and the saved pack both come from here, so what the author sees is what the device gets.
//
// media = { fonts: { key: Uint8Array (.fnt) }, pictures: { key: { w, h, frames: [RGBA], delays: [ms] } } }
import { toJson } from './layout.js';
import { imageToArt, artToBin, ART_MIN_DELAY_MS, MAX_ART_W, MAX_ART_H } from './artfmt.js';

export const MAX_FONTS = 8, MAX_ARTS = 16, MAX_ART_TOTAL = 393216;
const PANEL_MAX = 64, ICON_MAX = 40;   // a panel frame's or a menu icon's largest side (designer's choice)

export function packMedia(L, names, media) {
  const fonts = new Map(), pics = new Map(), files = [], errors = [], total = { waveshare: 0, cyd: 0 };
  const err = (m) => { if (!errors.includes(m)) errors.push(m); };
  const fit = (src, maxW, maxH) => {   // keep the picture's aspect, no larger than the box and never scaled up
    const s = Math.min(1, maxW / src.w, maxH / src.h);
    return [Math.max(1, Math.round(src.w * s)), Math.max(1, Math.round(src.h * s))];
  };
  const refs = {
    font(key) {
      if (fonts.has(key)) return fonts.get(key);
      const bin = media.fonts?.[key];
      if (!bin) { err(`the font ${key} is not loaded`); return 'f0'; }
      if (fonts.size >= MAX_FONTS) { err(`a theme can use at most ${MAX_FONTS} fonts (each font at each size counts once)`); return 'f0'; }
      const stem = 'f' + fonts.size;
      fonts.set(key, stem);
      files.push({ name: stem + '.fnt', data: bin });
      return stem;
    },
    pic(key, board, w, h, opts = {}) {
      const src = media.pictures?.[key];
      if (!src) { err(`the picture ${key} is not loaded`); return 'a0'; }
      if (opts.frameFor) [w, h] = fit(src, opts.frameFor.w, opts.frameFor.h);
      else if (opts.panel) [w, h] = fit(src, PANEL_MAX, PANEL_MAX);
      else if (opts.icon) [w, h] = fit(src, ICON_MAX, ICON_MAX);
      w = Math.min(w, MAX_ART_W); h = Math.min(h, MAX_ART_H);
      const transparent = opts.transparent !== false;
      const id = `${key}|${board}|${w}x${h}|${transparent}`;
      if (pics.has(id)) return pics.get(id);
      if (pics.size >= MAX_ARTS) { err(`a theme can use at most ${MAX_ARTS} pictures (each picture at each size on each board counts once)`); return 'a0'; }
      let data;
      try {
        data = artToBin(imageToArt(src.frames, src.w, src.h, { w, h, delays: src.delays, transparent, minDelay: ART_MIN_DELAY_MS[board] }));
      } catch (e) { err(`${key} at ${w}x${h}: ${e.message}`); return 'a0'; }
      const stem = 'a' + pics.size;
      pics.set(id, stem);
      files.push({ name: stem + '.art', data });
      total[board] += data.length;
      return stem;
    },
  };
  const json = toJson(L, names, refs);
  for (const b of ['waveshare', 'cyd'])
    if (total[b] > MAX_ART_TOTAL) err(`the ${b === 'cyd' ? 'CYD' : 'Waveshare'} pictures come to ${total[b]} bytes, over the device's ${MAX_ART_TOTAL}`);
  return { json, files, errors };
}
