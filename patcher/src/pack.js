'use strict';
// Assembles vsdata.vspack (notes/pack-format.md) from the game source and
// the extracted tables.

const path = require('path');
const { ASSETS } = require('./locate');
const { decodePng, rgbaTexture, textureBlob } = require('./assets/texture');
const { buildLogo } = require('./assets/logo');
const { PNG } = require('pngjs');
const { parseMultiatlas, atlasBlob } = require('./assets/atlas');
const { tilemapBlob } = require('./assets/tilemap');
const { parseBmfontXml, fontBlob } = require('./assets/font');
const { allTableBlobs, enumBlob } = require('./assets/table');
const { resolveEnums, E } = require('./tables');
const { fixedName } = require('./assets/util');

const MANIFEST = 8292959317163221061n;

// Short directory names for the enums (see tables.js E).
const ENUM_NAMES = {
  [E.weapon]: 'weapons', [E.char]: 'characters', [E.pickup]: 'pickups', [E.enemy]: 'enemies', [E.stage]: 'stages',
  [E.hitvfx]: 'hitvfx', [E.destructible]: 'destructibles', [E.event]: 'events', [E.prize]: 'prizes', [E.bgm]: 'bgm',
  [E.spawn]: 'spawn', ReachLV5: 'achievements',
};

class Pack {
  constructor() { this.entries = []; }
  add(kind, name, blob) {
    if (this.entries.some((e) => e.kind === kind && e.name === name)) throw new Error(`duplicate pack entry ${kind}:${name}`);
    this.entries.push({ kind, name, blob });
    return this.entries.length - 1;
  }
  find(kind, name) { return this.entries.findIndex((e) => e.kind === kind && e.name === name); }
  build(manifest = MANIFEST) {
    const HEAD = 32, DIR = 48;
    const dirOff = HEAD;
    let off = Math.ceil((dirOff + DIR * this.entries.length) / 16) * 16;
    const placed = this.entries.map((e) => { const p = { ...e, off }; off = Math.ceil((off + e.blob.length) / 16) * 16; return p; });
    const total = off;
    const out = Buffer.alloc(total);
    out.write('VSPK', 0, 'ascii');
    out.writeUInt32BE(1, 4);
    out.writeBigUInt64BE(BigInt(manifest), 8);
    out.writeUInt32BE(placed.length, 16);
    out.writeUInt32BE(dirOff, 20);
    out.writeUInt32BE(total, 24);
    out.writeUInt32BE(1, 28);
    placed.forEach((e, i) => {
      const o = dirOff + i * DIR;
      out.write(e.kind.padEnd(4, '\0'), o, 'ascii');
      out.writeUInt32BE(0, o + 4);
      fixedName(e.name).copy(out, o + 8);
      out.writeUInt32BE(e.off, o + 40);
      out.writeUInt32BE(e.blob.length, o + 44);
      e.blob.copy(out, e.off);
    });
    return out;
  }
}

/**
 * @param source  locate.js source (read/list)
 * @param extracted  result of extract.js ({data, enums})
 * @param log  function(string)
 */
function buildPack(source, extracted, log = () => {}) {
  const pack = new Pack();
  const base = (f) => path.posix.basename(f).replace(/\.[^.]+$/, '');
  const files = source.list(ASSETS);

  // Textures: every PNG under img/ and tilesets/.
  for (const f of files.filter((x) => x.endsWith('.png') && /\/(img|tilesets|fonts)\//.test(x))) {
    const tex = decodePng(source.read(f));
    pack.add('TEX', base(f), textureBlob(tex));
    log(`TEX ${base(f)} ${tex.usedWidth}x${tex.usedHeight} → ${tex.width}x${tex.height}`);
  }

  // The demake's own title logo, cut from the user's title.png (logo.js).
  const titleFile = files.find((x) => /\/img\/title\.png$/.test(x));
  if (titleFile) {
    const logo = buildLogo(PNG.sync.read(source.read(titleFile)));
    pack.add('TEX', 'title_vsdemake', textureBlob(rgbaTexture(logo.width, logo.height, logo.data)));
    log(`TEX title_vsdemake ${logo.width}x${logo.height} (built from title.png)`);
  }

  // Atlases and maps: JSON under img/ and tilesets/.
  for (const f of files.filter((x) => x.endsWith('.json') && /\/(img|tilesets)\//.test(x))) {
    const json = JSON.parse(source.read(f).toString('utf8'));
    if (json.textures) {
      for (const page of parseMultiatlas(json)) {
        const texIdx = pack.find('TEX', base(page.image));
        if (texIdx < 0) throw new Error(`atlas ${f}: texture ${page.image} not found`);
        pack.add('ATL', base(f), atlasBlob(page, texIdx));
        log(`ATL ${base(f)} ${page.frames.length} frames`);
      }
    } else if (json.layers) {
      const texIdx = pack.find('TEX', base(json.tilesets[0].image));
      if (texIdx < 0) throw new Error(`map ${f}: tileset image ${json.tilesets[0].image} not found`);
      pack.add('MAP', base(f), tilemapBlob(json, texIdx));
      log(`MAP ${base(f)} ${json.width}x${json.height}`);
    } else {
      log(`skip ${f}: unknown JSON kind`);
    }
  }

  // Fonts.
  for (const f of files.filter((x) => x.endsWith('.xml') && /\/fonts\//.test(x))) {
    const font = parseBmfontXml(source.read(f).toString('utf8'));
    const texIdx = pack.find('TEX', base(f));
    if (texIdx < 0) throw new Error(`font ${f}: texture ${base(f)}.png not found`);
    pack.add('FNT', base(f), fontBlob(font, texIdx));
    log(`FNT ${base(f)} ${font.chars.length} glyphs`);
  }

  // Enums and tables.
  const enums = resolveEnums(extracted.enums, extracted.data);
  for (const [key, e] of Object.entries(enums)) {
    pack.add('ENM', ENUM_NAMES[key] || key, enumBlob(e));
  }
  for (const t of allTableBlobs(extracted.data, enums)) {
    pack.add('TAB', t.name, t.blob);
    log(`TAB ${t.name} ${t.blob.length} bytes`);
  }

  return { buffer: pack.build(), entries: pack.entries.map((e) => ({ kind: e.kind, name: e.name, size: e.blob.length })) };
}

module.exports = { buildPack, Pack, ENUM_NAMES };
