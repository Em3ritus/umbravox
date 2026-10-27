// A fake UMBRAVOX console as seen from the USB wire (not a test file itself).
// A small JS port of device/sdcmd.h's upload state machine plus the SDLS
// listing of device/sdlog.cpp, with knobs for the faults a real link has:
// boot banner noise, a status line between replies, reads split anywhere,
// CRLF, lost replies, corrupted chunks, a device that goes silent.
// The strongest test drives the REAL C++ state machine (sim/sdcmd_console);
// this one exists so each fault can be aimed precisely.
import { crc32 } from '../js/zip.js';

const enc = new TextEncoder();
const hex8 = (n) => (n >>> 0).toString(16).padStart(8, '0');
const STATUS = 'f30    [scan ] draw 12 blit  3 ms | wifi   41/512  (2.4 30, 5 11) 2 fr ch6   ok | ble   12/2048   | gps no   0 sv | touch ok | alert - - | mesh 0n 0r 0%lost | hits 0 rem 0 | heap 81234';

export class FakeDevice {
  constructor(o = {}) {
    this.o = { crlf: true, banner: false, statusEvery: 0, split: 0, dropOk: [], corrupt: null,
      silentAfter: Infinity, bootSwallow: 0, ...o };
    this.files = new Map();                  // lower-cased path -> Uint8Array
    this.dirs = new Map([['/', '/']]);       // lower-cased -> as created
    for (const d of this.o.dirs || []) this.mkdir(d);
    this.received = [];                      // every line the host sent
    this.out = [];                           // bytes queued for the host
    this.waiters = [];
    this.inBuf = '';
    this.up = null;
    this.replies = 0;
    this.okCount = 0;
    this.writes = 0;
    this.reads = 0;
    if (this.o.banner) {
      this.emit(new Uint8Array([0xff, 0xfe, 0x00]));
      this.say('ets Jun  8 2016 00:22:57');
      this.say('rst:0x1 (POWERON_RESET),boot:0x13 (SPI_FAST_FLASH_BOOT)');
      this.say('');
      this.say('UMBRAVOX bring-up');
      this.say('chip  : ESP32-D0WD-V3 rev 3, 2 core(s)');
      this.say('panel up @ 40 MHz SPI, GPS on GPIO 22 @ 9600');
      this.say(STATUS);
    }
  }

  mkdir(p) {
    const parts = p.split('/').filter(Boolean);
    let cur = '';
    for (const x of parts) { cur += '/' + x; if (!this.dirs.has(cur.toLowerCase())) this.dirs.set(cur.toLowerCase(), cur); }
  }
  get(p) { return this.files.get(p.toLowerCase()); }

  emit(bytes) { this.out.push(...bytes); this.wake(); }
  say(line) { this.emit(enc.encode(line + (this.o.crlf ? '\r\n' : '\n'))); }
  reply(line) {
    this.replies++;
    if (this.replies > this.o.silentAfter) return;
    this.say(line);
    if (this.o.statusEvery && this.replies % this.o.statusEvery === 0) this.say(STATUS);
  }
  wake() { const w = this.waiters; this.waiters = []; for (const f of w) f(); }

  // The port-like object the client is given.
  port() {
    return {
      write: async (bytes) => { this.writes++; this.rx(bytes); },
      read: async () => {
        this.reads++;
        while (!this.out.length) await new Promise((r) => this.waiters.push(r));
        let n = this.out.length;
        if (this.o.split) n = Math.min(n, typeof this.o.split === 'function' ? this.o.split() : this.o.split);
        return Uint8Array.from(this.out.splice(0, n));
      },
    };
  }

  rx(bytes) {
    for (const b of bytes) {
      if (b === 10 || b === 13) {
        const l = this.inBuf; this.inBuf = '';
        if (l) this.line(l);
      } else this.inBuf += String.fromCharCode(b);
    }
  }

  line(l) {
    this.received.push(l);
    if (this.o.bootSwallow > 0) { this.o.bootSwallow--; return; }   // still booting: the UART lost it
    const t = l.split(' ');
    const w = t[0];
    if (w === 'SDPUT') return this.put(t);
    if (w === 'D' || w === 'END' || w === 'ABORT') {
      if (!this.up) return this.reply('ERR no upload');
      if (w === 'ABORT') { this.drop(); return this.reply('OK aborted'); }
      if (w === 'D') return this.chunk(t);
      return this.finish();
    }
    if (this.up) return this.reply('ERR upload in progress (ABORT to cancel)');
    if (w === 'SDLS') return this.ls(l.slice(5) || '/');
    this.reply("unknown command '" + l + "'");
  }

