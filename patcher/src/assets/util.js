'use strict';

function fnv1a(str) {
  let h = 0x811c9dc5;
  for (let i = 0; i < str.length; i++) {
    h ^= str.charCodeAt(i);
    h = Math.imul(h, 0x01000193) >>> 0;
  }
  return h >>> 0;
}

/** NUL-terminated string pool with de-duplication. Offset 0 is always "". */
class StringTable {
  constructor() { this.parts = [Buffer.from([0])]; this.size = 1; this.index = new Map([['', 0]]); }
  add(s) {
    s = s == null ? '' : String(s);
    if (this.index.has(s)) return this.index.get(s);
    const off = this.size;
    const b = Buffer.from(s + '\0', 'utf8');
    this.parts.push(b); this.size += b.length; this.index.set(s, off);
    return off;
  }
  buffer() { return Buffer.concat(this.parts); }
}

/** Fixed-width NUL-padded name. */
function fixedName(s, n = 32) {
  const b = Buffer.alloc(n);
  const src = Buffer.from(String(s), 'utf8');
  if (src.length >= n) throw new Error(`name too long for ${n} bytes: ${s}`);
  src.copy(b);
  return b;
}

function align16(buf) {
  const pad = (16 - (buf.length % 16)) % 16;
  return pad ? Buffer.concat([buf, Buffer.alloc(pad)]) : buf;
}

module.exports = { fnv1a, StringTable, fixedName, align16 };
