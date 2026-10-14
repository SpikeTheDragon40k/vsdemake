'use strict';
// PNG → ARGB8 big-endian texture padded to a power of two.

const { PNG } = require('pngjs');

function nextPow2(n) { let p = 1; while (p < n) p <<= 1; return p; }

/** RGBA8 (un-premultiplied) → padded big-endian ARGB8.
 * @returns {{ width, height, usedWidth, usedHeight, pixels: Buffer }} */
function rgbaTexture(w, h, data) {
  const W = nextPow2(w), H = nextPow2(h);
  const out = Buffer.alloc(W * H * 4); // transparent black padding
  for (let y = 0; y < h; y++) {
    for (let x = 0; x < w; x++) {
      const s = (y * w + x) * 4, d = (y * W + x) * 4;
      // big-endian ARGB word: A R G B in memory order
      out[d] = data[s + 3]; out[d + 1] = data[s]; out[d + 2] = data[s + 1]; out[d + 3] = data[s + 2];
    }
  }
  return { width: W, height: H, usedWidth: w, usedHeight: h, pixels: out };
}

function decodePng(buf) {
  const png = PNG.sync.read(buf); // RGBA8, un-premultiplied
  return rgbaTexture(png.width, png.height, png.data);
}

function textureBlob(tex) {
  const head = Buffer.alloc(24);
  head.writeUInt32BE(tex.width, 0);
  head.writeUInt32BE(tex.height, 4);
  head.writeUInt32BE(1, 8); // ARGB8
  head.writeUInt32BE(tex.usedWidth, 12);
  head.writeUInt32BE(tex.usedHeight, 16);
  head.writeUInt32BE(0, 20);
  return Buffer.concat([head, tex.pixels]);
}

module.exports = { decodePng, rgbaTexture, textureBlob, nextPow2 };
