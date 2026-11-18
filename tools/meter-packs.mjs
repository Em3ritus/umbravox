// The speed meter's calibration packs (plan 5c), for tools/meterprobe.py on both boards:
//
//   node tools/meter-packs.mjs OUT_DIR        writes OUT_DIR/{meter-small,stress-p4,alert-fx,meter-fullcard}/
//
// meter-small: one moved count tile, nothing else (the cheap end). stress-p4: the largest pack the format allows
// (tools/stress-pack.mjs). alert-fx: every alert feature (tools/alert-pack.mjs). meter-fullcard: a whole-screen HIGH
// card on both boards and fast flash effects on the board's cap of pieces (the expensive alert end). The probe
// measures the built-in theme with no pack (THEME 0) as well. The fit needs the spread these give in pixels drawn
// and pushed, not any one look.
import fs from 'node:fs';
import path from 'node:path';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
const repo = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const out = process.argv[2];
if (!out) { console.error('usage: meter-packs.mjs OUT_DIR'); process.exit(2); }
fs.mkdirSync(out, { recursive: true });

for (const tool of ['stress-pack.mjs', 'alert-pack.mjs']) {
  const r = spawnSync(process.execPath, [path.join(repo, 'tools', tool), out], { encoding: 'utf8' });
  if (r.status !== 0) { console.error(`${tool} failed:\n${r.stdout}${r.stderr}`); process.exit(1); }
}

const base = JSON.parse(fs.readFileSync(path.join(repo, 'packs/obsidian/theme.json'), 'utf8'));
function write(folder, name, layout) {
  const dir = path.join(out, folder);
  fs.rmSync(dir, { recursive: true, force: true }); fs.mkdirSync(dir, { recursive: true });
  const theme = { ...base, name, header: name, author: 'probe' };
  fs.writeFileSync(path.join(dir, 'theme.json'), JSON.stringify(theme, null, 2) + '\n');
  fs.writeFileSync(path.join(dir, 'layout.json'), JSON.stringify(layout) + '\n');
  console.log('wrote', dir);
}

write('meter-small', 'METER SMALL', { format: 1,
  waveshare: { 'count.wifi': { rect: [16, 52, 100, 68] } },
  cyd: { 'count.ble': { rect: [8, 142, 88, 46] } },
});
write('meter-fullcard', 'METER FULLCARD', { format: 1,
  waveshare: {
    'alert.card.high': { rect: [0, 0, 480, 320] },
    'count.wifi': { rect: [16, 52, 100, 68], effect: 'flash', speed: 'fast' },
    'count.ble': { rect: [16, 196, 100, 68], effect: 'flash', speed: 'fast' },
    stop: { rect: [384, 276, 96, 44], effect: 'flash', speed: 'fast' },
    'nav.scan': { rect: [0, 276, 96, 44], effect: 'flash', speed: 'fast' },
  },
  cyd: {
    'alert.card.high': { rect: [0, 0, 320, 240] },
    'count.ble': { rect: [8, 142, 88, 46], effect: 'flash', speed: 'fast' },
    stop: { rect: [260, 196, 60, 44], effect: 'flash', speed: 'fast' },
  },
});
