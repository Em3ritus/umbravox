// Plan 5c: tools/meter-packs.mjs writes the speed meter's calibration packs, and the device code accepts each layout.
import { test, before } from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { spawnSync } from 'node:child_process';
import { pathToFileURL } from 'node:url';
import { ROOT } from './_util.mjs';
import { createPreview } from '../js/preview.js';

let preview;
before(async () => {
  const r = spawnSync('bash', [path.join(ROOT, 'tools/build-designer-wasm.sh')], { encoding: 'utf8' });
  assert.equal(r.status, 0, r.stdout + r.stderr);
  const M = await (await import(pathToFileURL(path.join(ROOT, 'web/designer/wasm/uv_preview.js')).href)).default();
  preview = createPreview(M);
});

test('meter-packs.mjs writes the four calibration packs with layouts the device accepts', () => {
  const out = fs.mkdtempSync(path.join(os.tmpdir(), 'uvx-meter-'));
  const r = spawnSync('node', [path.join(ROOT, 'tools/meter-packs.mjs'), out], { encoding: 'utf8' });
  assert.equal(r.status, 0, r.stdout + r.stderr);
  const want = ['alert-fx', 'meter-fullcard', 'meter-small', 'stress-p4'];
  assert.deepEqual(fs.readdirSync(out).sort(), want);
  const names = new Set();
  for (const d of want) {
    const theme = JSON.parse(fs.readFileSync(path.join(out, d, 'theme.json'), 'utf8'));
    names.add(theme.name);
    const layout = fs.readFileSync(path.join(out, d, 'layout.json'), 'utf8');
    const v = preview.checkLayout(layout);
    assert.ok(v.ok, `${d}: ${v.why}`);
  }
  assert.equal(names.size, 4, 'four distinct theme names');
  const full = JSON.parse(fs.readFileSync(path.join(out, 'meter-fullcard', 'layout.json'), 'utf8'));
  assert.deepEqual(full.waveshare['alert.card.high'].rect, [0, 0, 480, 320]);
  assert.deepEqual(full.cyd['alert.card.high'].rect, [0, 0, 320, 240]);
});
