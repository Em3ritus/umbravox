// Install a pack over the CYD's USB cable (Web Serial), speaking the SD-install
// protocol of device/sdcmd.h, as tools/sdput.py does. Pure: no DOM, no globals.
// The serial port is injected as {write(Uint8Array), read() -> Uint8Array|null},
// so the tests drive it with a fake device and with the real C++ state machine
// (sim/sdcmd_console). wrapWebSerial() turns a Web Serial SerialPort into one.
//
// SECURITY RULE (from the security review of the serial console): this page
// sends ONLY `SDLS /themes`, `SDPUT`, `D`, `END` and `ABORT`. The device's
// destructive commands (delete a file, remove a folder, restart the board) act
// without any confirmation on the device, and anything that can write to the
// port could use them, so they are never built, never offered and refused by
// isAllowedCommand() before a byte is written. Deleting and restarting stay
// with tools/sdput.py, run by the owner on their own computer.
//
// Everything that comes back from the device is untrusted text: replies are
// matched against a strict grammar, lines are bounded, buffering is bounded,
// and callers must put any of it on the page with textContent only.
import limits from './limits.json' with { type: 'json' };
import { crc32 } from './zip.js';
import { planInstall, retargetFiles } from './plan.js';

export const CHUNK = 96;              // raw bytes per D line (sdcmd::CHUNK_MAX)
export const DEVICE_LINE_CAP = 200;   // longest line the device takes (sdcmd::LINE_CAP)
export const READ_LINE_CAP = 512;     // longest device line kept; longer ones are dropped whole
export const RETRIES = 3;             // a chunk is resent up to this many times
export const MAX_LS_ENTRIES = 1024;   // a listing longer than this is refused
export const BAUD = 115200;
// The CYD's USB-serial chip (WCH CH340).
export const CH340 = { usbVendorId: 0x1a86, usbProductId: 0x7523 };

export const TIMEOUTS = {
  settle: 1500,   // listen this long first: a board reset by the port opening shows its banner
  boot: 12000,    // after a boot banner, wait at most this long for the firmware's main loop
  probe: 2500,    // per SDLS /themes attempt (three attempts)
  put: 5000,      // reply to SDPUT
  chunk: 2000,    // reply to each D line
  end: 8000,      // reply to END (the device re-reads the card)
  abort: 2000,    // reply to ABORT
  total: 110000,  // the whole upload; under the device's own 120 s cap
};

const enc = new TextEncoder();
const HEX = '0123456789abcdef';
const hex = (b) => { let s = ''; for (const x of b) s += HEX[x >> 4] + HEX[x & 15]; return s; };
export const crc32hex = (bytes) => crc32(bytes).toString(16).padStart(8, '0');

// --- the rules the device applies ---------------------------------------------
const FOLDER_RE = new RegExp(limits.folderName.pattern);
const DOS_RE = /^(con|prn|aux|nul|com[0-9]|lpt[0-9])$/i;
export const folderOk = (f) => typeof f === 'string' && FOLDER_RE.test(f) && !DOS_RE.test(f);
const CAPS = { 'theme.json': limits.MAX_JSON, 'emblem.bin': limits.MAX_EMBLEM };

const DEC = '(?:0|[1-9][0-9]{0,9})';
const PATH = '/themes/[A-Za-z0-9_-]{1,31}/(?:theme\\.json|emblem\\.bin)';
const ALLOWED = [
  /^$/,                                                     // a lone newline: ends any partial line
  /^SDLS \/themes$/,
  new RegExp('^SDPUT (' + PATH + ') [1-9][0-9]{0,9} [0-9a-f]{8}$'),
  new RegExp('^D ' + DEC + ' (?:[0-9a-f]{2}){1,' + CHUNK + '}$'),
  /^END$/,
  /^ABORT$/,
];
// The whole send-side vocabulary. Anything else is refused before it is written.
export function isAllowedCommand(line) {
  if (typeof line !== 'string' || line.length > DEVICE_LINE_CAP) return false;
  const i = ALLOWED.findIndex((re) => re.test(line));
  if (i < 0) return false;
  if (i === 2) return folderOk(line.split(' ')[1].split('/')[2]);
  return true;
}

