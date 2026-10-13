'use strict';
// BMFont XML → FNT blob. Only the subset the game's damage font uses.

function parseBmfontXml(xml) {
  const attr = (tag, name) => { const m = new RegExp(`<${tag}\\b[^>]*\\b${name}="([^"]*)"`).exec(xml); return m ? m[1] : null; };
  const lineHeight = Number(attr('common', 'lineHeight'));
  const chars = [];
  const re = /<char\b([^>]*)\/>/g;
  let m;
  while ((m = re.exec(xml))) {
    const a = {};
    for (const [, k, v] of m[1].matchAll(/(\w+)="([^"]*)"/g)) a[k] = Number(v);
    chars.push(a);
  }
  if (!chars.length || !Number.isFinite(lineHeight)) throw new Error('not a BMFont XML');
  return { lineHeight, chars };
}

function fontBlob(font, textureEntry) {
  const head = Buffer.alloc(16);
  head.writeUInt32BE(textureEntry, 0);
  head.writeUInt32BE(font.lineHeight, 4);
  head.writeUInt32BE(font.chars.length, 8);
  const body = Buffer.alloc(20 * font.chars.length);
  font.chars.forEach((c, i) => {
    const o = i * 20;
    body.writeUInt32BE(c.id, o);
    body.writeUInt16BE(c.x, o + 4); body.writeUInt16BE(c.y, o + 6); body.writeUInt16BE(c.width, o + 8); body.writeUInt16BE(c.height, o + 10);
    body.writeInt16BE(c.xoffset, o + 12); body.writeInt16BE(c.yoffset, o + 14); body.writeInt16BE(c.xadvance, o + 16); body.writeInt16BE(0, o + 18);
  });
  return Buffer.concat([head, body]);
}

module.exports = { parseBmfontXml, fontBlob };
