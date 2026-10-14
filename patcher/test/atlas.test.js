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

test('level-up text follows the entry key order and the passive/weapon wording', () => {
  const { levelUpText, getLang } = require('../src/assets/lang');
  const t = {
    weaponLevelUp_all: '[All] ', weaponLevelUp_damage: 'dmg %0\n', weaponLevelUp_damageAll: 'dmgAll %0%\n',
    weaponLevelUp_area: 'area %0%\n', weaponLevelUp_projectile: '%0 proj\n', weaponLevelUp_projectiles: '%0 projs\n',
    weaponLevelUp_cooldown: 'cd %0 s\n', weaponLevelUp_effect: 'eff %0 s\n',
  };
  assert.equal(levelUpText({ area: 0.1, power: 0.5 }, false, t), 'area 10%\ndmg 5\n');
  assert.equal(levelUpText({ power: 0.1 }, true, t), 'dmgAll 10%\n');
  assert.equal(levelUpText({ amount: 2, interval: -200, level: 3 }, false, t), '2 projs\ncd 0.2 s\n');
  assert.equal(levelUpText({ amount: 1, duration: 500 }, false, t), '1 proj\neff 0.5 s\n');
  assert.equal(getLang({ k: 'a %0 b %1' }, 'k', [1, 2]), 'a 1 b 2');
});