  put(t) {
    if (this.up) this.drop();
    if (t.length !== 4) return this.reply('ERR usage: SDPUT <path> <size> <crc32hex>');
    const m = /^\/themes\/([A-Za-z0-9_-]{1,31})\/(theme\.json|emblem\.bin)$/i.exec(t[1]);
    if (!m || /^(con|prn|aux|nul|com[0-9]|lpt[0-9])$/i.test(m[1])) return this.reply('ERR path not allowed');
    if (!/^(0|[1-9][0-9]{0,9})$/.test(t[2])) return this.reply('ERR bad size');
    if (!/^[0-9a-fA-F]{8}$/.test(t[3])) return this.reply('ERR bad crc32 (8 hex digits)');
    const size = Number(t[2]), file = m[2].toLowerCase();
    if (size === 0) return this.reply('ERR empty file');
    if (size > (file === 'theme.json' ? 4096 : 9728)) return this.reply('ERR too large');
    const dir = '/themes/' + m[1];
    const hadDir = this.dirs.has(dir.toLowerCase());
    this.mkdir(dir);
    this.up = { target: dir + '/' + file, size, crc: parseInt(t[3], 16) >>> 0, data: [], dir, newDir: !hadDir };
    this.reply('OK put ' + this.up.target + ' ' + size);
  }

  chunk(t) {
    const u = this.up;
    if (t.length !== 3 || !/^(0|[1-9][0-9]{0,9})$/.test(t[1])) return this.reply('ERR usage: D <offset> <hex>');
    if (!/^([0-9a-fA-F]{2}){1,96}$/.test(t[2])) return this.reply('ERR bad hex');
    const off = Number(t[1]);
    let data = t[2].match(/../g).map((h) => parseInt(h, 16));
    if (this.o.corrupt) data = this.o.corrupt(u.target, off, data);
    if (off + data.length > u.size) return this.reply('ERR past declared size ' + u.size);
    if (off > u.data.length) return this.reply('ERR gap, expected offset ' + u.data.length);
    const same = Math.min(u.data.length - off, data.length);
    for (let i = 0; i < same; i++) if (u.data[off + i] !== data[i]) return this.reply('ERR chunk at ' + off + ' differs from bytes written');
    u.data.push(...data.slice(same));
    this.okCount++;
    if (this.o.dropOk.includes(this.okCount)) { this.replies++; return; }   // the reply is lost on the wire
    this.reply('OK ' + off);
  }

  finish() {
    const u = this.up;
    if (u.data.length !== u.size) { this.drop(); return this.reply('ERR short, ' + u.data.length + ' of ' + u.size + ' bytes'); }
    const bytes = Uint8Array.from(u.data);
    if (crc32(bytes) !== u.crc) { this.drop(); return this.reply('ERR crc mismatch on the received bytes'); }
    this.up = null;
    this.files.set(u.target.toLowerCase(), bytes);
    this.reply('DONE ' + u.size + ' ' + hex8(u.crc));
  }

  drop() {
    const u = this.up;
    this.up = null;
    if (u && u.newDir && ![...this.files.keys()].some((k) => k.startsWith(u.dir.toLowerCase() + '/'))) this.dirs.delete(u.dir.toLowerCase());
  }

  ls(d) {
    this.replies++;
    this.say('=== SDLS ' + d + ' ===');
    if (!this.dirs.has(d.toLowerCase())) this.say('sdlog: cannot open ' + d);
    else {
      const pre = d.toLowerCase() === '/' ? '/' : d.toLowerCase() + '/';
      for (const [k, v] of this.dirs) if (k.startsWith(pre) && k !== d.toLowerCase() && !k.slice(pre.length).includes('/')) this.say('   <dir>  ' + v.slice(pre.length) + '/');
      if (this.o.statusEvery) this.say(STATUS);
      for (const [k, v] of this.files) if (k.startsWith(pre) && !k.slice(pre.length).includes('/')) this.say(String(v.length).padStart(8) + '  ' + k.slice(pre.length));
    }
    this.say('=== SDLS END ===');
  }
}
