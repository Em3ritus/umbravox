// tools/video2radar.py writes the same radar.vid the designer does: this holds the two encoders to the same bytes.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import path from 'node:path';
import { spawnSync } from 'node:child_process';
import { ROOT } from './_util.mjs';
import { buildClip, clipFile, CLIP_SIZE } from '../js/radarvideo.js';

const TOOL = path.join(ROOT, 'tools/video2radar.py');
const level = (f, i) => (i * 7 + f * 3 + (i >> 5)) % 16;

test('video2radar.py --selftest', () => {
  const r = spawnSync('python3', ['-I', TOOL, '--selftest'], { encoding: 'utf8' });
  assert.equal(r.status, 0, r.stdout + r.stderr);
});

test('the tool and the designer encode the same radar.vid for the same levels', () => {
  const frames = 4, fps = 12;
  const r = spawnSync('python3', ['-I', TOOL, '--emit-test-vid', String(frames), String(fps)], { encoding: null, maxBuffer: 1 << 24 });
  assert.equal(r.status, 0, String(r.stderr));
  const js = clipFile(buildClip(Array.from({ length: frames }, (_, f) => Uint8Array.from({ length: CLIP_SIZE * CLIP_SIZE }, (_, i) => level(f, i)))));
  assert.deepEqual(new Uint8Array(r.stdout), js);
});
