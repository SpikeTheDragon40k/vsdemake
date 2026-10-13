'use strict';
// Tiled JSON map → MAP blob (notes/pack-format.md).

const { fixedName } = require('./util');

// Draw depths and collision flags per layer, from notes/behaviour/stage-map.md.
const LAYER_INFO = {
  Floor: { depth: -2000 }, FloorOverlay: { depth: -1999 }, FakeWalls: { depth: -1998 },
  Walls: { depth: -1997, collides: true }, PlayerWall: { depth: -1997, collides: true, playerOnly: true },
  Obstacle: { depth: -1996, collides: true }, Decals: { depth: -1995 }, Overlay1: { depth: 1 }, Shadows: { depth: 2000 },
};

function tilemapBlob(map, tilesetTextureEntry) {
  if (map.orientation !== 'orthogonal' || map.infinite) throw new Error('only finite orthogonal maps are supported');
  if (map.tilesets.length !== 1) throw new Error(`expected 1 tileset, got ${map.tilesets.length}`);
  const ts = map.tilesets[0];
  const tileLayers = map.layers.filter((l) => l.type === 'tilelayer');
  const objGroups = map.layers.filter((l) => l.type === 'objectgroup');
  for (const l of tileLayers) {
    if (l.encoding || l.compression) throw new Error(`layer ${l.name}: encoded/compressed data not supported`);
    if (l.width !== map.width || l.height !== map.height) throw new Error(`layer ${l.name}: size differs from map`);
  }

  const collides = Buffer.alloc(ts.tilecount);
  for (const t of ts.tiles || []) {
    if ((t.properties || []).some((p) => p.name === 'collides' && p.value === true)) collides[t.id] = 1;
  }

  const parts = [];
  for (const l of tileLayers) {
    const info = LAYER_INFO[l.name] || { depth: 0 };
    const head = Buffer.alloc(40);
    fixedName(l.name).copy(head, 0);
    head.writeInt32BE(info.depth, 32);
    head.writeUInt32BE((info.collides ? 1 : 0) | (l.visible === false ? 2 : 0) | (info.playerOnly ? 4 : 0), 36);
    const data = Buffer.alloc(4 * l.data.length);
    l.data.forEach((g, i) => data.writeUInt32BE(g >>> 0, i * 4));
    parts.push(head, data);
  }
  const layersBuf = Buffer.concat(parts);

  const oparts = [];
  for (const g of objGroups) {
    const head = Buffer.alloc(36);
    fixedName(g.name).copy(head, 0);
    head.writeUInt32BE(g.objects.length, 32);
    oparts.push(head);
    for (const o of g.objects) {
      const props = (o.properties || []).filter((p) => typeof p.value === 'number' || typeof p.value === 'boolean');
      const b = Buffer.alloc(64 + 16 + 4 + props.length * 36);
      fixedName(o.name || '').copy(b, 0);
      fixedName(o.type || '').copy(b, 32);
      b.writeFloatBE(o.x, 64); b.writeFloatBE(o.y, 68); b.writeFloatBE(o.width || 0, 72); b.writeFloatBE(o.height || 0, 76);
      b.writeUInt32BE(props.length, 80);
      props.forEach((p, i) => { fixedName(p.name).copy(b, 84 + i * 36); b.writeFloatBE(Number(p.value), 84 + i * 36 + 32); });
      oparts.push(b);
    }
  }
  const objBuf = Buffer.concat(oparts);

  const HEAD = 64;
  const head = Buffer.alloc(HEAD);
  let o = 0;
  const u32 = (v) => { head.writeUInt32BE(v >>> 0, o); o += 4; };
  u32(map.width); u32(map.height); u32(map.tilewidth); u32(map.tileheight);
  u32(tilesetTextureEntry); u32(ts.firstgid); u32(ts.columns); u32(ts.margin || 0); u32(ts.spacing || 0); u32(ts.tilecount);
  u32(tileLayers.length); u32(objGroups.length);
  const collidesOff = HEAD;
  const layersOff = collidesOff + Math.ceil(collides.length / 16) * 16;
  const objOff = layersOff + layersBuf.length;
  u32(collidesOff); u32(layersOff); u32(objOff); u32(0);
  const collidesPadded = Buffer.alloc(layersOff - collidesOff); collides.copy(collidesPadded);
  return Buffer.concat([head, collidesPadded, layersBuf, objBuf]);
}

module.exports = { tilemapBlob, LAYER_INFO };
