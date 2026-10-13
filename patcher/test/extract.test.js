'use strict';
// Golden tests. They assert structure and counts only, never values that
// would reproduce game content. They need a game folder:
//   VSDEMAKE_GAME=/path/to/depot_1794681 node --test patcher/test
// and skip with a notice when it is not set (CI has no game).

const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('fs');
const path = require('path');
const { openSource, verify, BUNDLE } = require('../src/locate');
const { deobfuscate } = require('../src/deobfuscate');
const { extract, TABLES } = require('../src/extract');
const { validateAll } = require('../src/validate');

const GAME = process.env.VSDEMAKE_GAME || (fs.existsSync(path.join(__dirname, '..', '..', 'vs-1.0')) ? path.join(__dirname, '..', '..', 'vs-1.0') : null);
const skip = GAME ? false : 'VSDEMAKE_GAME not set and no vs-1.0/ folder: skipping tests that need the game';

let cache;
function pipeline() {
  if (cache) return cache;
  const source = openSource(GAME);
  const v = verify(source);
  const deob = deobfuscate(source.read(BUNDLE).toString('utf8'));
  const x = extract(deob.code);
  cache = { source, v, deob, x };
  return cache;
}

test('known build is recognised', { skip }, () => {
  const { v } = pipeline();
  assert.equal(v.status, 'ok', JSON.stringify(v));
  assert.equal(v.manifest, '8292959317163221061');
});

test('de-obfuscation resolves every decoder call', { skip }, () => {
  const { deob } = pipeline();
  assert.ok(deob.stats.resolvedCalls > 10000);
  assert.ok(deob.stats.strings > 1000);
  assert.ok(!/a0_0x[0-9a-f]+\(/.test(deob.code), 'decoder calls remain');
  assert.ok(deob.code.split('\n').length > 5000, 'output should be line-broken');
});

test('all tables are found with the expected entry counts', { skip }, () => {
  const { x } = pipeline();
  const counts = Object.fromEntries(Object.entries(x.data).map(([k, v]) => [k, Object.keys(v).length]));
  assert.deepEqual(counts, {
    weapons: 41, characters: 40, enemies: 52, stages: 4, pickups: 17, powerups: 14,
    destructibles: 4, achievements: 40, hitvfx: 8, constants: 14,
  });
  assert.deepEqual(Object.values(x.data.stages).map((s) => s.length), [31, 1, 31, 31]);
  assert.equal(Object.values(x.data.weapons).filter((w) => w[0].isEvolution).length, 9);
  assert.equal(Object.values(x.data.weapons).filter((w) => w[0].isPowerUp).length, 14);
  for (const name of Object.keys(TABLES)) assert.ok(x.sources.tables[name].line > 1, `${name} has a source line`);
});

test('known quirks are reported, not hidden', { skip }, () => {
  const { x } = pipeline();
  const w = x.warnings.join('\n');
  for (const missing of ['MEDUSA1_FAST', 'MEDUSA2_FAST', 'BOSS_XLDETH']) assert.match(w, new RegExp(`${missing} does not exist`));
  assert.match(w, /enemies\.BOSS_XLDEATH: duplicate key/);
  assert.equal(x.warnings.filter((m) => m.includes('unsupported expression')).length, 0, 'no unevaluated expressions');
});

test('constants are evaluated', { skip }, () => {
  const { x } = pipeline();
  const c = x.data.constants;
  for (const k of ['PlayerPxSpeed', 'EnemySpeed', 'ProjectileSpeed', 'PixelScale']) assert.equal(typeof c[k], 'number', k);
  assert.ok(c.PlayerPxSpeed > 0 && c.EnemySpeed > 0 && c.ProjectileSpeed > 0);
});

test('extracted tables pass the schemas', { skip }, () => {
  const { x } = pipeline();
  assert.deepEqual(validateAll(x.data), []);
});

test('a tampered bundle is reported as modified', { skip }, () => {
  const { source } = pipeline();
  const original = source.read.bind(source);
  const tampered = Object.create(source);
  tampered.read = (rel) => (rel === BUNDLE ? Buffer.concat([original(rel), Buffer.from('\n//x')]) : original(rel));
  assert.equal(verify(tampered).status, 'unknown');
  const t2 = Object.create(source);
  t2.read = (rel) => (rel.endsWith('UI.json') ? Buffer.from('{}') : original(rel));
  const v2 = verify(t2);
  assert.equal(v2.status, 'modified');
  assert.deepEqual(v2.mismatches, ['.webpack/renderer/assets/img/UI.json']);
});

// Game-independent tests.

test('deobfuscate rejects code without the string-array scheme', () => {
  assert.throws(() => deobfuscate('var a = 1; console.log(a);'), /string array function not found/);
});

test('deobfuscate aborts on an encoded (base64) decoder', () => {
  const strings = Array.from({ length: 20 }, (_, i) => `'s${i}'`).join(',');
  const src = `function A(){var x=[${strings}];A=function(){return x;};return A();}
function D(i,k){var a=A();return D=function(i,k){i=i-0x10;var s=a[i];return atob(s);},D(i,k);}
(function(A,T){var d=D,a=A();while(!![]){try{var c=parseInt(d(0x10))/0x1;if(c===T)break;else a.push(a.shift());}catch(e){a.push(a.shift());}}}(A,1));
console.log(D(0x10));`;
  assert.throws(() => deobfuscate(src), /encoded string scheme/);
});

test('schemas reject unknown fields', () => {
  const errors = validateAll({ hitvfx: { NONE: { isTintFill: false, targetTint: 1, duration: 1, bogus: 1 } } });
  assert.equal(errors.length, 1);
  assert.match(errors[0], /bogus/);
});
