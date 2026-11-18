// The Layout step's alert mode (plan 5b): Scan, or one alert level whose card is edited over the scan. DOM-free; the
// drawing is the device code (js/preview.js renderDraft('alert', frame, {alertConf})), whose frame * 20 ms is the
// alert's age.
import { fxCount, LEVELS } from './layout.js';

export const MODES = ['scan', ...LEVELS];
export const PLAY_MS = 5000;    // one alert of the console TEST (main.cpp TEST_SEQ)
export const STILL_MS = 3000;   // a settled alert: its opening has played and the words are resolved

// The preview frame to draw: Scan runs the clock; an alert level stands still at STILL_MS unless Play started it
// at playStart, then loops every PLAY_MS from age 0.
export function alertFrame(mode, playStart, nowMs) {
  if (mode === 'scan') return Math.floor(nowMs / 20);
  if (playStart === null || playStart === undefined) return STILL_MS / 20;
  return Math.floor(((((nowMs - playStart) % PLAY_MS) + PLAY_MS) % PLAY_MS) / 20);
}

// 'board' or 'screen' (the author changed board or left the Scan screen) -> back to Scan; 'mode:<m>' -> that mode.
// Play always stops.
export function nextMode(mode, event) {
  if (event === 'board' || event === 'screen') return { mode: 'scan', playing: false };
  const m = /^mode:(.+)$/.exec(event);
  if (!m || !MODES.includes(m[1])) throw new Error('unknown layout mode event ' + event);
  return { mode: m[1], playing: false };
}

// What the canvas can select and drag: the pieces on Scan, only this level's card in an alert mode.
export function editableRects(mode, pieceRects, cardRect) {
  return mode === 'scan' ? pieceRects : { ['alert.card.' + mode]: cardRect };
}

// May this piece's effect be set? Always if it has one already; otherwise only under the board's cap.
export function effectAllowed(L, board, piece, cap) {
  const p = L.boards[board][piece];
  if (p && !p.hidden && p.style?.effect) return true;
  return fxCount(L, board) < cap;
}
