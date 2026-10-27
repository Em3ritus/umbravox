// The "Install over USB" client (js/usbinstall.js): the wire format against
// tools/sdput.py's golden transcript, the reply and SDLS grammars, the
// refusals, a fake device with every fault a USB link has, and finally the
// REAL C++ state machine (device/sdcmd.h) through sim/sdcmd_console.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { spawn, spawnSync } from 'node:child_process';
import { ROOT, read } from './_util.mjs';
import { FakeDevice } from './_fakedevice.mjs';
import {
  buildPutLines, putSteps, parseReply, parseLs, isAllowedCommand, checkFiles, folderOk,
  installOverSerial, LineReader, wrapWebSerial, usbSupported, crc32hex, CHUNK, READ_LINE_CAP,
} from '../js/usbinstall.js';

const enc = (s) => new TextEncoder().encode(s);
const FAST = { settle: 40, boot: 600, probe: 300, put: 400, chunk: 150, end: 400, abort: 150, total: 30000 };
const bytes = (n, seed) => Uint8Array.from({ length: n }, (_, i) => (i * 37 + seed) & 255);
const PACK = [
  { path: 'my-pack/theme.json', data: bytes(700, 3) },
  { path: 'my-pack/emblem.bin', data: bytes(1000, 9) },
];
const cosmic = () => ['emblem.bin', 'theme.json'].map((f) => ({
  path: 'cosmic-clone/' + f, data: new Uint8Array(fs.readFileSync(path.join(ROOT, 'sim/packs/cosmic-clone', f))),
}));
const sentCommands = (dev) => dev.received.filter((l) => l !== '');

// A port that records what was written and never answers.
function deadPort() {
  const p = { writes: [], reads: 0, write: async (b) => { p.writes.push(b); }, read: () => { p.reads++; return new Promise(() => {}); } };
  return p;
}

// ------------------------------------------------------------- wire format
test('CRC-32 vectors (zlib / IEEE): 123456789 -> cbf43926, fox -> 414fa339, empty -> 00000000', () => {
  assert.equal(crc32hex(enc('123456789')), 'cbf43926');
  assert.equal(crc32hex(enc('The quick brown fox jumps over the lazy dog')), '414fa339');
  assert.equal(crc32hex(new Uint8Array(0)), '00000000');
});

test('buildPutLines: SDPUT with size and crc, lower-case hex chunks of <= 96 bytes, END', () => {
  const lines = buildPutLines('/themes/x/theme.json', Uint8Array.from({ length: 200 }, (_, i) => i));
  assert.equal(lines.length, 5);
  assert.match(lines[0], /^SDPUT \/themes\/x\/theme\.json 200 [0-9a-f]{8}$/);
  assert.deepEqual(lines.slice(1, 4).map((l) => l.split(' ')[1]), ['0', '96', '192']);
  assert.equal(lines[3], 'D 192 c0c1c2c3c4c5c6c7');
  assert.equal(lines[4], 'END');
  for (const l of buildPutLines('/themes/x/emblem.bin', new Uint8Array(9728).fill(0xab))) assert.ok(l.length <= 200, l.length);
  assert.equal(CHUNK, 96);
  assert.deepEqual(putSteps('/themes/x/theme.json', enc('abc')).map((s) => s.want),
    ['OK put /themes/x/theme.json 3', 'OK 0', 'DONE 3 352441c2']);
});

test('the transcript equals sim/sdput_golden.txt byte for byte (the file the C++ test replays)', () => {
  const golden = read('sim/sdput_golden.txt');
  const chk = checkFiles(cosmic());
  assert.ok(chk.ok, chk.error);
  const out = ["# tools/sdput.py transcript of the pack folder 'cosmic-clone'",
    "# '> ' is sent, '< ' is the reply the tool requires. Regenerate with:",
    '#   python3 -I ../tools/sdput.py transcript packs/cosmic-clone > sdput_golden.txt'];
  for (const f of chk.ordered) for (const s of putSteps(f.devicePath, f.data)) out.push('> ' + s.line, '< ' + s.want);
  assert.equal(out.join('\n') + '\n', golden);
  assert.deepEqual(chk.ordered.map((f) => f.devicePath), ['/themes/cosmic-clone/emblem.bin', '/themes/cosmic-clone/theme.json'], 'emblem first, like the tool');
});

test('tools/sdput.py, run now, still prints that same golden transcript', () => {
  const r = spawnSync('python3', ['-I', path.join(ROOT, 'tools/sdput.py'), 'transcript', path.join(ROOT, 'sim/packs/cosmic-clone')], { encoding: 'utf8' });
  assert.equal(r.status, 0, r.stderr);
  assert.equal(r.stdout, read('sim/sdput_golden.txt'));
});

