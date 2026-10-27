// Shared helpers for the designer tests (not a test file itself).
import { fileURLToPath } from 'node:url';
import path from 'node:path';
import fs from 'node:fs';
import os from 'node:os';
import { spawnSync } from 'node:child_process';

export const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
export const read = (rel) => fs.readFileSync(path.join(ROOT, rel), 'utf8');
export const SIM = path.join(ROOT, 'sim/umbravox');

export function ensureSim() {
  if (fs.existsSync(SIM)) return;
  const r = spawnSync('make', ['-C', path.join(ROOT, 'sim')], { encoding: 'utf8' });
  if (r.status !== 0 || !fs.existsSync(SIM)) {
    throw new Error('cannot build sim/umbravox with make -C sim:\n' + r.stdout + r.stderr);
  }
}

// Write a built pack under a fresh temp dir; returns the pack folder path.
export function writePack(pack) {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'uvx-pack-'));
  let folder = null;
  for (const f of pack.files) {
    const p = path.join(root, f.path);
    fs.mkdirSync(path.dirname(p), { recursive: true });
    fs.writeFileSync(p, f.data);
    folder = path.join(root, f.path.split('/')[0]);
  }
  return folder;
}

export function checkPack(folder) {
  ensureSim();
  const r = spawnSync(SIM, ['--check-pack', folder], { encoding: 'utf8' });
  return { status: r.status, out: (r.stdout + r.stderr).trim() };
}

// Deterministic photo-like RGBA image: smooth gradients plus a little noise.
export function gradient(w, h) {
  const d = new Uint8ClampedArray(w * h * 4);
  let s = 12345;
  for (let y = 0; y < h; y++) for (let x = 0; x < w; x++) {
    s = (s * 1103515245 + 12345) >>> 0;
    const n = (s >>> 16) % 24;
    const i = 4 * (y * w + x);
    d[i] = (x * 255 / w + n) & 255; d[i + 1] = (y * 255 / h + n) & 255;
    d[i + 2] = ((x + y) * 255 / (w + h) + n) & 255; d[i + 3] = 255;
  }
  return d;
}
