'use strict';
// Pack format round trip: builds the pack in memory and reads it back with a
// small reader written against notes/pack-format.md. Counts and structure
// only, no game values.

const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('fs');
const path = require('path');
const { openSource, BUNDLE } = require('../src/locate');
const { deobfuscate } = require('../src/deobfuscate');
const { extract } = require('../src/extract');
const { buildPack } = require('../src/pack');
const { fnv1a } = require('../src/assets/util');
const { TABLES } = require('../src/tables');

const GAME = process.env.VSDEMAKE_GAME || (fs.existsSync(path.join(__dirname, '..', '..', 'vs-1.0')) ? path.join(__dirname, '..', '..', 'vs-1.0') : null);
const skip = GAME ? false : 'no game folder';

function readPack(buf) {
  assert.equal(buf.toString('ascii', 0, 4), 'VSPK');
  assert.equal(buf.readUInt32BE(4), 1);
  assert.equal(buf.readBigUInt64BE(8), 8292959317163221061n);
  const n = buf.readUInt32BE(16), dirOff = buf.readUInt32BE(20);
  assert.equal(buf.readUInt32BE(24), buf.length);
  const entries = [];
  for (let i = 0; i < n; i++) {
    const o = dirOff + i * 48;
    const kind = buf.toString('ascii', o, o + 4).replace(/\0+$/, '');
    const name = buf.toString('utf8', o + 8, o + 40).replace(/\0+$/, '');
    const off = buf.readUInt32BE(o + 40), size = buf.readUInt32BE(o + 44);
    assert.equal(off % 16, 0, `${kind}:${name} aligned`);
    assert.ok(off + size <= buf.length);
    entries.push({ kind, name, blob: buf.subarray(off, off + size) });
  }
  return entries;
}

const cstr = (b, off) => b.toString('utf8', off, b.indexOf(0, off));

function readTable(blob) {
  const fieldCount = blob.readUInt32BE(0), recordSize = blob.readUInt32BE(4), groupCount = blob.readUInt32BE(8), recordCount = blob.readUInt32BE(12);
  const [fieldsOff, groupsOff, recordsOff, poolOff, , stringsOff] = [16, 20, 24, 28, 32, 36].map((o) => blob.readUInt32BE(o));
  assert.equal(recordSize, 8 + 4 * fieldCount);
  const fields = [];
  for (let i = 0; i < fieldCount; i++) fields.push({ name: cstr(blob, fieldsOff + i * 36), type: blob.readUInt32BE(fieldsOff + i * 36 + 32) });
  const groups = [];
  for (let i = 0; i < groupCount; i++) {
    const o = groupsOff + i * 16;
    groups.push({ key: cstr(blob, stringsOff + blob.readUInt32BE(o)), enumIndex: blob.readInt32BE(o + 4), first: blob.readUInt32BE(o + 8), count: blob.readUInt32BE(o + 12) });
  }
  const record = (i) => {
    const o = recordsOff + i * recordSize;
    const present = blob.readBigUInt64BE(o);
    const r = {};
    fields.forEach((f, j) => {
      if (!((present >> BigInt(j)) & 1n)) return;
      const w = o + 8 + 4 * j;
      switch (f.type) {
        case 1: r[f.name] = blob.readFloatBE(w); break;
        case 2: case 3: case 5: r[f.name] = blob.readInt32BE(w); break;
        case 4: r[f.name] = cstr(blob, stringsOff + blob.readUInt32BE(w)); break;
        case 6: case 7: { const p = poolOff + blob.readUInt32BE(w); const n = blob.readUInt32BE(p); r[f.name] = Array.from({ length: n }, (_, k) => blob.readUInt32BE(p + 4 + 4 * k)); break; }
      }
    });
    return r;
  };
  return { fields, groups, recordCount, record };
}

let cache;
function pack() {
  if (cache) return cache;
  const source = openSource(GAME);
  const x = extract(deobfuscate(source.read(BUNDLE).toString('utf8')).code);
  const built = buildPack(source, x);
  cache = { x, built, entries: readPack(built.buffer) };
  return cache;
}

test('pack has the expected directory', { skip }, () => {
  const { entries } = pack();
  const count = (k) => entries.filter((e) => e.kind === k).length;
  assert.equal(count('TEX'), 20);
  assert.equal(count('ATL'), 10);
  assert.equal(count('MAP'), 2);
  assert.equal(count('FNT'), 1);
  assert.equal(count('ENM'), 12);
  assert.equal(count('TAB'), Object.values(TABLES).reduce((n, d) => n + 1 + Object.keys(d.sub || {}).length, 0));
  const names = new Set(entries.map((e) => `${e.kind}:${e.name}`));
  for (const n of ['TEX:enemies', 'TEX:title_vsdemake', 'ATL:characters', 'MAP:Forest', 'MAP:Library', 'FNT:damNum-export', 'TAB:weapons', 'TAB:stages.events', 'ENM:weapons']) assert.ok(names.has(n), n);
});