// ------------------------------------------------------------- reply grammar
test('parseReply accepts exactly the five reply shapes', () => {
  assert.deepEqual(parseReply('OK put /themes/my-pack/theme.json 758'), { kind: 'put', path: '/themes/my-pack/theme.json', size: 758 });
  assert.deepEqual(parseReply('OK 0'), { kind: 'ok', offset: 0 });
  assert.deepEqual(parseReply('OK 9696'), { kind: 'ok', offset: 9696 });
  assert.deepEqual(parseReply('DONE 745 c536ca9d'), { kind: 'done', size: 745, crc: 'c536ca9d' });
  assert.deepEqual(parseReply('OK aborted'), { kind: 'aborted' });
  assert.deepEqual(parseReply('ERR crc mismatch on the received bytes'), { kind: 'err', reason: 'crc mismatch on the received bytes' });
});

test('parseReply: anything else is not a reply (null), however close', () => {
  for (const l of ['', 'OK', 'OK ', 'OK 01', 'OK -1', 'OK 1 ', ' OK 1', 'OK  1', 'OK 12345678901', 'ok 1',
    'DONE 745 C536CA9D', 'DONE 745 c536ca9', 'DONE 0745 c536ca9d', 'DONE 745 c536ca9d x',
    'OK put /themes/../x/theme.json 5', 'OK put /themes/a/preview.png 5', 'OK put /diag.log 5', 'OK put /themes/x/theme.json',
    'ERR', 'ERR ', 'ERR \u0007bell', 'OK rebooting', 'OK aborted ', 'OK\taborted',
    'f30    [scan ] draw 12 blit  3 ms | wifi', '=== SDLS /themes ===', 'unknown command \'x\'',
    'ERR ' + 'x'.repeat(300), 'OK ' + '1'.repeat(300)]) {
    assert.equal(parseReply(l), null, JSON.stringify(l));
  }
});

// ------------------------------------------------------------- SDLS
const BENCH = ['=== SDLS /themes ===', '   <dir>  my-umbravox/', '    9246  emblem.bin', '   12999  diag.log', '=== SDLS END ==='];
test('parseLs reads the lines the real device printed on the bench', () => {
  const r = parseLs(BENCH);
  assert.equal(r.ok, true);
  assert.equal(r.dir, '/themes');
  assert.deepEqual(r.dirs, ['my-umbravox']);
  assert.deepEqual(r.files, [{ name: 'emblem.bin', size: 9246 }, { name: 'diag.log', size: 12999 }]);
});

test('parseLs ignores status lines, banner text and CR, and lines outside the listing', () => {
  const r = parseLs(['UMBRAVOX bring-up', '   <dir>  outside/', '=== SDLS /themes ===\r',
    'f30    [scan ] draw 12 blit  3 ms | wifi   41/512', '   <dir>  a/', '   <dir>  B_2/\r', '=== SDLS END ===', '   <dir>  after/']);
  assert.deepEqual(r.dirs, ['a', 'B_2']);
  assert.equal(r.ok, true);
});

test('parseLs: no /themes is an empty listing; no card, a busy device or a cut listing is an error', () => {
  const none = parseLs(['=== SDLS /themes ===', 'sdlog: cannot open /themes', '=== SDLS END ===']);
  assert.equal(none.ok, true); assert.equal(none.missing, true); assert.deepEqual(none.dirs, []);
  const nocard = parseLs(['=== SDLS /themes ===', 'sdlog: no card', '=== SDLS END ===']);
  assert.equal(nocard.ok, false); assert.match(nocard.error, /no SD card/);
  assert.equal(parseLs(['=== SDLS /themes ===', '   <dir>  a/']).ok, false, 'no END line');
  assert.equal(parseLs(['ERR upload in progress (ABORT to cancel)']).ok, false);
  assert.equal(parseLs([]).ok, false);
});

// ------------------------------------------------------------- refusals
test('folderOk: the device rule (1-31 of A-Z a-z 0-9 _ -, no DOS device names)', () => {
  for (const f of ['midnight-ember', 'My_Pack-2', 'a'.repeat(31), 'com10', 'console']) assert.ok(folderOk(f), f);
  for (const f of ['', '..', 'a.b', 'a/b', 'a b', 'a'.repeat(32), 'CON', 'nul', 'Com1', 'lpt9', 'é']) assert.ok(!folderOk(f), f);
});