// (line to send, the reply that confirms it) for one file, as sdput.py put_steps().
export function putSteps(path, bytes) {
  const crc = crc32hex(bytes);
  const steps = [{ line: 'SDPUT ' + path + ' ' + bytes.length + ' ' + crc, want: 'OK put ' + path + ' ' + bytes.length }];
  for (let off = 0; off < bytes.length; off += CHUNK) {
    steps.push({ line: 'D ' + off + ' ' + hex(bytes.subarray(off, off + CHUNK)), want: 'OK ' + off, offset: off, n: Math.min(CHUNK, bytes.length - off) });
  }
  steps.push({ line: 'END', want: 'DONE ' + bytes.length + ' ' + crc });
  return steps;
}
export const buildPutLines = (path, bytes) => putSteps(path, bytes).map((s) => s.line);

// One device line -> a reply object, or null when it is not a reply (a log
// line, the status line, banner text, anything malformed).
const R_PUT = new RegExp('^OK put (' + PATH + ') (' + DEC + ')$');
const R_OK = new RegExp('^OK (' + DEC + ')$');
const R_DONE = new RegExp('^DONE (' + DEC + ') ([0-9a-f]{8})$');
const R_ERR = /^ERR ([\x21-\x7e][\x20-\x7e]*)$/;
export function parseReply(line) {
  if (typeof line !== 'string' || line.length > DEVICE_LINE_CAP) return null;
  let m;
  if ((m = R_PUT.exec(line)) && folderOk(m[1].split('/')[2])) return { kind: 'put', path: m[1], size: Number(m[2]) };
  if ((m = R_OK.exec(line))) return { kind: 'ok', offset: Number(m[1]) };
  if ((m = R_DONE.exec(line))) return { kind: 'done', size: Number(m[1]), crc: m[2] };
  if (line === 'OK aborted') return { kind: 'aborted' };
  if ((m = R_ERR.exec(line))) return { kind: 'err', reason: m[1] };
  return null;
}

// SDLS output (device/sdlog.cpp list()) -> {ok, dir, missing, dirs, files, error}.
// Lines outside "=== SDLS <dir> ===" .. "=== SDLS END ===" and lines inside
// that match no entry shape (the status line) are ignored. Names are data.
const LS_HEAD = /^=== SDLS (\/[\x21-\x7e]*) ===$/;
const LS_DIR = /^ {3}<dir> {2}([\x20-\x7e]+)\/$/;
const LS_FILE = /^ {0,7}(0|[1-9][0-9]{0,9}) {2}([\x21-\x7e][\x20-\x7e]*)$/;
const BUSY = 'The device is busy with another upload (perhaps another program or tab). Wait 10 seconds and try again.';
export function parseLs(lines) {
  const out = { ok: false, dir: null, missing: false, dirs: [], files: [], error: null };
  let inside = false;
  for (const raw of lines) {
    const l = String(raw).replace(/\r$/, '');
    if (!inside) {
      const h = LS_HEAD.exec(l);
      if (h) { inside = true; out.dir = h[1]; continue; }
      if (l.startsWith('ERR upload in progress')) { out.error = BUSY; return out; }
      continue;
    }
    if (l === '=== SDLS END ===') { out.ok = out.error === null; return out; }
    let m;
    if ((m = LS_DIR.exec(l))) out.dirs.push(m[1]);
    else if ((m = LS_FILE.exec(l))) out.files.push({ name: m[2], size: Number(m[1]) });
    else if (l === 'sdlog: no card') out.error = 'The device reports no SD card. Put the card in the CYD and try again.';
    else if (l.startsWith('sdlog: cannot open ')) out.missing = true;
    if (out.dirs.length + out.files.length > MAX_LS_ENTRIES) { out.error = 'The card listing is too long.'; return out; }
  }
  if (!out.error) out.error = inside ? 'The card listing was cut off.' : 'No card listing came back.';
  return out;
}

