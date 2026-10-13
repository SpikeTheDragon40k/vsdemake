'use strict';
// JSON tables → TAB blobs and enums → ENM blobs (notes/pack-format.md).

const { TABLES, TYPE_IDS } = require('../tables');
const { StringTable, fixedName } = require('./util');

class TableError extends Error {}

function parseType(t) {
  const [kind, ...rest] = t.split(':');
  return { kind, arg: rest.join(':') };
}

function getPath(obj, path) {
  let v = obj;
  for (const p of path.split('.')) { if (v == null) return undefined; v = v[p]; }
  return v;
}

/**
 * Builds one TAB blob plus any sub-table blobs.
 * @returns {Array<{name: string, blob: Buffer}>}
 */
function tableBlobs(name, def, data, enums) {
  const out = [];
  const strings = new StringTable();
  const pool = []; // u32 words
  const subRecords = {}; // field → records of the sub table

  const enumIndex = (enumName, value, where) => {
    if (value === null || value === undefined) return -1;
    const e = enums[enumName];
    if (!e) throw new TableError(`${where}: unknown enum ${enumName}`);
    const i = e.indexOfValue[String(value)];
    if (i === undefined) throw new TableError(`${where}: value ${JSON.stringify(value)} is not a member of ${enumName}`);
    return i;
  };
  const f32bits = (v) => { const b = Buffer.alloc(4); b.writeFloatBE(Number(v)); return b.readUInt32BE(); };

  const encodeScalar = (type, v, where) => {
    const { kind, arg } = parseType(type);
    switch (kind) {
      case 'f32': return f32bits(v);
      case 'i32': {
        if (!Number.isInteger(v)) throw new TableError(`${where}: expected integer, got ${JSON.stringify(v)}`);
        const b = Buffer.alloc(4); b.writeInt32BE(v); return b.readUInt32BE();
      }
      case 'bool': return v ? 1 : 0;
      case 'str': return strings.add(v);
      case 'enum': { const b = Buffer.alloc(4); b.writeInt32BE(enumIndex(arg, v, where)); return b.readUInt32BE(); }
      default: throw new TableError(`${where}: ${type} is not a scalar type`);
    }
  };

  const encodeRecords = (fields, entries, where, subDefs) => {
    const recs = [];
    for (const [ei, entry] of entries.entries()) {
      let present = 0n;
      const words = [];
      fields.forEach(([fname, type, path], fi) => {
        const v = getPath(entry, path || fname);
        if (v === undefined) { words.push(0); return; }
        present |= 1n << BigInt(fi);
        const w = `${where}[${ei}].${fname}`;
        const { kind, arg } = parseType(type);
        if (kind === 'list') {
          if (!Array.isArray(v)) throw new TableError(`${w}: expected array`);
          const off = pool.length * 4;
          pool.push(v.length);
          for (const item of v) pool.push(encodeScalar(arg, item, w));
          words.push(off);
        } else if (kind === 'sub') {
          const sdef = subDefs[arg];
          if (!sdef) throw new TableError(`${w}: no sub-table ${arg}`);
          const items = sdef.single ? [v] : v;
          if (!Array.isArray(items)) throw new TableError(`${w}: expected array`);
          const list = subRecords[arg] = subRecords[arg] || [];
          const first = list.length;
          list.push(...encodeRecords(sdef.fields, items, w, {}));
          const off = pool.length * 4;
          pool.push(items.length);
          for (let i = 0; i < items.length; i++) pool.push(first + i);
          words.push(off);
        } else {
          words.push(encodeScalar(type, v, w));
        }
      });
      recs.push({ present, words });
    }
    return recs;
  };

  // Groups → records.
  const groups = [];
  const records = [];
  const keyed = def.keyed !== false;
  for (const [key, value] of Object.entries(data)) {
    const entries = keyed ? (def.single ? [value] : value) : [{ value }];
    if (!Array.isArray(entries)) throw new TableError(`${name}.${key}: expected array of level entries`);
    const first = records.length;
    records.push(...encodeRecords(def.fields, entries, `${name}.${key}`, def.sub || {}));
    groups.push({ keyOffset: strings.add(key), enumIndex: def.keyEnum ? enumIndex(def.keyEnum, key, `${name}.${key}`) : -1, first, count: entries.length });
  }

  out.push({ name, blob: assemble(def.fields, groups, records, pool, strings) });
  for (const [sname, sdef] of Object.entries(def.sub || {})) {
    const recs = subRecords[sname] || [];
    // Sub-tables share the parent's pool and strings (offsets are per blob,
    // so they get their own copies).
    out.push({ name: `${name}.${sname}`, blob: assemble(sdef.fields, [], recs, pool, strings) });
  }
  return out;
}