const refusals = [
  ['theme.json over 4096 bytes', [{ path: 'x/theme.json', data: new Uint8Array(4097) }], /4096/],
  ['emblem.bin over 9728 bytes', [{ path: 'x/theme.json', data: enc('{}') }, { path: 'x/emblem.bin', data: new Uint8Array(9729) }], /9728/],
  ['an empty file', [{ path: 'x/theme.json', data: new Uint8Array(0) }], /empty/],
  ['a dotted folder', [{ path: 'a.b/theme.json', data: enc('{}') }], /folder/],
  ['a DOS device folder', [{ path: 'CON/theme.json', data: enc('{}') }], /folder/],
  ['a 32-character folder', [{ path: 'a'.repeat(32) + '/theme.json', data: enc('{}') }], /folder/],
  ['a .. folder', [{ path: '../theme.json', data: enc('{}') }], /folder/],
  ['an extra file', [{ path: 'x/theme.json', data: enc('{}') }, { path: 'x/preview.png', data: enc('p') }], /theme\.json/],
  ['no theme.json', [{ path: 'x/emblem.bin', data: enc('UVE') }], /theme\.json/],
  ['two theme.json', [{ path: 'x/theme.json', data: enc('{}') }, { path: 'x/theme.json', data: enc('{}') }], /theme\.json/],
  ['two folders', [{ path: 'x/theme.json', data: enc('{}') }, { path: 'y/emblem.bin', data: enc('UVE') }], /one folder/],
  ['a nested path', [{ path: 'x/sub/theme.json', data: enc('{}') }], /theme\.json/],
  ['no files', [], /theme\.json/],
  ...['__proto__', 'constructor', 'toString', 'hasOwnProperty'].map((k) =>
    ['a prototype key as a file name (' + k + ')', [{ path: 'x/theme.json', data: enc('{}') }, { path: 'x/' + k, data: enc('p') }], new RegExp(k + ' is not one of them')]),
];
for (const [what, files, re] of refusals) {
  test('refused before anything is sent: ' + what, async () => {
    const port = deadPort();
    const r = await installOverSerial(port, files, { timeouts: FAST, decide: async () => 'overwrite' });
    assert.equal(r.ok, false);
    assert.equal(r.refused, true);
    assert.match(r.error, re);
    assert.equal(r.folderState, 'untouched');
    assert.deepEqual(r.wrote, []);
    assert.equal(port.writes.length, 0, 'nothing written to the port');
    assert.equal(port.reads, 0, 'the port was not even read');
  });
}

test('isAllowedCommand: only SDLS /themes, SDPUT, D, END and ABORT may ever be sent', () => {
  for (const l of ['', 'SDLS /themes', 'SDPUT /themes/x/theme.json 3 352441c2', 'D 0 616263', 'END', 'ABORT']) assert.ok(isAllowedCommand(l), l);
  for (const l of ['SDDEL /themes/x/theme.json', 'SDRMDIR x', 'REBOOT', 'SDCAT /diag.log', 'SDLS', 'SDLS /', 'SDLS /themes/x',
    'SDPUT /diag.log 3 352441c2', 'SDPUT /themes/x/theme.json 3 352441c2 x', 'D 0 6162630', 'D 0 ' + 'ab'.repeat(97), 'END x',
    'ABORT\nREBOOT', 'TEST', 'NETRESET', 'ENROLL', 'end']) assert.ok(!isAllowedCommand(l), l);
});