// files: [{path: '<folder>/<file>', data: Uint8Array}] as buildPack returns.
// -> {ok, folder, ordered: [{path, devicePath, name, data}] emblem first} | {ok:false, error}
export function checkFiles(files) {
  const SHAPE = 'Over USB a pack is exactly theme.json, plus emblem.bin if it has a picture';
  if (!Array.isArray(files) || !files.length) return { ok: false, error: SHAPE + '; there is no theme.json here.' };
  const folders = new Set(files.map((f) => String(f && f.path).split('/')[0]));
  if (folders.size !== 1) return { ok: false, error: 'A pack is one folder; these files are in ' + folders.size + '.' };
  const folder = [...folders][0];
  if (!folderOk(folder)) return { ok: false, error: 'The folder name "' + folder + '" is not one the device accepts. ' + limits.folderName.text + ' DOS device names (CON, NUL, COM1...) are refused too.' };
  const seen = new Map();
  for (const f of files) {
    const parts = String(f.path).split('/');
    const name = parts[1];
    if (parts.length !== 2 || !(name in CAPS)) return { ok: false, error: SHAPE + '; ' + f.path + ' is not one of them.' };
    if (seen.has(name)) return { ok: false, error: SHAPE + '; ' + name + ' appears twice.' };
    if (!(f.data instanceof Uint8Array)) return { ok: false, error: name + ' has no bytes.' };
    if (f.data.length === 0) return { ok: false, error: name + ' is empty.' };
    if (f.data.length > CAPS[name]) return { ok: false, error: name + ' is ' + f.data.length + ' bytes; the device takes at most ' + CAPS[name] + '.' };
    seen.set(name, f);
  }
  if (!seen.has('theme.json')) return { ok: false, error: SHAPE + '; there is no theme.json here.' };
  const ordered = ['emblem.bin', 'theme.json'].filter((n) => seen.has(n)).map((n) => ({
    path: seen.get(n).path, devicePath: '/themes/' + folder + '/' + n, name: n, data: seen.get(n).data,
  }));
  return { ok: true, folder, ordered };
}

// --- reading lines off the port, bounded ---------------------------------------
const TIMEOUT = Symbol('timeout'), CANCELLED = Symbol('cancelled');
export class Cancelled extends Error { constructor() { super('cancelled'); this.name = 'Cancelled'; } }

// Bytes in, printable-ASCII lines out (LF or CR ends a line; blank lines are
// skipped). A line longer than maxLine or holding any other byte is dropped
// whole, never truncated, so at most maxLine characters are ever buffered
// however much the device (or anything else feeding the port) sends.
export class LineReader {
  constructor(port, { maxLine = READ_LINE_CAP, now = Date.now } = {}) {
    this.port = port; this.maxLine = maxLine; this.now = now;
    this.line = ''; this.over = false; this.bad = false;
    this.chunk = null; this.pos = 0; this.pending = null;
  }
  get buffered() { return this.line.length; }

  // The next line, or null once `deadline` (a now() time) passes.
  async next(deadline, signal) {
    for (;;) {
      if (signal && signal.aborted) throw new Cancelled();
      while (this.chunk && this.pos < this.chunk.length) {
        const b = this.chunk[this.pos++];
        if (b === 10 || b === 13) {
          const l = this.line, drop = this.over || this.bad;
          this.line = ''; this.over = this.bad = false;
          if (!drop && l) return l;
        } else if (b < 0x20 || b > 0x7e) this.bad = true;
        else if (this.line.length < this.maxLine) this.line += String.fromCharCode(b);
        else this.over = true;
      }
      this.chunk = null;
      if (this.now() >= deadline) return null;
      const c = await this.pull(deadline, signal);
      if (c === TIMEOUT) return null;
      this.chunk = c; this.pos = 0;
    }
  }

  async pull(deadline, signal) {
    // One read in flight at a time; one left pending by a timeout is kept for the next call.
    if (!this.pending) this.pending = Promise.resolve().then(() => this.port.read()).then((v) => ({ v }), (e) => ({ e }));
    let timer = null, onAbort = null;
    const t = new Promise((r) => { timer = setTimeout(() => r(TIMEOUT), Math.max(0, deadline - this.now())); });
    const a = new Promise((r) => { onAbort = () => r(CANCELLED); if (signal) signal.addEventListener('abort', onAbort, { once: true }); });
    try {
      const x = await Promise.race([this.pending, t, a]);
      if (x === TIMEOUT) return TIMEOUT;
      if (x === CANCELLED) throw new Cancelled();
      this.pending = null;
      if (x.e) throw x.e;
      if (x.v === null || x.v === undefined) throw new Error('the serial port closed');
      if (!(x.v instanceof Uint8Array)) throw new Error('the serial port returned something that is not bytes');
      return x.v;
    } finally {
      clearTimeout(timer);
      if (signal) signal.removeEventListener('abort', onAbort);
    }
  }
}
// One reader per port, so a read left pending by one install is not lost to the next.
const readers = new WeakMap();
const readerFor = (port) => { if (!readers.has(port)) readers.set(port, new LineReader(port)); return readers.get(port); };

