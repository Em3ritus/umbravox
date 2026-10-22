// A minimal in-memory FileSystemDirectoryHandle, enough for install.js.
export class StubFile {
  constructor(dir, name) { this.kind = 'file'; this.name = name; this.dir = dir; }
  async createWritable() {
    const self = this; let buf = [];
    return {
      async write(d) { buf.push(Uint8Array.from(d)); },
      async close() {
        let n = 0; for (const b of buf) n += b.length;
        const out = new Uint8Array(n); let o = 0; for (const b of buf) { out.set(b, o); o += b.length; }
        self.dir.files.set(self.name, self.dir.corrupt ? self.dir.corrupt(self.name, out) : out);
      },
    };
  }
  async getFile() {
    const d = this.dir.files.get(this.name);
    const rd = this.dir.readCorrupt ? this.dir.readCorrupt(this.name, d) : d;
    return { size: rd.length, async arrayBuffer() { return rd.buffer.slice(rd.byteOffset, rd.byteOffset + rd.byteLength); },
             async text() { return new TextDecoder().decode(rd); } };
  }
}
export class StubDir {
  constructor(name = '') { this.kind = 'directory'; this.name = name; this.dirs = new Map(); this.files = new Map(); }
  async getDirectoryHandle(name, opt = {}) {
    if (!this.dirs.has(name)) {
      if (!opt.create) { const e = new Error('not found: ' + name); e.name = 'NotFoundError'; throw e; }
      const d = new StubDir(name); d.readCorrupt = this.readCorrupt; d.corrupt = this.corrupt; this.dirs.set(name, d);
    }
    return this.dirs.get(name);
  }
  async getFileHandle(name, opt = {}) {
    if (!this.files.has(name)) {
      if (!opt.create) { const e = new Error('not found: ' + name); e.name = 'NotFoundError'; throw e; }
      this.files.set(name, new Uint8Array(0));
    }
    return new StubFile(this, name);
  }
  async removeEntry(name) { this.files.delete(name); this.dirs.delete(name); }
  async *values() {
    for (const n of this.dirs.keys()) yield this.dirs.get(n);
    for (const n of this.files.keys()) yield new StubFile(this, n);
  }
  async *entries() { for await (const v of this.values()) yield [v.name, v]; }
  // test helper: put a file at a/b/c
  put(p, data) {
    const parts = p.split('/'); let d = this;
    for (const s of parts.slice(0, -1)) {
      if (!d.dirs.has(s)) { const n = new StubDir(s); n.readCorrupt = d.readCorrupt; n.corrupt = d.corrupt; d.dirs.set(s, n); }
      d = d.dirs.get(s);
    }
    d.files.set(parts.at(-1), typeof data === 'string' ? new TextEncoder().encode(data) : data);
  }
  get(p) {
    let d = this; const parts = p.split('/');
    for (const s of parts.slice(0, -1)) d = d.dirs.get(s);
    return d.files.get(parts.at(-1));
  }
}
