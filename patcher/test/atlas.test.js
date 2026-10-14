'use strict';
// Atlas frame rules; synthetic data only, runs without the game folder.

const test = require('node:test');
const assert = require('node:assert/strict');
const { parseMultiatlas } = require('../src/assets/atlas');

function atlas(frame) {
  return { textures: [{ image: 'a.png', size: { w: 64, h: 64 }, frames: [frame] }] };
}

test('untrimmed frames ignore stale spriteSourceSize offsets (as Phaser)', () => {
  const [page] = parseMultiatlas(atlas({
    filename: 'x.png', rotated: false, trimmed: false,
    sourceSize: { w: 30, h: 27 }, spriteSourceSize: { x: 48, y: 51, w: 30, h: 27 },
    frame: { x: 4, y: 5, w: 30, h: 27 },
  }));
  const f = page.frames[0];
  assert.deepEqual([f.offX, f.offY, f.srcW, f.srcH], [0, 0, 30, 27]);
});

test('trimmed frames keep their offsets and source size', () => {
  const [page] = parseMultiatlas(atlas({
    filename: 'y.png', rotated: false, trimmed: true,
    sourceSize: { w: 40, h: 40 }, spriteSourceSize: { x: 3, y: 7, w: 30, h: 27 },
    frame: { x: 4, y: 5, w: 30, h: 27 },
  }));
  const f = page.frames[0];
  assert.deepEqual([f.offX, f.offY, f.srcW, f.srcH], [3, 7, 40, 40]);
});