// --- the conversation -------------------------------------------------------------
const BANNER = /UMBRAVOX bring-up|^rst:0x|^ets [A-Z][a-z]{2} /;
const STATUS = /^f[0-9]+ +\[/;               // the main loop's status line: the console is being polled
const FATAL = /^(no upload|upload timed out|write failed|no card|read-back failed|non-printable)/;

class Session {
  constructor(port, opts) {
    this.port = port;
    this.reader = readerFor(port);
    this.t = { ...TIMEOUTS, ...(opts.timeouts || {}) };
    this.signal = opts.signal || null;
    this.now = this.reader.now;
    this.totalEnd = Infinity;
  }
  async send(line) {
    if (!isAllowedCommand(line)) throw new Error('refusing to send a command this page may not send');
    await this.port.write(enc.encode(line + '\n'));
  }
  deadline(ms) { return Math.min(this.now() + ms, this.totalEnd); }
  // The reply `match` accepts, any ERR reply, or null on timeout. Every other line is ignored.
  async expect(match, ms, signal = this.signal) {
    const end = this.deadline(ms);
    for (;;) {
      const l = await this.reader.next(end, signal);
      if (l === null) {
        if (this.now() >= this.totalEnd) throw new Error('the upload took longer than ' + Math.round(this.t.total / 1000) + ' s');
        return null;
      }
      const r = parseReply(l);
      if (r && (r.kind === 'err' || match(r))) return r;
    }
  }
  // A board reset by the port opening prints a banner and boots for a few
  // seconds. Listen first; after a banner wait for the main loop's status line.
  async settle() {
    let end = this.now() + this.t.settle, booting = false;
    for (;;) {
      const l = await this.reader.next(end, this.signal);
      if (l === null) return booting ? 'boot-timeout' : 'quiet';
      if (BANNER.test(l)) { if (!booting) { booting = true; end = this.now() + this.t.boot; } }
      else if (booting && STATUS.test(l)) return 'booted';
    }
  }
  async listThemes() {
    let probes = 0;
    while (probes < 3) {
      await this.send('');              // a lone newline ends any partial line noise left
      await this.send('SDLS /themes');
      probes++;
      const end = this.now() + this.t.probe;
      const got = [];
      let head = false;
      for (;;) {
        const l = await this.reader.next(end, this.signal);
        if (l === null) break;
        if (!head) {
          if (l === '=== SDLS /themes ===') { head = true; got.push(l); continue; }
          if (l.startsWith('ERR upload in progress')) return { ok: false, error: BUSY };
          if (l.startsWith('unknown command (non-printable')) break;   // the probe was garbled: again
          continue;
        }
        got.push(l);
        if (got.length > MAX_LS_ENTRIES + 64) return { ok: false, error: 'The card listing is too long.' };
        if (l === '=== SDLS END ===') {
          // Earlier probes the device has still to answer would land in the
          // upload as "ERR upload in progress": let them come out first.
          for (let extra = probes - 1; extra > 0;) {
            const x = await this.reader.next(this.now() + this.t.probe, this.signal);
            if (x === null) break;
            if (x === '=== SDLS END ===' || x.startsWith('unknown command')) extra--;
          }
          return parseLs(got);
        }
      }
    }
    return { ok: false, error: 'The device did not answer (no listing came back for SDLS /themes). Check the cable, that the board is on and running UMBRAVOX firmware with USB install, and that no serial monitor has the port open.' };
  }
}

const MSG_UNTOUCHED = 'Nothing on the card was changed.';
function stateMessage(state, folder, failed, reason) {
  const dir = '/themes/' + folder;
  if (state === 'untouched') return MSG_UNTOUCHED;
  if (state === 'mixed') {
    return dir + '/emblem.bin now holds the new picture, but theme.json was not replaced: the card has the new emblem.bin beside the old theme.json ' +
      '(or, in a new folder, an emblem.bin on its own, which the device skips). Run the install again.';
  }
  if (state === 'unknown') {
    if (reason && /old file left as/.test(reason)) {
      return 'The device could not swap in ' + failed + ' and kept the old file under another name (' + reason.replace(/^.*old file left as /, '') +
        '). That pack may be skipped at boot until it is installed again; the next install puts the old file back first.';
    }
    return 'The device did not confirm ' + failed + '. The card holds either the old file or the new one, never a half-written one. Run the install again to be sure.';
  }
  return '';
}

// Upload a pack. Never throws: the result says what happened and what is on
// the card. ok is true only when the device confirmed every file with DONE and
// the size and CRC-32 this page computed.
//   opts: {themeName, decide(plan) -> 'overwrite'|'alternative'|'cancel',
//          signal (AbortSignal), onProgress({phase, file, sent, total}), timeouts}
// folderState: 'untouched' | 'mixed' (new emblem.bin, old theme.json) |
//              'unknown' (a file the device did not confirm) | 'complete'.
export async function installOverSerial(port, files, opts = {}) {
  const res = { ok: false, cancelled: false, refused: false, folder: null, wrote: [], error: null, folderState: 'untouched', message: '', plan: null };
  const chk = checkFiles(files);
  if (!chk.ok) { res.refused = true; res.error = chk.error; res.message = MSG_UNTOUCHED; return res; }
  res.folder = chk.folder;
  let ordered = chk.ordered;
  const total = ordered.reduce((n, f) => n + f.data.length, 0);
  let sent = 0;
  const progress = (phase, file) => { if (opts.onProgress) { try { opts.onProgress({ phase, file, sent, total }); } catch (e) { /* the caller's problem */ } } };
  const s = new Session(port, opts);
  let inUpload = false, failed = null, failedState = 'untouched', failedReason = null;
  try {
    if (s.signal && s.signal.aborted) throw new Cancelled();
    progress('connect', null);
    await s.settle();
    progress('list', null);
    const ls = await s.listThemes();
    if (!ls.ok) throw new Error(ls.error);
    const plan = planInstall(chk.folder, opts.themeName ?? null, ls.dirs.map((folder) => ({ folder, name: null })));
    res.plan = plan;
    if (plan.collision) {
      if (typeof opts.decide !== 'function') throw new Error('/themes/' + plan.collision.folder + ' is already on the card, and nothing was asked about replacing it.');
      // The question may still be open when the user cancels: the signal wins.
      const choice = await new Promise((resolve, reject) => {
        const onAbort = () => reject(new Cancelled());
        if (s.signal) s.signal.addEventListener('abort', onAbort, { once: true });
        Promise.resolve().then(() => opts.decide(plan)).then(resolve, reject)
          .finally(() => { if (s.signal) s.signal.removeEventListener('abort', onAbort); });
      });
      if (s.signal && s.signal.aborted) throw new Cancelled();
      if (choice === 'alternative' && plan.alternative) {
        const re = checkFiles(retargetFiles(files, plan.alternative));
        if (!re.ok) throw new Error(re.error);
        ordered = re.ordered; res.folder = re.folder;
      } else if (choice !== 'overwrite') throw new Cancelled();
    }
    s.totalEnd = s.now() + s.t.total;
    for (const f of ordered) {
      failed = f.devicePath; failedState = 'untouched'; failedReason = null;
      progress('upload', f.devicePath);
      const steps = putSteps(f.devicePath, f.data);
      await s.send(steps[0].line);
      inUpload = true;
      let r = await s.expect((x) => x.kind === 'put' && x.path === f.devicePath && x.size === f.data.length, s.t.put);
      if (!r) throw new Error(f.devicePath + ': no reply to SDPUT');
      if (r.kind === 'err') { inUpload = false; throw new Error(f.devicePath + ': the device refused it: ' + r.reason); }
      for (const st of steps.slice(1, -1)) {
        let okd = false;
        for (let attempt = 0; attempt <= RETRIES && !okd; attempt++) {
          await s.send(st.line);
          r = await s.expect((x) => x.kind === 'ok' && x.offset === st.offset, s.t.chunk);
          if (r && r.kind === 'ok') okd = true;
          else if (r && FATAL.test(r.reason)) throw new Error(f.devicePath + ': ' + r.reason);
        }
        if (!okd) throw new Error(f.devicePath + ': chunk at ' + st.offset + ': no reply after ' + (1 + RETRIES) + ' tries');
        sent += st.n;
        progress('upload', f.devicePath);
      }
      await s.send('END');
      failedState = 'unknown';          // from here the device may swap the file in
      r = await s.expect((x) => x.kind === 'done', s.t.end);
      if (!r) throw new Error(f.devicePath + ': no reply to END');
      if (r.kind === 'err') {
        inUpload = false;
        failedReason = r.reason;
        failedState = /old file left as/.test(r.reason) ? 'unknown' : 'untouched';
        throw new Error(f.devicePath + ': ' + r.reason);
      }
      inUpload = false;
      if (r.size !== f.data.length || r.crc !== crc32hex(f.data)) throw new Error(f.devicePath + ': the device confirmed different bytes (' + r.size + ' bytes, crc ' + r.crc + ')');
      res.wrote.push(f.devicePath);
      failed = null;
    }
    res.ok = true;
    res.folderState = 'complete';
    progress('done', null);
    return res;
  } catch (e) {
    res.cancelled = e instanceof Cancelled;
    res.error = res.cancelled ? 'cancelled' : (e && e.message) || String(e);
    if (inUpload) {
      // Tell the device at once, rather than leave the console locked for its
      // 10 s idle timeout. The ABORT is sent even when cancelled.
      try { await s.send('ABORT'); await s.expect((x) => x.kind === 'aborted', s.t.abort, null); } catch (e2) { /* the first error is the one to report */ }
    }
    res.folderState = failed && failedState === 'unknown' ? 'unknown' : res.wrote.length ? 'mixed' : 'untouched';
    res.message = stateMessage(res.folderState, res.folder, failed, failedReason);
    return res;
  }
}

// --- Web Serial ---------------------------------------------------------------------
export const usbSupported = (g = globalThis) => !!(g && g.navigator && g.navigator.serial);

// A Web Serial SerialPort (already open) -> the port-like object above. One
// pump reads the port all the time, so the browser's buffer never overruns
// while the page is idle, into a queue of at most maxQueued bytes (the oldest
// output is dropped first: it is only stale console chatter).
export function wrapWebSerial(port, { maxQueued = 16384 } = {}) {
  const q = [];
  let queued = 0, closed = false, failure = null, reader = null;
  const waiters = [];
  const wake = () => { while (waiters.length) waiters.shift()(); };
  const pump = (async () => {
    while (!closed && port.readable) {
      reader = port.readable.getReader();
      let done = false;
      try {
        for (;;) {
          const r = await reader.read();
          if (r.done) { done = true; break; }
          let v = r.value;
          if (!(v instanceof Uint8Array) || !v.length) continue;
          if (v.length > maxQueued) v = v.slice(v.length - maxQueued);
          q.push(v); queued += v.length;
          while (queued > maxQueued) queued -= q.shift().length;
          wake();
        }
      } catch (e) {
        // Overrun, break, framing and parity errors are not fatal: the port
        // gets a new readable stream. Anything else ends the connection.
        if (!(e && /^(BufferOverrunError|BreakError|FramingError|ParityError)$/.test(e.name))) { failure = e; done = true; }
      } finally {
        try { reader.releaseLock(); } catch (e) { /* already released */ }
        reader = null;
      }
      if (done) break;
    }
    closed = true;
    wake();
  })();
  return {
    get queued() { return queued; },
    async write(bytes) {
      const w = port.writable.getWriter();
      try { await w.write(bytes); } finally { w.releaseLock(); }
    },
    async read() {
      while (!q.length && !closed) await new Promise((r) => waiters.push(r));
      if (q.length) { const c = q.shift(); queued -= c.length; return c; }
      if (failure) throw failure;
      return null;
    },
    async close() {
      closed = true;
      try { if (reader) await reader.cancel(); } catch (e) { /* closing anyway */ }
      await pump;
      wake();
      if (typeof port.close === 'function') { try { await port.close(); } catch (e) { /* already closed */ } }
    },
  };
}
