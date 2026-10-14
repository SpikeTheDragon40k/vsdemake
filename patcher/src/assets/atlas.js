'use strict';
// TexturePacker "multiatlas" JSON → ATL blob (notes/pack-format.md).

const { fnv1a, StringTable } = require('./util');

/** Parses the JSON; returns one entry per texture page {image, size, frames[]}. */
function parseMultiatlas(json) {
  if (!json.textures) throw new Error('not a TexturePacker multiatlas (no "textures")');
  return json.textures.map((t) => ({
    image: t.image,
    size: t.size,
    // Phaser applies spriteSourceSize/sourceSize only when "trimmed" is true.
    // Many untrimmed frames in this build carry stale offsets (e.g. Bat3_i01:
    // 30x27 frame, offset 48,51), which must be ignored as Phaser does.
    frames: t.frames.map((f) => ({
      name: f.filename,
      x: f.frame.x, y: f.frame.y, w: f.frame.w, h: f.frame.h,
      offX: f.trimmed ? f.spriteSourceSize.x : 0, offY: f.trimmed ? f.spriteSourceSize.y : 0,
      srcW: f.trimmed ? f.sourceSize.w : f.frame.w, srcH: f.trimmed ? f.sourceSize.h : f.frame.h,
      rotated: f.rotated ? 1 : 0,
    })),
  }));
}

function atlasBlob(page, textureEntry) {
  const strings = new StringTable();
  const frames = page.frames.map((f) => ({ ...f, hash: fnv1a(f.name), nameOffset: strings.add(f.name) }))
    .sort((a, b) => a.hash - b.hash);
  for (let i = 1; i < frames.length; i++) {
    if (frames[i].hash === frames[i - 1].hash) throw new Error(`atlas ${page.image}: hash collision ${frames[i].name} / ${frames[i - 1].name}`);
  }
  const FRAME = 28;
  const head = Buffer.alloc(16);
  const body = Buffer.alloc(FRAME * frames.length);
  frames.forEach((f, i) => {
    const o = i * FRAME;
    body.writeUInt32BE(f.hash, o);
    body.writeUInt32BE(f.nameOffset, o + 4);
    body.writeUInt16BE(f.x, o + 8); body.writeUInt16BE(f.y, o + 10); body.writeUInt16BE(f.w, o + 12); body.writeUInt16BE(f.h, o + 14);
    body.writeInt16BE(f.offX, o + 16); body.writeInt16BE(f.offY, o + 18);
    body.writeUInt16BE(f.srcW, o + 20); body.writeUInt16BE(f.srcH, o + 22);
    body.writeUInt16BE(f.rotated, o + 24); body.writeUInt16BE(0, o + 26);
  });
  const str = strings.buffer();
  head.writeUInt32BE(textureEntry, 0);
  head.writeUInt32BE(frames.length, 4);
  head.writeUInt32BE(16 + body.length, 8);
  head.writeUInt32BE(str.length, 12);
  return Buffer.concat([head, body, str]);
}

module.exports = { parseMultiatlas, atlasBlob };