function assemble(fields, groups, records, pool, strings) {
  if (fields.length > 64) throw new TableError('more than 64 fields');
  const HEAD = 48;
  const fieldsBuf = Buffer.concat(fields.map(([fname, type]) => {
    const b = Buffer.alloc(36);
    fixedName(fname).copy(b, 0);
    b.writeUInt32BE(TYPE_IDS[parseType(type).kind], 32);
    return b;
  }));
  const groupsBuf = Buffer.alloc(16 * groups.length);
  groups.forEach((g, i) => {
    groupsBuf.writeUInt32BE(g.keyOffset, i * 16);
    groupsBuf.writeInt32BE(g.enumIndex, i * 16 + 4);
    groupsBuf.writeUInt32BE(g.first, i * 16 + 8);
    groupsBuf.writeUInt32BE(g.count, i * 16 + 12);
  });
  const recordSize = 8 + 4 * fields.length;
  const recordsBuf = Buffer.alloc(recordSize * records.length);
  records.forEach((r, i) => {
    const o = i * recordSize;
    recordsBuf.writeBigUInt64BE(r.present, o);
    r.words.forEach((w, j) => recordsBuf.writeUInt32BE(w >>> 0, o + 8 + 4 * j));
  });
  const poolBuf = Buffer.alloc(4 * pool.length);
  pool.forEach((w, i) => poolBuf.writeUInt32BE(w >>> 0, i * 4));
  const stringsBuf = strings.buffer();

  const pad16 = (n) => Math.ceil(n / 16) * 16;
  const fieldsOff = HEAD;
  const groupsOff = pad16(fieldsOff + fieldsBuf.length);
  const recordsOff = pad16(groupsOff + groupsBuf.length);
  const poolOff = pad16(recordsOff + recordsBuf.length);
  const stringsOff = pad16(poolOff + poolBuf.length);
  const total = stringsOff + stringsBuf.length;
  const out = Buffer.alloc(total);
  let o = 0;
  const u32 = (v) => { out.writeUInt32BE(v >>> 0, o); o += 4; };
  u32(fields.length); u32(recordSize); u32(groups.length); u32(records.length);
  u32(fieldsOff); u32(groupsOff); u32(recordsOff); u32(poolOff); u32(poolBuf.length); u32(stringsOff); u32(stringsBuf.length);
  fieldsBuf.copy(out, fieldsOff); groupsBuf.copy(out, groupsOff); recordsBuf.copy(out, recordsOff); poolBuf.copy(out, poolOff); stringsBuf.copy(out, stringsOff);
  return out;
}

function enumBlob(e) {
  const strings = new StringTable();
  const members = e.names.map((n, i) => ({ name: strings.add(n), value: strings.add(e.values[i]), index: i }));
  const head = Buffer.alloc(16);
  const body = Buffer.alloc(12 * members.length);
  members.forEach((m, i) => { body.writeUInt32BE(m.name, i * 12); body.writeUInt32BE(m.value, i * 12 + 4); body.writeInt32BE(m.index, i * 12 + 8); });
  const str = strings.buffer();
  head.writeUInt32BE(members.length, 0);
  head.writeUInt32BE(16 + body.length, 4);
  head.writeUInt32BE(str.length, 8);
  return Buffer.concat([head, body, str]);
}

/** All TAB blobs for the extracted data. */
function allTableBlobs(data, enums) {
  const out = [];
  for (const [name, def] of Object.entries(TABLES)) {
    if (!(name in data)) throw new TableError(`table ${name} missing from extracted data`);
    out.push(...tableBlobs(name, def, data[name], enums));
  }
  return out;
}

module.exports = { tableBlobs, allTableBlobs, enumBlob, TableError };