test('the module source never contains a destructive command as a string to send', () => {
  const src = read('web/designer/js/usbinstall.js').split('\n').filter((l) => !/^\s*\/\//.test(l)).join('\n');
  for (const w of ["'SDDEL", "'SDRMDIR", "'REBOOT", '"SDDEL', '"REBOOT', '`SDDEL', '`REBOOT']) assert.ok(!src.includes(w), w);
});

// ------------------------------------------------------------- the fake device
test('installs over a noisy link: banner, status lines, 1-7 byte reads, CRLF', async () => {
  let s = 7;
  const dev = new FakeDevice({ banner: true, statusEvery: 3, split: () => 1 + ((s = (s * 1103515245 + 12345) >>> 0) % 7) });
  const progress = [];
  const r = await installOverSerial(dev.port(), PACK, { timeouts: FAST, onProgress: (p) => progress.push(p) });
  assert.equal(r.ok, true, r.error);
  assert.equal(r.folderState, 'complete');
  assert.deepEqual(r.wrote, ['/themes/my-pack/emblem.bin', '/themes/my-pack/theme.json']);
  assert.deepEqual([...dev.get('/themes/my-pack/theme.json')], [...PACK[0].data]);
  assert.deepEqual([...dev.get('/themes/my-pack/emblem.bin')], [...PACK[1].data]);
  const cmds = sentCommands(dev);
  assert.equal(cmds[0], 'SDLS /themes');
  assert.match(cmds[1], /^SDPUT \/themes\/my-pack\/emblem\.bin 1000 /);
  for (const l of dev.received) assert.ok(isAllowedCommand(l), l);
  const last = progress[progress.length - 1];
  assert.equal(last.sent, 1700); assert.equal(last.total, 1700);
  assert.ok(progress.some((p) => p.phase === 'upload' && p.sent > 0 && p.sent < 1700));
});

test('the golden pack through the fake device sends exactly the golden lines', async () => {
  const dev = new FakeDevice();
  const r = await installOverSerial(dev.port(), cosmic(), { timeouts: FAST });
  assert.equal(r.ok, true, r.error);
  const golden = read('sim/sdput_golden.txt').split('\n').filter((l) => l.startsWith('> ')).map((l) => l.slice(2));
  assert.deepEqual(sentCommands(dev).slice(1), golden);
});

test('a lost OK: the chunk is resent, the device takes it as a duplicate, the install completes', async () => {
  const dev = new FakeDevice({ dropOk: [2] });
  const r = await installOverSerial(dev.port(), PACK, { timeouts: FAST });
  assert.equal(r.ok, true, r.error);
  const d96 = sentCommands(dev).filter((l) => l.startsWith('D 96 '));
  assert.equal(d96.length, 3, 'D 96 of the emblem twice, of theme.json once');
  assert.deepEqual([...dev.get('/themes/my-pack/emblem.bin')], [...PACK[1].data]);
});

test('a corrupted chunk: the device answers ERR crc at END; nothing claimed, the card keeps its files', async () => {
  const dev = new FakeDevice({ corrupt: (target, off, d) => (target.endsWith('emblem.bin') && off === 96 ? d.map((b, i) => (i ? b : b ^ 1)) : d) });
  const r = await installOverSerial(dev.port(), PACK, { timeouts: FAST });
  assert.equal(r.ok, false);
  assert.match(r.error, /crc mismatch/);
  assert.deepEqual(r.wrote, []);
  assert.equal(r.folderState, 'untouched');
  assert.match(r.message, /Nothing on the card was changed/);
  assert.equal(dev.get('/themes/my-pack/emblem.bin'), undefined);
  assert.ok(!sentCommands(dev).some((l) => l.startsWith('SDPUT /themes/my-pack/theme.json')), 'theme.json not attempted after a failure');
});

test('theme.json fails after the emblem landed: reported as mixed, never as success', async () => {
  const dev = new FakeDevice({ corrupt: (target, off, d) => (target.endsWith('theme.json') && off === 0 ? d.map((b, i) => (i ? b : b ^ 1)) : d) });
  const r = await installOverSerial(dev.port(), PACK, { timeouts: FAST });
  assert.equal(r.ok, false);
  assert.equal(r.folderState, 'mixed');
  assert.deepEqual(r.wrote, ['/themes/my-pack/emblem.bin']);
  assert.match(r.message, /emblem\.bin/);
  assert.match(r.message, /theme\.json/);
});

test('a device that goes silent: bounded retries, ABORT, failure within the timeouts', async () => {
  const dev = new FakeDevice({ silentAfter: 4 });   // the listing, SDPUT and two chunks answered
  const t0 = Date.now();
  const r = await installOverSerial(dev.port(), PACK, { timeouts: FAST });
  assert.equal(r.ok, false);
  assert.match(r.error, /no reply/);
  assert.ok(Date.now() - t0 < 5000, 'gave up in bounded time');
  const cmds = sentCommands(dev);
  assert.equal(cmds[cmds.length - 1], 'ABORT');
  assert.equal(cmds.filter((l) => l.startsWith('D 192 ')).length, 4, 'sent once, then retried 3 times');
  assert.equal(r.folderState, 'untouched');
});

test('no reply to END: the device may hold either file, so the state is unknown, not success', async () => {
  const dev = new FakeDevice();
  const p = dev.port();
  const real = dev.finish.bind(dev);
  dev.finish = () => { real(); dev.out.length = 0; };   // the DONE is lost
  const r = await installOverSerial(p, PACK, { timeouts: FAST });
  assert.equal(r.ok, false);
  assert.equal(r.folderState, 'unknown');
  assert.deepEqual(r.wrote, []);
});

test('a DONE that names other bytes than the page computed is not success', async () => {
  const dev = new FakeDevice();
  const reply = dev.reply.bind(dev);
  dev.reply = (l) => reply(l.startsWith('DONE ') ? l.replace(/[0-9a-f]{8}$/, '00000000') : l);
  const r = await installOverSerial(dev.port(), PACK, { timeouts: FAST });
  assert.equal(r.ok, false);
  assert.match(r.error, /different bytes/);
  assert.notEqual(r.folderState, 'complete');
  assert.deepEqual(r.wrote, []);
});

test('cancel mid-upload: ABORT is the last line sent, result is cancelled, nothing claimed', async () => {
  const dev = new FakeDevice();
  const ac = new AbortController();
  const r = await installOverSerial(dev.port(), PACK, {
    timeouts: FAST, signal: ac.signal, onProgress: (p) => { if (p.sent >= 480) ac.abort(); },
  });
  assert.equal(r.ok, false);
  assert.equal(r.cancelled, true);
  const cmds = sentCommands(dev);
  assert.equal(cmds[cmds.length - 1], 'ABORT');
  assert.equal(dev.up, null, 'the device has no upload running');
  assert.equal(r.folderState, 'untouched');
  assert.equal(dev.dirs.has('/themes/my-pack'), false, 'the new folder went with the ABORT');
});

test('cancelled before it starts: nothing at all is sent', async () => {
  const ac = new AbortController(); ac.abort();
  const dev = new FakeDevice();
  const r = await installOverSerial(dev.port(), PACK, { timeouts: FAST, signal: ac.signal });
  assert.equal(r.ok, false); assert.equal(r.cancelled, true); assert.equal(r.folderState, 'untouched');
  assert.deepEqual(sentCommands(dev), []);
});

test('cancelled while the collision question is open: only the listing was sent, no ABORT needed', async () => {
  const ac = new AbortController();
  const dev = new FakeDevice({ dirs: ['/themes/my-pack'] });
  const r = await installOverSerial(dev.port(), PACK, { timeouts: FAST, signal: ac.signal, decide: async () => { ac.abort(); return 'overwrite'; } });
  assert.equal(r.ok, false); assert.equal(r.cancelled, true);
  assert.deepEqual(sentCommands(dev), ['SDLS /themes']);
});

test('cancelled while a collision question is never answered: returns cancelled, does not hang', async () => {
  const ac = new AbortController();
  const dev = new FakeDevice({ dirs: ['/themes/my-pack'] });
  const decide = () => { setTimeout(() => ac.abort(), 50); return new Promise(() => {}); };   // the question stays open
  const r = await installOverSerial(dev.port(), PACK, { timeouts: FAST, signal: ac.signal, decide });
  assert.equal(r.cancelled, true);
  assert.deepEqual(sentCommands(dev), ['SDLS /themes']);
});

test('a board still booting swallows the first probe: the listing is retried', async () => {
  const dev = new FakeDevice({ banner: true, bootSwallow: 1 });
  const r = await installOverSerial(dev.port(), PACK, { timeouts: FAST });
  assert.equal(r.ok, true, r.error);
  assert.equal(sentCommands(dev).filter((l) => l === 'SDLS /themes').length, 2);
});

test('a device that never answers the listing: failure, nothing uploaded', async () => {
  const port = deadPort();
  const r = await installOverSerial(port, PACK, { timeouts: FAST });
  assert.equal(r.ok, false);
  assert.match(r.error, /did not answer/);
  const sent = new TextDecoder().decode(Uint8Array.from(port.writes.flatMap((w) => [...w])));
  assert.ok(!sent.includes('SDPUT'));
});

test('a device busy with another upload: refused with a reason, nothing sent after the listing', async () => {
  const dev = new FakeDevice();
  dev.up = { target: '/themes/z/theme.json', size: 10, crc: 0, data: [], dir: '/themes/z' };
  const r = await installOverSerial(dev.port(), PACK, { timeouts: FAST });
  assert.equal(r.ok, false);
  assert.match(r.error, /another upload/);
  assert.deepEqual(sentCommands(dev).filter((l) => l !== 'SDLS /themes'), []);
});

// ------------------------------------------------------------- stale and late lines (review fixes)
test('stale console output queued before the install is drained, not taken as a reply', async () => {
  const dev = new FakeDevice();
  dev.say('ERR upload in progress (ABORT to cancel)');   // left over from before this install
  dev.say('ERR upload timed out');
  const r = await installOverSerial(dev.port(), PACK, { timeouts: { ...FAST, settle: 0 } });
  assert.equal(r.ok, true, r.error);
});

test('a boot banner seen while draining still makes the install wait for the main loop', async () => {
  const dev = new FakeDevice({ banner: true });
  const r = await installOverSerial(dev.port(), PACK, { timeouts: { ...FAST, settle: 0 } });
  assert.equal(r.ok, true, r.error);
});

test('an ERR that arrives in place of OK put: the install fails, but ABORT is still sent', async () => {
  const dev = new FakeDevice();
  const put = dev.put.bind(dev);
  dev.put = (t) => { dev.say('ERR upload timed out'); put(t); };   // a stale line, then the real OK put
  const r = await installOverSerial(dev.port(), PACK, { timeouts: FAST });
  assert.equal(r.ok, false);
  assert.match(r.error, /upload timed out/);
  const cmds = sentCommands(dev);
  assert.equal(cmds[cmds.length - 1], 'ABORT');
  assert.equal(dev.up, null, 'no upload left open on the device');
  assert.equal(r.folderState, 'untouched');
});

test('a stale ERR just before the real DONE: the DONE (size and CRC match) is honoured', async () => {
  const dev = new FakeDevice();
  const fin = dev.finish.bind(dev);
  dev.finish = () => { dev.say('ERR upload timed out'); fin(); };
  const r = await installOverSerial(dev.port(), PACK, { timeouts: FAST });
  assert.equal(r.ok, true, r.error);
  assert.equal(r.folderState, 'complete');
  assert.deepEqual(r.wrote, ['/themes/my-pack/emblem.bin', '/themes/my-pack/theme.json']);
  assert.deepEqual([...dev.get('/themes/my-pack/theme.json')], [...PACK[0].data]);
});

test('an ERR then a DONE naming other bytes: unknown, never success', async () => {
  const dev = new FakeDevice();
  const fin = dev.finish.bind(dev);
  dev.finish = () => { dev.say('ERR upload timed out'); fin(); };
  const reply = dev.reply.bind(dev);
  dev.reply = (l) => reply(l.startsWith('DONE ') ? l.replace(/[0-9a-f]{8}$/, '00000000') : l);
  const r = await installOverSerial(dev.port(), PACK, { timeouts: FAST });
  assert.equal(r.ok, false);
  assert.equal(r.folderState, 'unknown');
  assert.deepEqual(r.wrote, []);
});

test('an ERR at END and no DONE within the grace time: the card keeps the old file (untouched)', async () => {
  const dev = new FakeDevice();
  dev.finish = () => { dev.drop(); dev.reply('ERR crc mismatch on the received bytes'); };
  const t0 = Date.now();
  const r = await installOverSerial(dev.port(), PACK, { timeouts: FAST });
  assert.equal(r.ok, false);
  assert.equal(r.folderState, 'untouched');
  assert.match(r.error, /crc mismatch/);
  assert.ok(Date.now() - t0 >= 400, 'waited for a late DONE');
});

// ------------------------------------------------------------- device ERR texts on chunks
test('a non-fatal chunk ERR is retried, then the device text is in the failure', async () => {
  const dev = new FakeDevice();
  const chunk = dev.chunk.bind(dev);
  dev.chunk = (t) => (t[1] === '96' ? dev.reply('ERR bad hex') : chunk(t));
  const r = await installOverSerial(dev.port(), PACK, { timeouts: FAST });
  assert.equal(r.ok, false);
  assert.match(r.error, /ERR bad hex/);
  assert.doesNotMatch(r.error, /no reply/);
  assert.equal(sentCommands(dev).filter((l) => l.startsWith('D 96 ')).length, 1 + 3);
  assert.equal(sentCommands(dev).at(-1), 'ABORT');
});

test('a fatal chunk ERR (the device abandoned the upload) is not retried', async () => {
  const dev = new FakeDevice();
  const chunk = dev.chunk.bind(dev);
  dev.chunk = (t) => (t[1] === '96' ? (dev.drop(), dev.reply('ERR write failed (card full or removed?)')) : chunk(t));
  const r = await installOverSerial(dev.port(), PACK, { timeouts: FAST });
  assert.equal(r.ok, false);
  assert.match(r.error, /write failed \(card full or removed\?\)/);
  assert.equal(sentCommands(dev).filter((l) => l.startsWith('D 96 ')).length, 1);
});

test('line noise on one chunk (ERR non-printable byte in line) is retried and the install completes', async () => {
  const dev = new FakeDevice();
  const chunk = dev.chunk.bind(dev);
  let once = true;
  dev.chunk = (t) => (t[1] === '96' && once ? (once = false, dev.reply('ERR non-printable byte in line')) : chunk(t));
  const r = await installOverSerial(dev.port(), PACK, { timeouts: FAST });
  assert.equal(r.ok, true, r.error);
});

// ------------------------------------------------------------- bounded buffering
test('a hostile endless line without a newline is bounded and times out', async () => {
  const flood = new Uint8Array(4096).fill(0x41);
  let reads = 0;
  const port = { write: async () => {}, read: async () => { reads++; return flood; } };
  const reader = new LineReader(port);
  const line = await reader.next(Date.now() + 300);
  assert.equal(line, null);
  assert.ok(reads > 10);
  assert.ok(reader.buffered <= READ_LINE_CAP, 'buffered ' + reader.buffered);
  const t0 = Date.now();
  const r = await installOverSerial(port, PACK, { timeouts: FAST });
  assert.equal(r.ok, false);
  assert.ok(Date.now() - t0 < 5000);
});

test('LineReader: CRLF, split reads, overlong and non-printable lines dropped whole', async () => {
  const chunks = [enc('UMB'), enc('RAVOX\r\nOK '), enc('96\r'), enc('\n'), Uint8Array.from([0xff, 0x4f, 0x4b, 0x20, 0x31, 10]),
    enc('x'.repeat(READ_LINE_CAP + 1) + '\nOK 0\n')];
  const port = { write: async () => {}, read: async () => chunks.shift() || new Promise(() => {}) };
  const rd = new LineReader(port);
  const got = [];
  for (let l; (l = await rd.next(Date.now() + 200)) !== null;) got.push(l);
  assert.deepEqual(got, ['UMBRAVOX', 'OK 96', 'OK 0']);
});

test('LineReader: a closed port is an error, not a hang', async () => {
  const rd = new LineReader({ write: async () => {}, read: async () => null });
  await assert.rejects(rd.next(Date.now() + 1000), /closed/);
});

// ------------------------------------------------------------- collisions through plan.js
test('an existing folder (any case) goes through planInstall; overwrite / other folder / cancel', async () => {
  const seen = [];
  const run = async (choice, dirs = ['/themes/My-Pack']) => {
    const dev = new FakeDevice({ dirs });
    const r = await installOverSerial(dev.port(), PACK, { timeouts: FAST, themeName: 'MY PACK', decide: async (plan) => { seen.push(plan); return choice; } });
    return { dev, r };
  };
  const a = await run('alternative');
  assert.equal(seen[0].collision.folder, 'My-Pack');
  assert.equal(seen[0].collision.name, null, 'over USB the theme name in the folder is not read');
  assert.equal(seen[0].alternative, 'my-pack-2');
  assert.equal(a.r.ok, true); assert.equal(a.r.folder, 'my-pack-2');
  assert.deepEqual([...a.dev.get('/themes/my-pack-2/theme.json')], [...PACK[0].data]);
  assert.equal(a.dev.get('/themes/my-pack/theme.json'), undefined);

  const o = await run('overwrite');
  assert.equal(o.r.ok, true);
  assert.ok(o.dev.get('/themes/my-pack/theme.json'));

  const c = await run('cancel');
  assert.equal(c.r.ok, false); assert.equal(c.r.cancelled, true); assert.equal(c.r.folderState, 'untouched');
  assert.deepEqual(sentCommands(c.dev), ['SDLS /themes'], 'only the listing was sent');

  await run('alternative', ['/themes/my-pack', '/themes/MY-PACK-2']);
  assert.equal(seen[seen.length - 1].alternative, 'my-pack-3');
});

test('a collision with no decide callback is refused: never overwrite silently', async () => {
  const dev = new FakeDevice({ dirs: ['/themes/my-pack'] });
  const r = await installOverSerial(dev.port(), PACK, { timeouts: FAST });
  assert.equal(r.ok, false);
  assert.match(r.error, /already on the card/);
  assert.deepEqual(sentCommands(dev), ['SDLS /themes']);
});

test('no collision: decide is not asked', async () => {
  const dev = new FakeDevice({ dirs: ['/themes/other'] });
  let asked = 0;
  const r = await installOverSerial(dev.port(), PACK, { timeouts: FAST, decide: async () => { asked++; return 'cancel'; } });
  assert.equal(r.ok, true); assert.equal(asked, 0);
});

// ------------------------------------------------------------- Web Serial adapter
function fakeSerialPort(chunks) {
  const written = [];
  let ctl;
  const port = {
    readable: new ReadableStream({ start(c) { ctl = c; for (const x of chunks) c.enqueue(x); } }),
    writable: new WritableStream({ write(x) { written.push(...x); } }),
  };
  return { port, written, push: (x) => ctl.enqueue(x), end: () => ctl.close() };
}

test('wrapWebSerial: reads stream chunks, writes bytes, keeps at most maxQueued bytes', async () => {
  const f = fakeSerialPort([enc('OK 0\n')]);
  const w = wrapWebSerial(f.port, { maxQueued: 64 });
  await w.write(enc('END\n'));
  assert.equal(new TextDecoder().decode(Uint8Array.from(f.written)), 'END\n');
  assert.equal(new TextDecoder().decode(await w.read()), 'OK 0\n');
  for (let i = 0; i < 50; i++) f.push(enc('0123456789'));
  await new Promise((r) => setTimeout(r, 20));
  assert.ok(w.queued <= 64, 'queued ' + w.queued);
  f.end();
  let total = 0;
  for (let c; (c = await w.read()) !== null;) total += c.length;
  assert.ok(total <= 64);
  await w.close();
});

test('usbSupported follows navigator.serial', () => {
  assert.equal(usbSupported({ navigator: { serial: {} } }), true);
  assert.equal(usbSupported({ navigator: {} }), false);
  assert.equal(usbSupported({}), false);
});

// ------------------------------------------------------------- the REAL C++ state machine
const CONSOLE = path.join(ROOT, 'sim/sdcmd_console');
function ensureConsole() {
  const r = spawnSync('make', ['-C', path.join(ROOT, 'sim'), 'sdcmd_console'], { encoding: 'utf8' });
  assert.equal(r.status, 0, r.stdout + r.stderr);
}
// The child's stdin/stdout as the port.
function pipePort(child) {
  const q = [];
  let wait = null, ended = false;
  child.stdout.on('data', (b) => { q.push(new Uint8Array(b)); if (wait) { wait(); wait = null; } });
  child.stdout.on('end', () => { ended = true; if (wait) { wait(); wait = null; } });
  return {
    write: (b) => new Promise((res, rej) => child.stdin.write(Buffer.from(b), (e) => (e ? rej(e) : res()))),
    read: async () => {
      while (!q.length && !ended) await new Promise((r) => { wait = r; });
      return q.length ? q.shift() : null;
    },
  };
}
async function onRealConsole(args, files, opts) {
  ensureConsole();
  const dump = fs.mkdtempSync(path.join(os.tmpdir(), 'uvx-card-'));
  const child = spawn(CONSOLE, [...args, '--dump', dump], { stdio: ['pipe', 'pipe', 'inherit'] });
  const exited = new Promise((r) => child.on('exit', (code) => r(code)));
  const r = await installOverSerial(pipePort(child), files, { timeouts: { ...FAST, chunk: 400 }, ...opts });
  child.stdin.end();
  assert.equal(await exited, 0);
  const card = (p) => { const f = path.join(dump, p); return fs.existsSync(f) ? new Uint8Array(fs.readFileSync(f)) : null; };
  const dirs = fs.readFileSync(path.join(dump, '.dirs'), 'utf8').split('\n').filter(Boolean);
  const received = fs.readFileSync(path.join(dump, '.received'), 'utf8').split('\n').filter(Boolean);
  return { r, card, dirs, received };
}

test('REAL device/sdcmd.h over stdin/stdout: the golden pack lands byte for byte', async () => {
  const files = cosmic();
  const { r, card, received } = await onRealConsole(['--banner', '--status-ms', '20'], files);
  assert.equal(r.ok, true, r.error);
  assert.equal(r.folderState, 'complete');
  assert.deepEqual(card('themes/cosmic-clone/emblem.bin'), files[0].data);
  assert.deepEqual(card('themes/cosmic-clone/theme.json'), files[1].data);
  for (const l of received) assert.ok(isAllowedCommand(l), l);
});

test('REAL: an existing folder asks, and "Install as" lands in cosmic-clone-2', async () => {
  const files = cosmic();
  let plan = null;
  const { r, card } = await onRealConsole(['--mkdir', '/themes/cosmic-clone'], files, { decide: async (p) => { plan = p; return 'alternative'; } });
  assert.equal(plan.collision.folder, 'cosmic-clone');
  assert.equal(r.ok, true, r.error);
  assert.deepEqual(card('themes/cosmic-clone-2/theme.json'), files[1].data);
  assert.equal(card('themes/cosmic-clone/theme.json'), null);
});

test('REAL: a lost OK is retried and the real duplicate check accepts it', async () => {
  const files = cosmic();
  const { r, card, received } = await onRealConsole(['--drop-ok', '3', '--status-ms', '15'], files);
  assert.equal(r.ok, true, r.error);
  assert.deepEqual(card('themes/cosmic-clone/emblem.bin'), files[0].data);
  // the emblem's chunk at 192 twice (its OK was lost), theme.json's once
  assert.equal(received.filter((l) => l.startsWith('D 192 ')).length, 3);
  assert.equal(received.filter((l) => l === 'D 192 ' + Buffer.from(files[0].data.subarray(192, 288)).toString('hex')).length, 2);
});

test('REAL: cancel mid-upload sends ABORT; no temp, no new folder is left', async () => {
  const files = cosmic();
  const ac = new AbortController();
  const { r, card, dirs, received } = await onRealConsole([], files, { signal: ac.signal, onProgress: (p) => { if (p.sent > 1000) ac.abort(); } });
  assert.equal(r.cancelled, true);
  assert.equal(received[received.length - 1], 'ABORT');
  assert.equal(card('themes/cosmic-clone/emblem.bin.tmp'), null);
  assert.ok(!dirs.includes('/themes/cosmic-clone'), dirs.join(','));
});

test('REAL: replacing an existing pack (overwrite) swaps both files', async () => {
  const files = cosmic();
  const { r, card } = await onRealConsole(['--put', '/themes/cosmic-clone/theme.json=' + path.join(ROOT, 'sim/packs/umbravox-clone/theme.json')], files,
    { decide: async () => 'overwrite' });
  assert.equal(r.ok, true, r.error);
  assert.deepEqual(card('themes/cosmic-clone/theme.json'), files[1].data);
});