test('textures are power-of-two ARGB8 and padded, not resized', { skip }, () => {
  const { entries } = pack();
  for (const e of entries.filter((e) => e.kind === 'TEX')) {
    const w = e.blob.readUInt32BE(0), h = e.blob.readUInt32BE(4), fmt = e.blob.readUInt32BE(8), uw = e.blob.readUInt32BE(12), uh = e.blob.readUInt32BE(16);
    assert.equal(fmt, 1);
    assert.ok((w & (w - 1)) === 0 && (h & (h - 1)) === 0, `${e.name} POT`);
    assert.ok(uw <= w && uh <= h);
    assert.equal(e.blob.length, 24 + w * h * 4);
  }
  const enemies = entries.find((e) => e.kind === 'TEX' && e.name === 'enemies').blob;
  assert.deepEqual([enemies.readUInt32BE(12), enemies.readUInt32BE(16), enemies.readUInt32BE(0), enemies.readUInt32BE(4)], [1021, 1467, 1024, 2048]);
});

test('atlas frames are sorted by hash and resolve by name', { skip }, () => {
  const { entries } = pack();
  const atl = entries.find((e) => e.kind === 'ATL' && e.name === 'characters').blob;
  const texIdx = atl.readUInt32BE(0), n = atl.readUInt32BE(4), strOff = atl.readUInt32BE(8);
  assert.equal(entries[texIdx].kind, 'TEX');
  assert.equal(entries[texIdx].name, 'characters');
  assert.equal(n, 60);
  let prev = -1;
  const names = [];
  for (let i = 0; i < n; i++) {
    const o = 16 + i * 28;
    const h = atl.readUInt32BE(o);
    assert.ok(h > prev, 'sorted');
    prev = h;
    const name = cstr(atl, strOff + atl.readUInt32BE(o + 4));
    assert.equal(fnv1a(name), h);
    names.push(name);
  }
  assert.ok(names.includes('Antonio_01.png'));
});

test('tables round-trip counts and the enum ids line up', { skip }, () => {
  const { entries, x } = pack();
  const tab = (name) => readTable(entries.find((e) => e.kind === 'TAB' && e.name === name).blob);
  const weapons = tab('weapons');
  assert.equal(weapons.groups.length, 41);
  assert.equal(weapons.recordCount, Object.values(x.data.weapons).reduce((n, a) => n + a.length, 0));
  assert.deepEqual(weapons.fields.map((f) => f.name), TABLES.weapons.fields.map(([f]) => f));
  const whip = weapons.groups.find((g) => g.key === 'WHIP');
  const rec = weapons.record(whip.first);
  assert.equal(rec.level, 1);
  assert.equal(rec.bulletType, whip.enumIndex, 'bulletType enum id equals the group id');
  assert.equal(typeof rec.interval, 'number');

  const stages = tab('stages');
  assert.deepEqual(stages.groups.map((g) => g.count), [31, 1, 31, 31]);
  const events = tab('stages.events');
  const totalEvents = Object.values(x.data.stages).flat().reduce((n, r) => n + (r.events ? r.events.length : 0), 0);
  assert.equal(events.recordCount, totalEvents);
  const row2 = stages.record(stages.groups[0].first + 2);
  assert.ok(Array.isArray(row2.events) && row2.events.length === x.data.stages.FOREST[2].events.length);

  const constants = tab('constants');
  assert.ok(constants.groups.some((g) => g.key === 'PlayerPxSpeed'));
  const enemies = tab('enemies');
  assert.equal(enemies.groups.length, 52);
  assert.ok(enemies.groups.every((g) => g.enumIndex >= 0), 'every enemy key is an enum member');
});

test('enum blobs list members in declaration order', { skip }, () => {
  const { entries } = pack();
  const e = entries.find((e) => e.kind === 'ENM' && e.name === 'weapons').blob;
  const n = e.readUInt32BE(0), strOff = e.readUInt32BE(4);
  const first = cstr(e, strOff + e.readUInt32BE(16));
  assert.equal(first, 'VOID');
  assert.ok(n > 40);
  for (let i = 0; i < n; i++) assert.equal(e.readInt32BE(16 + i * 12 + 8), i);
});
