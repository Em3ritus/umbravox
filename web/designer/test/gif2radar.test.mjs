// tools/gif2radar.py turns the COSMIC MUFFIN gif into the colour radar.vid compiled into the firmware. Its own selftest builds a
// small gif in memory and holds the output to the radar.vid v2 layout themepack::parseClip reads.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import path from 'node:path';
import { spawnSync } from 'node:child_process';
import { ROOT } from './_util.mjs';

const TOOL = path.join(ROOT, 'tools/gif2radar.py');

test('gif2radar.py --selftest', () => {
  const r = spawnSync('python3', ['-I', TOOL, '--selftest'], { encoding: 'utf8' });
  assert.equal(r.status, 0, r.stdout + r.stderr);
  assert.match(r.stdout, /all passed/);
});
